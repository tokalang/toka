#!/usr/bin/env python3
"""RwMutex read/write guard unlock responsibility; no new unlock protocol."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = """import std/sync::{RwMutex, RwReadLock, RwWriteLock}
import std/error::Error
fn observe_read(held: RwReadLock<i32>, ignored: Result<i32, Error>) {}
fn observe_write(held: RwWriteLock<i32>, ignored: Result<i32, Error>) {}
fn observe_read_result(held: Result<RwReadLock<i32>, Error>, ignored: Result<i32, Error>) {}
fn observe_write_result(held: Result<RwWriteLock<i32>, Error>, ignored: Result<i32, Error>) {}
"""
OWNER = "auto ~mutex = RwMutex<i32>::make_shared(7)\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-rw-unlock-") as directory:
        work = Path(directory)
        def source(name, body):
            path = work / (name + ".tk")
            path.write_text(PREFIX + "fn main() -> i32 {\n" + body + "\n}\n")
            return path
        def compile(path, *flags):
            return subprocess.run([str(compiler), "--workspace-node", "rw-unlock-test", "--workspace-root", str(work),
                                   str(path), *map(str, flags)], cwd=ROOT, env=env,
                                  capture_output=True, text=True, timeout=60)
        rejected_count = controls = 0
        for mode in ("read", "write"):
            acquire = f"mutex.{mode}_lock()"
            access = "borrow" if mode == "read" else "borrow_mut"
            held = f"auto held = {acquire}.unwrap()\n"
            after = f"\nheld.{access}()\n{acquire}\nreturn 0"
            negatives = {
                "direct": OWNER + held + "auto ignored = mutex.unlock()" + after,
                "qualified": OWNER + held + "RwMutex<i32>::unlock(mutex)" + after,
                "alias": OWNER + "auto ~other = ~mutex\n" + held + "other.unlock()" + after,
                "moved": OWNER + held + "auto moved = cede held\nmutex.unlock()\n" + f"moved.{access}()\nreturn 0",
                "result": OWNER + f"auto pending = {acquire}\nmutex.unlock()\nreturn 0",
                "temporary": OWNER + f"observe_{mode}({acquire}.unwrap(), mutex.unlock())\nreturn 0",
                "temporary-result": OWNER + f"observe_{mode}_result({acquire}, mutex.unlock())\nreturn 0",
            }
            if mode == "read":
                negatives["one-reader-left"] = OWNER + held + (
                    "auto remaining = mutex.read_lock().unwrap()\ncede held\nmutex.unlock()\n"
                    "remaining.borrow()\nreturn 0")
            for label, body in negatives.items():
                name = mode + "-" + label
                path = source(name, body)
                normal = compile(path, "--check-only")
                shadow = compile(path, "--check-only", "--non-call-transfer-shadow=json")
                assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
                assert "ActiveGuardOwnsUnlock" in normal.stderr, (name, normal.stderr)
                assert "E0438" not in normal.stderr and "E0410" not in normal.stderr, (name, normal.stderr)
                for flag, ext in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (name + ext)
                    result = compile(path, flag, "-o", output)
                    assert result.returncode == 1 and "ActiveGuardOwnsUnlock" in result.stderr and not output.exists(), result.stderr
                rejected_count += 1
                print("PASS reject/parity/rollback: " + name, flush=True)
            # Only checking guard tracking here, not asserting that an unlock
            # without a matching native acquisition is runtime-valid.
            for label, body in {
                "released": OWNER + held + "cede held\nmutex.unlock()\nreturn 0",
                "out-of-scope": OWNER + "{\n" + held + "}\nmutex.unlock()\nreturn 0",
                "other-owner": OWNER + held + "auto ~other = RwMutex<i32>::make_shared(8)\nother.unlock()\nreturn 0",
            }.items():
                name = mode + "-" + label
                result = compile(source(name, body), "--check-only")
                assert result.returncode == 0, (name, result.stderr)
                controls += 1

        cc = os.environ.get("CC", "clang")
        darwin = platform.system() == "Darwin"
        runtime, hook = work / "runtime.o", work / ("unlock.dylib" if darwin else "unlock.o")
        for src, output in ((ROOT / "lib/sys/toka_rt.c", runtime),
                            (ROOT / "tests/runtime/native_sync_unlock_count.c", hook)):
            subprocess.run([cc, "-std=c11", "-pthread", "-dynamiclib" if darwin and output == hook else "-c",
                            str(src), "-o", str(output)], check=True, capture_output=True)
        runs = (
            ("write-once", "auto held = mutex.write_lock().unwrap()", 1),
            ("read-once", "auto held = mutex.read_lock().unwrap()", 1),
            ("two-read-guards", "auto first = mutex.read_lock().unwrap()\nauto second = mutex.read_lock().unwrap()", 2),
            ("write-then-read", "auto first = mutex.write_lock().unwrap()\ncede first\nauto second = mutex.read_lock().unwrap()", 2),
        )
        for name, body, count in runs:
            path = work / (name + ".tk")
            path.write_text(PREFIX + "extern fn audit_unlock_count() -> i32\nfn test() {\n" + OWNER + body +
                            f"\n}}\nfn main() -> i32 {{\ntest()\nreturn (unsafe audit_unlock_count()) - {count}\n}}\n")
            obj, binary = work / (name + ".o"), work / name
            normal = compile(path, "--check-only")
            shadow = compile(path, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            built = compile(path, "-c", "-o", obj)
            assert built.returncode == 0, built.stderr
            subprocess.run([cc, str(obj), str(runtime), str(hook), "-pthread", "-lm",
                            *([] if darwin else ["-Wl,--wrap=" + symbol for symbol in
                                ("pthread_mutex_init", "pthread_mutex_unlock", "pthread_rwlock_init", "pthread_rwlock_unlock")]),
                            "-o", str(binary)], check=True, capture_output=True)
            ran = subprocess.run([str(binary)], env=dict(env, **({"DYLD_INSERT_LIBRARIES": str(hook)} if darwin else {})),
                                 capture_output=True, text=True, timeout=10)
            assert ran.returncode == 0, (name, ran.returncode, ran.stderr)
            print(f"PASS runtime: {name}, {count} native unlock(s)", flush=True)
        print(f"RwMutex unlock: {rejected_count} rejection/parity/rollback cases; {controls} check-only controls; {len(runs)} counted runtime cases")


if __name__ == "__main__":
    main()
