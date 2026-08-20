#!/usr/bin/env python3
"""
Toka Atomic Durable Replace Crash Harness

Scope & Verification Invariants:
1. Verifies the 5-step atomic file replacement protocol under simulated sudden process
   termination (SIGKILL injected at exact native failpoints).
2. Verifies that at no point does a crash leave the target file truncated, empty, or corrupted.
3. Invariant:
   - Failpoint 1 (after temp write): Target file MUST remain intact as original content.
   - Failpoint 2 (after temp sync): Target file MUST remain intact as original content.
   - Failpoint 3 (after rename, before parent dir sync): Target file MUST be valid original content
     OR valid new content (atomic rename visibility); MUST NOT be partial/corrupted.
   - Failpoint 4 (after parent dir sync): Target file MUST be valid new content.
"""

import os
import sys
import subprocess
import shutil
import tempfile

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    tokac_bin = os.path.join(repo_root, "build", "bin", "tokac")
    target_src = os.path.join(repo_root, "tests", "conformance", "io", "durable_replace_crash_target.tk")

    # Detect LLVM Clang compiler
    clang_bin = "clang"
    if os.path.exists("/opt/homebrew/opt/llvm/bin/clang"):
        clang_bin = "/opt/homebrew/opt/llvm/bin/clang"
    elif shutil.which("clang-20"):
        clang_bin = "clang-20"
    elif shutil.which("clang-19"):
        clang_bin = "clang-19"

    test_dir = tempfile.mkdtemp(prefix="toka_crash_harness_")
    toka_rt_testing_obj = os.path.join(test_dir, "toka_rt_testing.o")
    target_obj = os.path.join(test_dir, "crash_target.o")
    target_bin = os.path.join(test_dir, "crash_target")
    target_file = os.path.join(test_dir, "manifest.json")

    try:
        print("[Crash Harness] Compiling test runtime with -DTOKA_TESTING=1...")
        rt_src = os.path.join(repo_root, "lib", "sys", "toka_rt.c")
        rt_cmd = [
            clang_bin,
            "-DTOKA_HAS_OPENSSL=1",
            "-DTOKA_TESTING=1",
            "-I/opt/homebrew/include",
            "-c", rt_src,
            "-o", toka_rt_testing_obj
        ]
        res = subprocess.run(rt_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Failed to build testing runtime object:\n{res.stderr}")
            sys.exit(1)

        print("[Crash Harness] Compiling durable_replace_crash_target.tk...")
        compile_cmd = [
            tokac_bin,
            "-I", os.path.join(repo_root, "lib"),
            target_src,
            "-c",
            "-o", target_obj
        ]
        res = subprocess.run(compile_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Compilation failed:\n{res.stderr}\n{res.stdout}")
            sys.exit(1)

        # Link target binary with test runtime object
        link_cmd = [
            clang_bin,
            target_obj,
            toka_rt_testing_obj,
            "-L/opt/homebrew/lib",
            "-lssl", "-lcrypto",
            "-o", target_bin
        ]
        res = subprocess.run(link_cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Linking failed:\n{res.stderr}")
            sys.exit(1)

        # Baseline Test (No Failpoint)
        with open(target_file, "w") as f:
            f.write("VERSION_00")

        env = os.environ.copy()
        env["TOKA_CRASH_TARGET_PATH"] = target_file
        env["TOKA_CRASH_CONTENT"] = "VERSION_BASELINE"
        env.pop("TOKA_FAILPOINT", None)

        run_res = subprocess.run([target_bin], env=env, capture_output=True, text=True)
        assert run_res.returncode == 0, f"Baseline run failed: {run_res.stderr}"
        with open(target_file, "r") as f:
            content = f.read()
        assert content == "VERSION_BASELINE", f"Baseline content mismatch: {content}"
        print("[Crash Harness] Baseline durable replace OK.")

        # Failpoint 1: after_temp_write (Killed before sync and rename)
        with open(target_file, "w") as f:
            f.write("VERSION_00")
        env["TOKA_FAILPOINT"] = "after_temp_write"
        env["TOKA_CRASH_CONTENT"] = "VERSION_FAILPOINT_1"
        run_res = subprocess.run([target_bin], env=env, capture_output=True, text=True)
        assert run_res.returncode in (-9, 137), f"Expected SIGKILL, got {run_res.returncode}"
        with open(target_file, "r") as f:
            content = f.read()
        assert content == "VERSION_00", f"Expected target intact as VERSION_00, got: {content}"
        print("[Crash Harness] Failpoint 'after_temp_write' recovery verified (target intact).")

        # Failpoint 2: after_temp_sync (Killed before rename)
        with open(target_file, "w") as f:
            f.write("VERSION_00")
        env["TOKA_FAILPOINT"] = "after_temp_sync"
        env["TOKA_CRASH_CONTENT"] = "VERSION_FAILPOINT_2"
        run_res = subprocess.run([target_bin], env=env, capture_output=True, text=True)
        assert run_res.returncode in (-9, 137), f"Expected SIGKILL, got {run_res.returncode}"
        with open(target_file, "r") as f:
            content = f.read()
        assert content == "VERSION_00", f"Expected target intact as VERSION_00, got: {content}"
        print("[Crash Harness] Failpoint 'after_temp_sync' recovery verified (target intact).")

        # Failpoint 3: after_rename (Killed after atomic rename, before parent dir sync)
        with open(target_file, "w") as f:
            f.write("VERSION_00")
        env["TOKA_FAILPOINT"] = "after_rename"
        env["TOKA_CRASH_CONTENT"] = "VERSION_FAILPOINT_3"
        run_res = subprocess.run([target_bin], env=env, capture_output=True, text=True)
        assert run_res.returncode in (-9, 137), f"Expected SIGKILL, got {run_res.returncode}"
        with open(target_file, "r") as f:
            content = f.read()
        # Correct contract before dir sync: either old or new version is valid, never corrupted
        assert content in ("VERSION_00", "VERSION_FAILPOINT_3"), f"Target corrupted: {content}"
        print("[Crash Harness] Failpoint 'after_rename' recovery verified (valid uncorrupted state: " + content + ").")

        # Failpoint 4: after_parent_dir_sync
        with open(target_file, "w") as f:
            f.write("VERSION_00")
        env["TOKA_FAILPOINT"] = "after_parent_dir_sync"
        env["TOKA_CRASH_CONTENT"] = "VERSION_FAILPOINT_4"
        run_res = subprocess.run([target_bin], env=env, capture_output=True, text=True)
        assert run_res.returncode in (-9, 137), f"Expected SIGKILL, got {run_res.returncode}"
        with open(target_file, "r") as f:
            content = f.read()
        assert content == "VERSION_FAILPOINT_4", f"Expected target committed as VERSION_FAILPOINT_4, got: {content}"
        print("[Crash Harness] Failpoint 'after_parent_dir_sync' recovery verified (committed new version).")

        print("\n[Crash Harness] All Failpoint Visibility & Crash Recovery Invariants PASSED!")
    finally:
        shutil.rmtree(test_dir, ignore_errors=True)

if __name__ == "__main__":
    main()
