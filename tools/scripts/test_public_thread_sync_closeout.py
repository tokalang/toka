#!/usr/bin/env python3
"""Required positive sync/thread matrix. Open targets FAIL, never xfail/skip."""
import argparse
import os
from pathlib import Path
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
    failures = []
    passed = 0
    with tempfile.TemporaryDirectory(prefix="toka-thread-sync-candidate-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread",
                        "-c", str(ROOT / "lib/sys/toka_rt.c"), "-o", str(runtime)], check=True)

        def compile_source(name, *flags):
            return subprocess.run([str(compiler), str(FIXTURES / name), *map(str, flags)],
                                  cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)

        for name in ("sync_unique_guard.tk", "sync_managed_storage_pending.tk",
                     "sync_shared_guard.tk", "sync_thread_pending.tk", "captured_shared_receiver.tk",
                     "sync_thread_resource_replace.tk"):
            normal = compile_source(name, "--check-only")
            shadow = compile_source(name, "--check-only", "--non-call-transfer-shadow=json")
            if normal.returncode != 0 or shadow.returncode != 0 or normal.stderr != shadow.stderr:
                failures.append(name + ": required frontend/parity positive\n" + normal.stderr + shadow.stderr)
                continue
            executable = work / name
            built = compile_source(name, runtime, "-o", executable)
            if built.returncode != 0:
                failures.append(name + ": required build positive\n" + built.stderr)
                continue
            try:
                ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
                if ran.returncode != 0:
                    failures.append(name + f": required runtime positive rc={ran.returncode}\n" + ran.stderr)
                    continue
            except subprocess.TimeoutExpired:
                failures.append(name + ": runtime timeout")
                continue
            passed += 1
            print("PASS runtime/parity: " + name, flush=True)

        for name, reason in (
            ("sync_reject_managed_pointee_write.tk", "E04573"),
            ("sync_reject_read_guard_rebind.tk", "E04573"),
            ("sync_reject_managed_guard_owner_drop.tk", "ActiveDerivedBorrow"),
            ("sync_reject_readonly_raw_slot.tk", "E04573"),
        ):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                artifact = work / (name + suffix)
                rejected = compile_source(name, mode, "-o", artifact)
                if rejected.returncode != 1 or reason not in rejected.stderr or artifact.exists():
                    failures.append(name + mode + ": expected rejection without artifact\n" + rejected.stderr)
                else:
                    passed += 1
                    print("PASS reject: " + name + " " + mode, flush=True)
    for failure in failures:
        print("FAIL " + failure, flush=True)
    print(f"sync/thread closeout: {passed}/{passed + len(failures)} checks; {len(failures)} failed; no skips")
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
