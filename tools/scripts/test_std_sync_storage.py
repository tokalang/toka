#!/usr/bin/env python3
"""Accepted storage migration checks; managed-T guard morphology remains open."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/std_thread_handoff"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-sync-storage-") as directory:
        work = Path(directory)
        darwin = platform.system() == "Darwin"
        runtime = work / "runtime.o"
        hooks = work / ("hooks.dylib" if darwin else "hooks.o")
        for source, target in ((ROOT / "lib/sys/toka_rt.c", runtime),
                               (ROOT / "tests/runtime/sync_allocation_hooks.c", hooks)):
            output_kind = "-dynamiclib" if darwin and target == hooks else "-c"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread",
                            output_kind, str(source), "-o", str(target)], check=True)

        def compile_source(name, *flags):
            return subprocess.run([str(compiler), str(FIXTURES / name), *map(str, flags)],
                                  env=env, cwd=ROOT, capture_output=True, text=True, timeout=45)

        for name in ("sync_storage.tk", "sync_null_allocation.tk"):
            normal = compile_source(name, "--check-only")
            shadow = compile_source(name, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0, normal.stderr + shadow.stderr
            assert normal.stderr == shadow.stderr, name + " diagnostic parity"
            obj, binary = work / (name + ".o"), work / name
            built = compile_source(name, "-c", "-o", obj)
            assert built.returncode == 0, built.stderr
            wrap = [] if darwin else ["-Wl,--wrap=malloc"]
            subprocess.run([os.environ.get("CC", "clang"), str(obj), str(runtime), str(hooks),
                            "-pthread", "-lm", *wrap, "-o", str(binary)], check=True)
            runtime_env = dict(env, DYLD_INSERT_LIBRARIES=str(hooks)) if darwin else env
            result = subprocess.run([str(binary)], env=runtime_env, capture_output=True, timeout=10)
            # The reviewed private factory cleans partial construction then
            # uses allocation-free/non-I/O _Exit(134), rather than the old
            # unchecked-wrapper abort path. Only this migrated fatal oracle
            # changes; ordinary positives and rejection reasons stay fixed.
            expected = 134 if name == "sync_null_allocation.tk" else 0
            assert result.returncode == expected, (name, result.returncode, result.stderr)
        for name, reason in (("sync_reject_readonly.tk", "E04573"),
                             ("sync_reject_live_guard.tk", "ActiveDerivedBorrow")):
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                rejected = compile_source(name, flag, "-o", output)
                assert rejected.returncode == 1 and reason in rejected.stderr, rejected.stderr
                assert not output.exists(), name + " rejected artifact"
    print("sync storage subset: scalar/resource/remaining shared lock owner runtime; null allocation fatal; "
          "2 strict parity cases; 4 readonly/live-guard no-artifact checks. "
          "Complete native witness/thread qualification is a separate required gate.")


if __name__ == "__main__":
    main()
