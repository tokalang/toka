#!/usr/bin/env python3
"""Prepared composite allocation: success and both allocation-failure edges."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WRAPPED = ("malloc", "free", "_Exit", "pthread_mutex_init", "pthread_mutex_destroy",
           "pthread_cond_init", "pthread_cond_destroy")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    source = ROOT / "tests/semantics/std_thread_handoff/sync_composite_allocation.tk"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    cc = os.environ.get("CC", "clang")
    darwin = platform.system() == "Darwin"
    with tempfile.TemporaryDirectory(prefix="toka-native-composite-") as directory:
        work = Path(directory)
        runtime, hooks = work / "runtime.o", work / ("hooks.dylib" if darwin else "hooks.o")
        for src, out in ((ROOT / "lib/sys/toka_rt.c", runtime),
                         (ROOT / "tests/runtime/native_sync_composite_hooks.c", hooks)):
            subprocess.run([cc, "-std=c11", "-pthread", "-dynamiclib" if darwin and out == hooks else "-c",
                            str(src), "-o", str(out)], check=True, capture_output=True)
        def compile(*flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  env=env, cwd=ROOT, capture_output=True, text=True, timeout=60)
        normal = compile("--check-only")
        shadow = compile("--check-only", "--non-call-transfer-shadow=json")
        assert normal.returncode == shadow.returncode == 0, normal.stderr + shadow.stderr
        assert normal.stderr == shadow.stderr
        obj, exe = work / "source.o", work / "run"
        result = compile("-c", "-o", obj)
        assert result.returncode == 0 and obj.exists(), result.stderr
        subprocess.run([cc, str(obj), str(runtime), str(hooks), "-pthread", "-lm",
                        *([] if darwin else ["-Wl,--wrap=" + n for n in WRAPPED]), "-o", str(exe)],
                       check=True, capture_output=True)
        runs = 0
        for kind in (0, 1):
            for shared in (0, 1):
                for fault in ((0, 1, 2) if shared else (0, 1)):
                    case = dict(env, TOKA_COMPOSITE_KIND=str(kind), TOKA_COMPOSITE_SHARED=str(shared),
                                TOKA_COMPOSITE_FAULT=str(fault))
                    if darwin: case["DYLD_INSERT_LIBRARIES"] = str(hooks)
                    result = subprocess.run([str(exe)], env=case, capture_output=True, timeout=10)
                    assert result.returncode == (134 if fault else 0), (kind, shared, fault, result.returncode, result.stderr)
                    runs += 1
        faults = ("missing", "incomplete", "site", "binding", "definition", "input", "type",
                  "fields", "field-index", "field-type", "field-witness", "control:missing", "control:field-witness")
        for fault in faults:
            for mode, ext in (("-c", ".o"), ("--emit-llvm", ".ll")):
                artifact = work / (fault + ext)
                result = compile(mode, "-o", artifact, "--native-sync-allocation-fault=composite:" + fault)
                assert result.returncode == 1 and "E0701" in result.stderr and not artifact.exists(), result.stderr
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library,
                        ignore=shutil.ignore_patterns("*.o", "*.a", "*.tki", "*.ll"))
        sync = (ROOT / "lib/std/sync.tk").read_text()
        target = "auto ^owner# = new Once(mutex = Mutex<bool>(handle = 0:Addr, data_ptr = 0:Addr), done = false)"
        assert sync.count(target) == 1
        sync = sync.replace(target, target.replace("handle = 0:Addr", "handle = 1:Addr"))
        site = "owner = cede prepared\n        return ^owner"
        # The first instance is Once::make. Observe the restored source after
        # rejecting the nonempty target, without changing the expected oracle.
        assert site in sync
        sync = sync.replace(site, "owner = cede prepared\n        auto restored = prepared.done\n        return ^owner", 1)
        (library / "std/sync.tk").write_text(sync)
        negative = work / "nonempty.tk"
        negative.write_text("import std/sync::Once\nfn main() -> i32 { auto ^owner = Once::make()\nreturn 0 }\n")
        for mode, ext in (("-c", ".o"), ("--emit-llvm", ".ll")):
            artifact = work / ("nonempty" + ext)
            result = subprocess.run([str(compiler), "--workspace-node", "composite-negative", "--workspace-root", str(work),
                                     str(negative), mode, "-o", str(artifact)], cwd=work,
                                    env=dict(env, TOKA_LIB=str(library)), capture_output=True, text=True, timeout=60)
            assert result.returncode == 1 and "EmptyNativeChildRequired" in result.stderr, result.stderr
            assert "E0438" not in result.stderr and "E0410" not in result.stderr and not artifact.exists(), result.stderr
        print(f"composite allocation: {runs}/10 runtime cases, {len(faults) * 2} fault denials, 2 rollback denials; no skips")


if __name__ == "__main__":
    main()
