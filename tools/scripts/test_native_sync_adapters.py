#!/usr/bin/env python3
"""Execute exact private SDK adapter source, without publishing a witness."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WRAPPED = ("malloc", "free", "_Exit", "pthread_mutex_init", "pthread_mutex_destroy",
           "pthread_mutex_lock", "pthread_mutex_unlock", "pthread_rwlock_init",
           "pthread_rwlock_destroy", "pthread_rwlock_rdlock", "pthread_rwlock_wrlock",
           "pthread_rwlock_unlock", "pthread_cond_init", "pthread_cond_destroy",
           "pthread_cond_wait", "pthread_cond_signal", "pthread_cond_broadcast")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    cc = os.environ.get("CC", "clang")
    darwin = platform.system() == "Darwin"
    with tempfile.TemporaryDirectory(prefix="toka-native-sync-adapters-") as directory:
        work = Path(directory)
        # Private functions stay private in the SDK. This fixture appends an
        # in-module driver to the exact source, not a separate reimplementation.
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library,
                        ignore=shutil.ignore_patterns("*.o", "*.a", "*.so", "*.dylib", "*.tki", "*.ll"))
        driver = (ROOT / "tests/semantics/std_thread_handoff/native_sync_adapter_driver.tkfrag").read_text()
        driver = driver.replace("fn main()", "pub fn __native_test_entry()")
        (library / "std/sync.tk").write_text((ROOT / "lib/std/sync.tk").read_text() + "\n" + driver)
        env["TOKA_LIB"] = str(library)
        source = work / "driver.tk"
        source.write_text("import std/sync::__native_test_entry\nfn main() -> i32 { return __native_test_entry() }\n")
        runtime = work / "toka_rt.o"
        hooks = work / ("hooks.dylib" if darwin else "hooks.o")
        for path, output, kind in ((ROOT / "lib/sys/toka_rt.c", runtime, "-c"),
                                   (ROOT / "tests/runtime/native_sync_adapter_hooks.c", hooks,
                                    "-dynamiclib" if darwin else "-c")):
            result = subprocess.run([cc, "-std=c11", "-pthread", kind, str(path), "-o", str(output)],
                                    capture_output=True, text=True)
            if result.returncode: raise RuntimeError(result.stderr)
        obj = work / "adapter.o"
        built = subprocess.run([str(compiler), "--workspace-node", "native-sync-adapter-test",
                                "--workspace-root", str(work), str(source), "-c", "-o", str(obj)],
                               cwd=work, env=env, capture_output=True, text=True, timeout=60)
        if built.returncode: raise RuntimeError(built.stderr)
        if os.environ.get("TOKA_NATIVE_IR"):
            subprocess.run([str(compiler), "--workspace-node", "native-sync-adapter-test",
                            "--workspace-root", str(work), str(source), "--emit-llvm",
                            "-o", os.environ["TOKA_NATIVE_IR"]], cwd=work, env=env, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        binary = work / "adapter"
        wrapping = [] if darwin else ["-Wl,--wrap=" + name for name in WRAPPED]
        subprocess.run([cc, str(obj), str(runtime), str(hooks), "-pthread", "-lm",
                        *wrapping, "-o", str(binary)], check=True)
        runs = 0
        for category in range(4):
            for native_kind in range(2):
                modes = list(range(10) if native_kind == 0 else range(7))
                if category == 0: modes.append(10)
                for mode in modes:
                    case_env = dict(env, TOKA_SYNC_MODE=str(mode), TOKA_SYNC_KIND=str(native_kind),
                                    TOKA_SYNC_CATEGORY=str(category))
                    if darwin: case_env["DYLD_INSERT_LIBRARIES"] = str(hooks)
                    result = subprocess.run([str(binary)], env=case_env, capture_output=True,
                                            text=True, timeout=10)
                    expected = 0 if mode in (0, 4, 10) else 134
                    if result.returncode != expected:
                        raise RuntimeError(f"category={category} kind={native_kind} mode={mode}: "
                                           f"rc={result.returncode}, expected={expected}\n" + result.stderr)
                    runs += 1
        for mode in (0, 1, 3, 6, 8, 9):
            case_env = dict(env, TOKA_SYNC_MODE=str(mode), TOKA_SYNC_KIND="2", TOKA_SYNC_CATEGORY="0")
            if darwin: case_env["DYLD_INSERT_LIBRARIES"] = str(hooks)
            result = subprocess.run([str(binary)], env=case_env, capture_output=True, text=True, timeout=10)
            expected = 0 if mode == 0 else 134
            if result.returncode != expected:
                raise RuntimeError(f"cond mode={mode}: rc={result.returncode}, expected={expected}\n" + result.stderr)
            runs += 1
        print(f"native adapters: {runs} runtime/failure cases; no witness authority claimed")


if __name__ == "__main__":
    main()
