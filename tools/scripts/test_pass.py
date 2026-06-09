#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# tools/scripts/test_pass.py - High-performance Parallel Test Runner in Pure Python
# Compatible with Python 3.5+ (no third-party library dependencies)

import os
import sys
import time
import shutil
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

# Colors
if sys.platform != "win32":
    GREEN = "\033[0;32m"
    RED = "\033[0;31m"
    YELLOW = "\033[0;33m"
    GRAY = "\033[0;90m"
    NC = "\033[0m"
else:
    GREEN = RED = YELLOW = GRAY = NC = ""

# Configuration & Paths
TOKAC = os.environ.get("TOKAC", "./build/bin/tokac")
if sys.platform == "win32":
    TOKAC += ".exe"

# Locks for thread-safe output printing
print_lock = threading.Lock()

def safe_print(message):
    with print_lock:
        print(message)
        sys.stdout.flush()

def get_cores():
    # Respect CORES env var, default to os.cpu_count() or 4
    cores_env = os.environ.get("CORES")
    if cores_env:
        try:
            return int(cores_env)
        except ValueError:
            pass
    
    cpu_count = os.cpu_count()
    if cpu_count:
        return cpu_count
    return 4

def find_compiler_tool(name, default):
    candidates = [name + "-20", name]
    if sys.platform == "darwin":
        home = os.environ.get("HOME", "")
        prefixes = [
            os.path.join(home, "intel-brew/opt/llvm@20/bin"),
            os.path.join(home, "intel-brew/opt/llvm/bin"),
            "/opt/homebrew/opt/llvm@20/bin",
            "/opt/homebrew/opt/llvm/bin",
            "/usr/local/opt/llvm@20/bin",
            "/usr/local/opt/llvm/bin",
        ]
        for prefix in prefixes:
            for cand in candidates:
                full_path = os.path.join(prefix, cand)
                if os.path.exists(full_path) and os.access(full_path, os.X_OK):
                    return full_path

    # Try finding version 20 first, then standard, then default
    for candidate in candidates:
        if shutil.which(candidate):
            return candidate
    return default

def get_llvm_flags(llvm_config):
    # shutil.which might fail on absolute paths, so we use os.path.exists for absolute path llvm-config
    if not (os.path.isabs(llvm_config) and os.path.exists(llvm_config)) and not shutil.which(llvm_config):
        return "", ""
    try:
        # Get cxxflags
        cxxflags = subprocess.check_output([llvm_config, "--cxxflags"], universal_newlines=True).strip()
        cxxflags = " ".join(cxxflags.split()) # merge newlines
        
        # Get ldflags & libs
        ldflags = subprocess.check_output([llvm_config, "--ldflags", "--libs"], universal_newlines=True).strip()
        ldflags = " ".join(ldflags.split())
        return cxxflags, ldflags
    except Exception as e:
        return "", ""

def get_sysroot_flags():
    if sys.platform == "darwin":
        flags = []
        try:
            sdk_path = subprocess.check_output(["xcrun", "--show-sdk-path"], universal_newlines=True).strip()
            flags.append("-isysroot " + sdk_path)
        except Exception:
            pass
        try:
            out = subprocess.check_output(["file", TOKAC], universal_newlines=True)
            if "x86_64" in out:
                flags.append("-arch x86_64")
            elif "arm64" in out:
                flags.append("-arch arm64")
        except Exception:
            pass
        return " ".join(flags)
    return ""

def rebuild_runtime(clang, clangxx, sysroot, cxxflags):
    # Always force rebuild toka_rt.o and llvm_shim.o
    safe_print("Compiling native runtime and shim objects...")
    
    # 1. compile toka_rt.c
    try:
        if os.path.exists("lib/sys/toka_rt.o"):
            os.remove("lib/sys/toka_rt.o")
    except Exception:
        pass
    
    cmd_rt = [clang]
    if sysroot:
        cmd_rt.extend(sysroot.split())
    cmd_rt.extend(["-c", "lib/sys/toka_rt.c", "-o", "lib/sys/toka_rt.o"])
    
    rt_res = subprocess.run(cmd_rt, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if rt_res.returncode != 0:
        safe_print(RED + "Failed to compile toka_rt.c" + NC)
        safe_print(rt_res.stderr.decode("utf-8", errors="ignore"))
        sys.exit(1)
        
    # 2. compile llvm_shim.cpp
    try:
        if os.path.exists("lib/sys/llvm_shim.o"):
            os.remove("lib/sys/llvm_shim.o")
    except Exception:
        pass
    
    cmd_shim = [clangxx]
    if sysroot:
        cmd_shim.extend(sysroot.split())
    cmd_shim.extend(["-O3", "-c", "lib/sys/llvm_shim.cpp", "-o", "lib/sys/llvm_shim.o"])
    if cxxflags:
        cmd_shim.extend(cxxflags.split())
        
    if sys.platform in ["win32", "cygwin", "msys"]:
        cmd_shim.append("-DLLVM_SHARED_LIBS")
        
    shim_res = subprocess.run(cmd_shim, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if shim_res.returncode != 0:
        safe_print(RED + "Failed to compile llvm_shim.cpp" + NC)
        safe_print(shim_res.stderr.decode("utf-8", errors="ignore"))
        sys.exit(1)

def run_single_test(test_path, clangxx, sysroot, ldflags_libs):
    file_name = os.path.basename(test_path)
    safe_target = test_path.replace("/", "_").replace("\\", "_")
    
    out_dir = "/tmp/tokac_tests"
    if sys.platform == "win32":
        # Under Windows native, use the user profile temp directory
        out_dir = os.path.join(os.environ.get("TEMP", "C:\\Temp"), "tokac_tests")
        
    os.makedirs(out_dir, exist_ok=True)
    exe_file = os.path.join(out_dir, safe_target + ".exe")
    log_file = os.path.join(out_dir, safe_target + ".log")
    
    errors = []
    
    # 1. Compilation Step
    if file_name in ["llvm_shim_test.tk", "llvm_backend_instructions.tk"]:
        tmp_obj = exe_file + ".o"
        comp_cmd = [TOKAC, "--emit-obj", test_path, "-o", tmp_obj]
        comp_res = subprocess.run(comp_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if comp_res.returncode != 0:
            err_msg = comp_res.stderr.decode("utf-8", errors="ignore")
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Compilation failed{NC}\n" + "\n".join("    | " + l for l in err_msg.splitlines()[-5:])
            
        link_cmd = [clangxx]
        if sysroot:
            link_cmd.extend(sysroot.split())
        link_cmd.extend([tmp_obj, "lib/sys/llvm_shim.o", "lib/sys/toka_rt.o"])
        if ldflags_libs:
            link_cmd.extend(ldflags_libs.split())
        if sys.platform in ["win32", "cygwin", "msys"]:
            link_cmd.append("-lws2_32")
        link_cmd.extend(["-o", exe_file])
        
        link_res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            os.remove(tmp_obj)
        except Exception:
            pass
            
        if link_res.returncode != 0:
            link_err = link_res.stderr.decode("utf-8", errors="ignore")
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Linking failed{NC}\n" + "\n".join("    | " + l for l in link_err.splitlines()[-5:])
            
    elif file_name == "odr_main.tk":
        lib_obj = os.path.join(out_dir, "tests_pass_odr_test_lib.o")
        helper_obj = os.path.join(out_dir, "tests_pass_odr_helper.o")
        
        # Compile lib
        comp_lib = [TOKAC, "-c", "tests/pass/odr_test_lib.tk_lib", "-o", lib_obj]
        res_lib = subprocess.run(comp_lib, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if res_lib.returncode != 0:
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Compiling odr_test_lib failed{NC}"
            
        # Compile helper
        comp_helper = [TOKAC, "-c", "tests/pass/odr_helper.tk_lib", "-o", helper_obj]
        res_helper = subprocess.run(comp_helper, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if res_helper.returncode != 0:
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Compiling odr_helper failed{NC}"
            
        # Compile main
        comp_main = [TOKAC, test_path, lib_obj, helper_obj, "-o", exe_file]
        res_main = subprocess.run(comp_main, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        
        # Cleanup intermediate ODR objects
        for obj in [lib_obj, helper_obj]:
            try:
                os.remove(obj)
            except Exception:
                pass
                
        if res_main.returncode != 0:
            err_msg = res_main.stderr.decode("utf-8", errors="ignore")
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Compilation failed{NC}\n" + "\n".join("    | " + l for l in err_msg.splitlines()[-5:])
    else:
        comp_cmd = [TOKAC, test_path, "-o", exe_file]
        comp_res = subprocess.run(comp_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if comp_res.returncode != 0:
            err_msg = comp_res.stderr.decode("utf-8", errors="ignore")
            return False, f"[{RED}FAIL{NC}] {file_name}\n    {RED}{test_path}:1: error: Compilation failed{NC}\n" + "\n".join("    | " + l for l in err_msg.splitlines()[-5:])

    # 2. Execution Step (with 30s timeout)
    try:
        run_res = subprocess.run([exe_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        exit_code = run_res.returncode
        stdout_content = run_res.stdout.decode("utf-8", errors="ignore")
        stderr_content = run_res.stderr.decode("utf-8", errors="ignore")
    except subprocess.TimeoutExpired:
        exit_code = -9
        stdout_content = ""
        stderr_content = "TIMEOUT"
    except Exception as e:
        exit_code = -1
        stdout_content = ""
        stderr_content = str(e)
        
    try:
        os.remove(exe_file)
    except Exception:
        pass
        
    combined_output = stdout_content + "\n" + stderr_content

    # 3. Verification Step (PANIC / EXPECT_PANIC checks)
    expected_panic_lines = []
    with open(test_path, "r", encoding="utf-8", errors="ignore") as f:
        for idx, line in enumerate(f, 1):
            if "// EXPECT_PANIC" in line:
                expected_panic_lines.append(idx)
                
    # Parse panic info from output
    actual_line = None
    actual_msg = None
    panic_match = re.search(r"runtime error: Panic with \"(.*?)\"", combined_output)
    if panic_match:
        actual_msg = panic_match.group(1)
        # Try to find crash line number in panic output
        line_match = re.search(r":(\d+)\s+runtime error:", combined_output)
        if line_match:
            actual_line = int(line_match.group(1))

    if exit_code == 0:
        for exp in expected_panic_lines:
            errors.append(f"{RED}{test_path}:{exp}: error: Expected panic did not trigger.{NC}")
    else:
        if actual_line is not None:
            # Check execution flow: did we pass any expected panic checks early?
            for exp in expected_panic_lines:
                if exp < actual_line:
                    errors.append(f"{RED}{test_path}:{exp}: error: Expected panic missed (passed check).{NC}")
            
            # Read crash line content
            with open(test_path, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            crashed_source = lines[actual_line - 1] if actual_line <= len(lines) else ""
            
            if "assert" in crashed_source:
                errors.append(f"{RED}{test_path}:{actual_line}: error: Assertion failed: \"{actual_msg}\"{NC}")
            elif "// EXPECT_PANIC" not in crashed_source:
                errors.append(f"{RED}{test_path}:{actual_line}: error: Unexpected panic: \"{actual_msg}\"{NC}")
        else:
            errors.append(f"{RED}{test_path}:1: error: Runtime crash ({exit_code}) without panic info.{NC}")

    # 4. Result Reporting
    if not errors:
        if exit_code != 0:
            desc = f"[{GREEN}PASS{NC} with Expected {YELLOW}Panic{NC}] {file_name}"
            log_dump = GRAY + "    --------------------------------------------------\n"
            log_dump += f"    | Exit Code: {exit_code}\n"
            panic_line = [l for l in combined_output.splitlines() if "runtime error: Panic with" in l]
            if panic_line:
                log_dump += f"    | {panic_line[0]}\n"
            else:
                log_dump += f"    | Exit Code: {exit_code} (No panic info found)\n"
                log_dump += "    | Last logs:\n" + "\n".join("    | " + l for l in combined_output.splitlines()[-3:]) + "\n"
            log_dump += "    --------------------------------------------------" + NC
            return True, desc + "\n" + log_dump
        else:
            return True, f"[{GREEN}PASS{NC}] {file_name}"
    else:
        fail_desc = f"[{RED}FAIL{NC}] {file_name}"
        for err in errors:
            fail_desc += f"\n    {err}"
        log_dump = GRAY + "\n    --------------------------------------------------\n"
        log_dump += f"    | Exit Code: {exit_code}\n"
        log_dump += "    | Last logs:\n" + "\n".join("    | " + l for l in combined_output.splitlines()[-3:]) + "\n"
        log_dump += "    --------------------------------------------------" + NC
        return False, fail_desc + log_dump

def main():
    # Orchestrator Build
    if os.path.exists("build") and os.path.exists("build/CMakeCache.txt") and shutil.which("cmake"):
        safe_print("Building Toka compiler using cmake...")
        subprocess.run(["cmake", "--build", "build", "--parallel", str(get_cores())], stdout=subprocess.DEVNULL)
        
    # Detect Compilers
    CLANGXX = find_compiler_tool("clang++", "clang++")
    CLANG = find_compiler_tool("clang", "clang")
    LLVM_CONFIG = find_compiler_tool("llvm-config", "llvm-config")
    
    # Retrieve LLVM compile/link options
    cxxflags, ldflags_libs = get_llvm_flags(LLVM_CONFIG)
    sysroot = get_sysroot_flags()
    
    # Prebuild native shims
    rebuild_runtime(CLANG, CLANGXX, sysroot, cxxflags)
    
    # Find all test cases
    test_cases = []
    tests_dir = "tests/pass"
    for root, _, files in os.walk(tests_dir):
        for f in files:
            if f.endswith(".tk"):
                test_cases.append(os.path.join(root, f).replace("\\", "/"))
                
    test_cases.sort()
    
    cores = get_cores()
    safe_print(f"Starting Toka 'PASS' Test Suite (Parallel: {cores})...")
    safe_print("---------------------------------")
    
    passed_count = 0
    failed_count = 0
    failures = []
    
    start_time = time.time()
    
    with ThreadPoolExecutor(max_workers=cores) as executor:
        futures = {executor.submit(run_single_test, tc, CLANGXX, sysroot, ldflags_libs): tc for tc in test_cases}
        for fut in as_completed(futures):
            test_path = futures[fut]
            try:
                success, output = fut.result()
                if success:
                    passed_count += 1
                else:
                    failed_count += 1
                    failures.append(output)
                safe_print(output)
            except Exception as e:
                failed_count += 1
                fail_err = f"[{RED}FAIL{NC}] {os.path.basename(test_path)}\n    Unhandled Runner Exception: {str(e)}"
                failures.append(fail_err)
                safe_print(fail_err)
                
    elapsed = time.time() - start_time
    safe_print("---------------------------------")
    
    if failed_count > 0:
        safe_print(RED + "!!! Concentrated Failure Summary !!!" + NC)
        safe_print("---------------------------------")
        for fail in failures:
            safe_print(fail)
        safe_print("---------------------------------")
        
    safe_print("Summary:")
    safe_print(f"  Passed: {GREEN}{passed_count}{NC}")
    safe_print(f"  Failed: {RED}{failed_count}{NC}")
    safe_print(f"  Time: {elapsed:.2f}s")
    
    if failed_count > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
