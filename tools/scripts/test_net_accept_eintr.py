#!/usr/bin/env python3
"""Exercise std/net async accept EINTR retry, EAGAIN suspension, permanent errors, and bounded cancellation."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE_DIR = ROOT / "tests/fixtures/net_eintr"
FIXTURE_TK = FIXTURE_DIR / "test_accept_eintr.tk"
FIXTURE_C = FIXTURE_DIR / "test_net_fault_inject.c"

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def run(command, env=None, timeout=60, cwd=ROOT):
    argv = [str(x) for x in command]
    proc = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)
    return proc

def qualify(build_dir):
    build = Path(build_dir).resolve()
    compiler = build / "bin/tokac"
    require(compiler.is_file(), f"compiler not found at {compiler}")
    cc = os.environ.get("CC", "clang")

    # 1. Verify production library isolation and cleanliness
    prod_net_tk = (ROOT / "lib/std/net.tk").read_text(encoding="utf-8")
    require("__net_test" not in prod_net_tk,
            "__net_test symbols leaked into production lib/std/net.tk")
    require("toka_test_" not in prod_net_tk,
            "test hook symbols leaked into production lib/std/net.tk")
    
    # Verify operation-bound logging in production net.tk
    require("[TcpListener::bind]" in prod_net_tk,
            "operation-bound logging missing from TcpListener::bind")
    require("[net_async_accept_fd]" in prod_net_tk,
            "operation-bound logging missing from net_async_accept_fd")
    require("[net_async_accept_context_fd]" in prod_net_tk,
            "operation-bound logging missing from net_async_accept_context_fd")
    print("  [Isolation Proof] Production lib/std/net.tk contains 0 test observer symbols and carries operation-bound logging.")

    with tempfile.TemporaryDirectory(prefix="toka-net-eintr-") as directory:
        work = Path(directory)

        # 2. Build fault injection helper
        fault_obj = work / "fault_inject.o"
        res_c = run([cc, "-c", str(FIXTURE_C), "-o", str(fault_obj)])
        require(res_c.returncode == 0, f"failed to compile fault inject helper: {res_c.stderr}")

        # 3. Create isolated copy of library
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library, ignore=shutil.ignore_patterns("*.o", "*.tki", "__pycache__"))

        # Instrument sysnet accept to route through toka_test_inject_accept in test copy
        target_sys_files = [library / "sys/macos/net.tk", library / "sys/linux/net.tk"]
        for sys_file in target_sys_files:
            if sys_file.is_file():
                content = sys_file.read_text(encoding="utf-8")
                accept_target = "auto res = libc_accept(fd, *addr_buf, *addr_len_ptr as *u32)"
                require(accept_target in content, f"libc_accept call not found in {sys_file}")
                instrumented = (
                    "extern fn toka_test_inject_accept(fd: i32, *addr: void, *len: u32) -> i32\n" +
                    content.replace(
                        accept_target,
                        "auto res = toka_test_inject_accept(fd, *addr_buf, *addr_len_ptr as *u32)"
                    )
                )
                sys_file.write_text(instrumented, encoding="utf-8")

        # Instrument isolated std/net.tk to trigger canceler when test says to cancel during EINTR
        std_net = library / "std/net.tk"
        net_content = std_net.read_text(encoding="utf-8")
        eintr_branch = "} else if err == 4 { // EINTR: retry accept loop\n                        continue"
        require(net_content.count(eintr_branch) == 2, "expected 2 EINTR branches in lib/std/net.tk")
        
        # Prepend extern fn at file top
        net_content = "extern fn toka_test_is_cancel_triggered() -> i32\n" + net_content

        # Replace the second branch (inside net_async_accept_context_fd) to trigger canceler if armed
        context_eintr_replacement = (
            "} else if err == 4 { // EINTR: retry accept loop\n"
            "                        if toka_test_is_cancel_triggered() != 0 {\n"
            "                            canceler.call()\n"
            "                        }\n"
            "                        continue"
        )
        # Only replace the occurrence in net_async_accept_context_fd
        parts = net_content.split("pub fn net_async_accept_context_fd")
        require(len(parts) == 2, "net_async_accept_context_fd boundary not found")
        parts[1] = parts[1].replace(eintr_branch, context_eintr_replacement, 1)
        net_content = "pub fn net_async_accept_context_fd".join(parts)
        std_net.write_text(net_content, encoding="utf-8")

        # 4. Compile and run positive 4-control test
        test_bin = work / "test_accept_eintr"
        test_env = dict(os.environ, TOKA_LIB=str(library))
        res_build = run([compiler, str(FIXTURE_TK), str(fault_obj), "-o", str(test_bin)], env=test_env, cwd=work)
        require(res_build.returncode == 0, f"failed to compile test_accept_eintr:\n{res_build.stderr}")

        res_run = run([str(test_bin)], env=test_env, cwd=work)
        require(res_run.returncode == 0, f"test_accept_eintr execution failed (code={res_run.returncode}):\n{res_run.stderr}\n{res_run.stdout}")
        require("All 4 EINTR/Accept controls passed successfully!" in res_run.stdout,
                f"unexpected output from test_accept_eintr:\n{res_run.stdout}")
        print("  [Positive Control] All 4 controls passed (EINTR retry, EAGAIN reactor suspension, permanent error, bounded cancellation).")

        # 5. Negative Oracle Control: mutate isolated library to delete err == 4 handling
        # and verify that the test FAILS (proving the oracle does not bypass the production branch)
        mutated_net = net_content.replace(eintr_branch, "// err == 4 deleted for oracle test").replace(context_eintr_replacement, "// err == 4 deleted for oracle test")
        std_net.write_text(mutated_net, encoding="utf-8")

        res_neg_build = run([compiler, str(FIXTURE_TK), str(fault_obj), "-o", str(test_bin)], env=test_env, cwd=work)
        require(res_neg_build.returncode == 0, f"failed to compile test with mutated library:\n{res_neg_build.stderr}")

        res_neg_run = run([str(test_bin)], env=test_env, cwd=work)
        require(res_neg_run.returncode != 0,
                f"Oracle failure: test unexpectedly PASSED even though err == 4 handling was deleted!\nstdout:\n{res_neg_run.stdout}")
        print(f"  [Negative Oracle Proof] Mutated library (deleting err == 4) correctly FAILED with returncode={res_neg_run.returncode}.")

    print("test_net_accept_eintr: PASS (authentic oracle, isolated observation, production purity verified)")

if __name__ == "__main__":
    default_build = ROOT / "build"
    if not (default_build / "bin/tokac").is_file():
        default_build = ROOT / "../builds/fix-r3-task-escape"
    parser = argparse.ArgumentParser(description="Test std/net async accept EINTR and cancellation handling")
    parser.add_argument("--build-dir", default=str(default_build),
                        help="Path to CMake build directory containing tokac")
    args = parser.parse_args()
    qualify(args.build_dir)
