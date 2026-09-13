#!/usr/bin/env python3
"""Public native owner allocation/failure responsibility, with real source plans."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from test_native_sync_adapters import WRAPPED

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/semantics/std_thread_handoff/native_sync_public_owner.tk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    cc = os.environ.get("CC", "clang")
    darwin = platform.system() == "Darwin"
    with tempfile.TemporaryDirectory(prefix="toka-native-public-owner-") as directory:
        work = Path(directory)
        runtime = work / "runtime.o"
        hooks = work / ("hooks.dylib" if darwin else "hooks.o")
        for source, target in ((ROOT / "lib/sys/toka_rt.c", runtime),
                               (ROOT / "tests/runtime/native_sync_adapter_hooks.c", hooks)):
            subprocess.run([cc, "-std=c11", "-pthread",
                            "-dynamiclib" if darwin and target == hooks else "-c",
                            str(source), "-o", str(target)], check=True, capture_output=True)
        normal = subprocess.run([str(compiler), str(SOURCE), "--check-only"],
                                env=env, capture_output=True, text=True, timeout=60)
        shadow = subprocess.run([str(compiler), str(SOURCE), "--check-only", "--non-call-transfer-shadow=json"],
                                env=env, capture_output=True, text=True, timeout=60)
        assert normal.returncode == shadow.returncode == 0, normal.stderr + shadow.stderr
        assert normal.stderr == shadow.stderr, "public owner diagnostic parity"
        obj = work / "owner.o"
        built = subprocess.run([str(compiler), str(SOURCE), "-c", "-o", str(obj)],
                               env=env, capture_output=True, text=True, timeout=60)
        assert built.returncode == 0 and obj.exists(), built.stderr
        binary = work / "owner"
        wrap = [] if darwin else ["-Wl,--wrap=" + name for name in WRAPPED]
        subprocess.run([cc, str(obj), str(runtime), str(hooks), "-pthread", "-lm", *wrap,
                        "-o", str(binary)], check=True, capture_output=True)
        runs = 0
        for owner_kind in (1, 2):
            for category in range(4):
                for mode in ((0, 1, 2, 3, 11) if owner_kind == 1 else (0, 1, 2, 3, 11, 12)):
                    case_env = dict(env, TOKA_SYNC_PUBLIC_OWNER=str(owner_kind), TOKA_SYNC_KIND="0",
                                    TOKA_SYNC_CATEGORY=str(category), TOKA_SYNC_MODE=str(mode))
                    if darwin: case_env["DYLD_INSERT_LIBRARIES"] = str(hooks)
                    ran = subprocess.run([str(binary)], env=case_env, capture_output=True, timeout=10)
                    assert ran.returncode == (0 if mode == 0 else 134), (
                        owner_kind, category, mode, ran.returncode, ran.stderr)
                    runs += 1
        # RwMutex shared owners use the same checked allocation handoff;
        # CondVar is native-only and must not fabricate a payload allocation.
        for kind in (1, 2):
            for owner_kind in ((2,) if kind == 1 else (1, 2)):
                for category in (range(4) if kind == 1 else (0,)):
                    modes = (0, 1, 2, 3, 11, 12) if kind == 1 else (
                        (0, 1, 3, 11, 12) if owner_kind == 2 else (0, 1, 3, 11))
                    for mode in modes:
                        case_env = dict(env, TOKA_SYNC_PUBLIC_OWNER=str(owner_kind), TOKA_SYNC_KIND=str(kind),
                                        TOKA_SYNC_CATEGORY=str(category), TOKA_SYNC_MODE=str(mode))
                        if darwin: case_env["DYLD_INSERT_LIBRARIES"] = str(hooks)
                        ran = subprocess.run([str(binary)], env=case_env, capture_output=True, timeout=10)
                        assert ran.returncode == (0 if mode == 0 else 134), (kind, owner_kind, category, mode, ran.returncode, ran.stderr)
                        runs += 1
        # Reject a non-empty wrapper before giving it allocation-cleanup
        # authority. A later read of the prepared source also checks that this
        # additional Sema rejection restores the destructive RHS state.
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library,
                        ignore=shutil.ignore_patterns("*.o", "*.a", "*.tki", "*.ll"))
        sync = (ROOT / "lib/std/sync.tk").read_text()
        original = ("auto ~mutex# = new Mutex<'T>(handle = 0:Addr, data_ptr = 0:Addr)\n"
                    "        mutex = cede prepared")
        assert sync.count(original) == 1
        sync = sync.replace(original,
            "auto ~mutex# = new Mutex<'T>(handle = 1:Addr, data_ptr = 0:Addr)\n"
            "        mutex = cede prepared\n        auto allocation_rollback_observed = prepared.handle")
        (library / "std/sync.tk").write_text(sync)
        negative = work / "nonempty.tk"
        # Cover both the std/sync-prepared bool instance and a subsequent i32
        # instance after the first error; missing recipes must not bypass the
        # rollback on that later check either.
        for element, value in (("bool", "true"), ("i32", "7")):
            negative.write_text("import std/sync::Mutex\nfn main() -> i32 {\n"
                f"auto ~owner = Mutex<{element}>::make_shared({value})\nreturn 0\n" + "}\n")
            for flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / ("nonempty-" + element + extension)
                rejected = subprocess.run([str(compiler), "--workspace-node", "native-owner-negative",
                    "--workspace-root", str(work), str(negative), flag, "-o", str(output)],
                    cwd=work, env=dict(env, TOKA_LIB=str(library)), capture_output=True, text=True, timeout=60)
                assert rejected.returncode != 0 and "EmptyCarrierLiteralRequired" in rejected.stderr, rejected.stderr
                assert "E0438" not in rejected.stderr and "E0410" not in rejected.stderr, rejected.stderr
                assert not output.exists(), output
        faults = 0
        for stage in ("", "control:"):
            for fault in ("missing", "incomplete", "site", "binding", "definition", "input", "type"):
                for output_flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (stage.replace(":", "-") + fault + extension)
                    rejected = subprocess.run([str(compiler), str(SOURCE),
                        "--native-sync-allocation-fault=" + stage + fault, output_flag, "-o", str(output)],
                        env=env, capture_output=True, text=True, timeout=60)
                    assert rejected.returncode != 0 and "E0701" in rejected.stderr, rejected.stderr
                    assert not output.exists(), output
                    faults += 1
        print(f"public native owner: {runs} success/failure responsibility cases; "
              f"{faults} allocation/control-plan faults without artifacts; "
              "nonempty-carrier rejection/rollback; strict parity")


if __name__ == "__main__":
    main()
