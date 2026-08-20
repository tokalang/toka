#!/usr/bin/env python3
import json
import os
import sys
import subprocess

def main():
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    manifest_path = os.path.join(root_dir, "tests", "conformance", "manifest.json")
    tokac_bin = os.path.join(root_dir, "build", "bin", "tokac")
    source_lib_dir = os.path.join(root_dir, "lib")
    build_lib_dir = os.path.join(root_dir, "build", "lib")
    runtime_object = os.path.join(build_lib_dir, "sys", "toka_rt.o")
    tmp_dir = os.path.join(root_dir, "tmp")
    os.makedirs(tmp_dir, exist_ok=True)

    if not os.path.exists(manifest_path):
        print(f"[ERROR] Manifest not found at {manifest_path}")
        sys.exit(1)

    if not os.path.exists(tokac_bin):
        print(f"[ERROR] Compiler binary not found at {tokac_bin}. Build tokac first.")
        sys.exit(1)

    if not os.path.exists(runtime_object):
        print(f"[ERROR] Core runtime object not found at {runtime_object}. Build the toka_rt target first.")
        sys.exit(1)

    # tokac intentionally links the host runtime from lib/sys/ when invoked
    # from a source checkout.  Rebuild that ignored object here so this entry
    # point is independently reproducible on every host instead of relying on
    # an earlier PASS or release-gate stage.
    prepare_runtime = os.path.join(root_dir, "tools", "scripts", "test_pass.py")
    prepare_res = subprocess.run(
        [sys.executable, prepare_runtime, "--prepare-runtime-only"],
        cwd=root_dir,
    )
    if prepare_res.returncode != 0:
        print("[ERROR] Failed to prepare native runtime objects for conformance.")
        sys.exit(prepare_res.returncode)

    # A source checkout keeps Toka modules in lib/, while CMake writes the
    # host runtime object to build/lib/.  Use both locations so conformance
    # runs against a clean build rather than an ignored local lib/sys/*.o.
    tool_env = os.environ.copy()
    lib_paths = [source_lib_dir, build_lib_dir]
    inherited_lib = tool_env.get("TOKA_LIB")
    if inherited_lib:
        lib_paths.append(inherited_lib)
    tool_env["TOKA_LIB"] = os.pathsep.join(lib_paths)

    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    suites = manifest.get("suites", [])
    print(f"--- Running Toka 1.0 Conformance Test Suite ({len(suites)} tests) ---")

    passed_count = 0
    failed_count = 0

    for item in suites:
        test_id = item["id"]
        rel_path = item["path"]
        test_type = item["type"]
        timeout_sec = item.get("timeout_seconds", 10)
        test_full_path = os.path.join(root_dir, "tests", "conformance", rel_path)

        if not os.path.exists(test_full_path):
            print(f"[FAILED] [{test_id}] Source file missing: {test_full_path}")
            failed_count += 1
            continue

        out_bin = os.path.join(tmp_dir, f"conf_{test_id}.exe")
        out_ll = os.path.join(tmp_dir, f"conf_{test_id}.ll")

        try:
            if test_type == "ir-verify":
                cmd = [tokac_bin, "--emit-llvm", test_full_path, "-o", out_ll]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout_sec, env=tool_env, cwd=root_dir)
                if res.returncode != 0:
                    print(f"[FAILED] [{test_id}] LLVM IR generation failed unexpectedly:")
                    print(res.stdout + res.stderr)
                    failed_count += 1
                    continue

                if not os.path.exists(out_ll):
                    print(f"[FAILED] [{test_id}] LLVM IR output file missing: {out_ll}")
                    failed_count += 1
                    continue

                with open(out_ll, "r", encoding="utf-8", errors="ignore") as f_ll:
                    ir_content = f_ll.read()

                exp_pattern = item.get("expected_ir_pattern")
                if exp_pattern and exp_pattern not in ir_content:
                    print(f"[FAILED] [{test_id}] Expected IR pattern '{exp_pattern}' not found in emitted LLVM IR.")
                    failed_count += 1
                    continue

                exp_pattern_count = item.get("expected_ir_pattern_count")
                if exp_pattern_count is not None:
                    actual_pattern_count = ir_content.count(exp_pattern)
                    if actual_pattern_count != exp_pattern_count:
                        print(
                            f"[FAILED] [{test_id}] Expected IR pattern "
                            f"'{exp_pattern}' exactly {exp_pattern_count} time(s), "
                            f"found {actual_pattern_count}."
                        )
                        failed_count += 1
                        continue

                missing_patterns = [
                    pattern for pattern in item.get("expected_ir_patterns", [])
                    if pattern not in ir_content
                ]
                if missing_patterns:
                    print(f"[FAILED] [{test_id}] Expected IR patterns not found: {missing_patterns}")
                    failed_count += 1
                    continue

                print(f"[PASSED] [{test_id}] LLVM IR pattern '{exp_pattern}' verified cleanly.")
                passed_count += 1

            elif test_type == "compile-fail":
                cmd = [tokac_bin, test_full_path, "-o", out_bin]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout_sec, env=tool_env, cwd=root_dir)
                if res.returncode == 0:
                    print(f"[FAILED] [{test_id}] Expected compilation failure but succeeded.")
                    failed_count += 1
                    continue
                
                output = res.stdout + res.stderr
                exp_code = item.get("expected_diagnostic_code")
                if exp_code and exp_code not in output:
                    print(f"[FAILED] [{test_id}] Expected error code '{exp_code}' not found in output.")
                    failed_count += 1
                    continue

                exp_line = item.get("expected_span_line")
                if exp_line is not None:
                    expected_span = f":{exp_line}:"
                    if expected_span not in output:
                        print(f"[FAILED] [{test_id}] Expected error span line '{exp_line}' not found in output.")
                        failed_count += 1
                        continue

                print(f"[PASSED] [{test_id}] Compile-fail verified cleanly with code '{exp_code}'.")
                passed_count += 1

            elif test_type in ("run", "run-fail", "compile-pass"):
                cmd = [tokac_bin, test_full_path, "-o", out_bin]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout_sec, env=tool_env, cwd=root_dir)
                if res.returncode != 0:
                    print(f"[FAILED] [{test_id}] Compilation failed unexpectedly:")
                    print(res.stdout + res.stderr)
                    failed_count += 1
                    continue

                if test_type == "compile-pass":
                    print(f"[PASSED] [{test_id}] Compile-pass succeeded.")
                    passed_count += 1
                    continue

                # Execute binary with timeout
                run_res = subprocess.run([out_bin], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout_sec, env=tool_env, cwd=root_dir)
                if test_type == "run-fail":
                    if run_res.returncode == 0:
                        print(f"[FAILED] [{test_id}] Expected fail-closed runtime termination but execution succeeded.")
                        failed_count += 1
                        continue
                    print(f"[PASSED] [{test_id}] Fail-closed runtime termination verified (exit code {run_res.returncode}).")
                    passed_count += 1
                    continue
                exp_exit = item.get("expected_exit_code", 0)
                if run_res.returncode != exp_exit:
                    print(f"[FAILED] [{test_id}] Execution exit code mismatch: got {run_res.returncode}, expected {exp_exit}")
                    print(run_res.stdout + run_res.stderr)
                    failed_count += 1
                    continue

                print(f"[PASSED] [{test_id}] Run verified cleanly (exit code {run_res.returncode}).")
                passed_count += 1

        except subprocess.TimeoutExpired:
            print(f"[FAILED] [{test_id}] Execution timed out after {timeout_sec}s.")
            failed_count += 1
        finally:
            for clean_path in (out_bin, out_ll):
                if os.path.exists(clean_path):
                    try:
                        os.remove(clean_path)
                    except Exception:
                        pass

    print(f"\n--- Conformance Suite Results: {passed_count} Passed, {failed_count} Failed ---")
    if failed_count > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
