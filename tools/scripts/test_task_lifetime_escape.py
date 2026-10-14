#!/usr/bin/env python3
"""Regression test gate for TaskHandle lifetime escape prevention (THFIX95-R1, THFIX95-R2, THFIX95-R3)."""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command, env=None, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        print(f"[FAIL] {message}", file=sys.stderr)
        raise RuntimeError(message)


def check_negative(tokac, source, env, tmp_dir, expected_code):
    require(source.exists(), f"missing test source: {source}")

    # 1. --check-only: returncode 1, error code in stderr
    res = run([str(tokac), "--check-only", str(source)], env=env)
    require(res.returncode == 1, f"{source.name} returncode {res.returncode} != 1 in check-only:\n{res.stderr}")
    require(expected_code in res.stderr, f"{source.name} missing expected {expected_code} in stderr:\n{res.stderr}")

    # 2. Object file emission failure: returncode 1, target error code, zero .o artifact
    obj = tmp_dir / f"{source.stem}_fail.o"
    if obj.exists():
        obj.unlink()
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj)], env=env)
    require(obj_res.returncode == 1, f"{source.name} -c returncode {obj_res.returncode} != 1:\n{obj_res.stderr}")
    require(not obj.exists(), f"{source.name} must not emit .o on error")
    require(expected_code in obj_res.stderr, f"{source.name} -c missing expected {expected_code} in stderr:\n{obj_res.stderr}")

    # 3. LLVM IR emission failure: returncode 1, target error code, zero .ll artifact
    ll = tmp_dir / f"{source.stem}_fail.ll"
    if ll.exists():
        ll.unlink()
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll)], env=env)
    require(ll_res.returncode == 1, f"{source.name} --emit-llvm returncode {ll_res.returncode} != 1:\n{ll_res.stderr}")
    require(not ll.exists(), f"{source.name} must not emit .ll on error")
    require(expected_code in ll_res.stderr, f"{source.name} --emit-llvm missing expected {expected_code} in stderr:\n{ll_res.stderr}")

    print(f"  [PASS] Negative: {source.name} (rejected with {expected_code}, zero products)")


def check_positive(tokac, source, env, tmp_dir):
    require(source.exists(), f"missing test source: {source}")

    # 1. --check-only: returncode 0
    res = run([str(tokac), "--check-only", str(source)], env=env)
    require(res.returncode == 0, f"failed to check {source.name}:\n{res.stderr}")

    # 2. Object file emission
    obj_file = tmp_dir / f"{source.stem}.o"
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj_file)], env=env)
    require(obj_res.returncode == 0 and obj_file.exists() and obj_file.stat().st_size > 0,
            f"Object file emission failed for {source.name}:\n{obj_res.stderr}")

    # 3. LLVM IR emission
    ll_file = tmp_dir / f"{source.stem}.ll"
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll_file)], env=env)
    require(ll_res.returncode == 0 and ll_file.exists() and ll_file.stat().st_size > 0,
            f"LLVM-IR emission failed for {source.name}:\n{ll_res.stderr}")

    # 4. Compile & execute executable
    exe = tmp_dir / source.stem
    compiled = run([str(tokac), str(source), "-o", str(exe)], env=env)
    require(compiled.returncode == 0, f"failed to compile {source.name}:\n{compiled.stderr}")
    executed = run([str(exe)], env=env)
    require(executed.returncode == 0, f"failed to execute {source.name} (code {executed.returncode}):\n{executed.stderr}")

    print(f"  [PASS] Positive: {source.name} (compiled and executed successfully)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="Path to compiler build directory")
    args = parser.parse_args()

    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    toka_lib = os.environ.get("TOKA_LIB", str(ROOT / "lib"))
    env = dict(os.environ, TOKA_LIB=toka_lib)

    suite_dir = ROOT / "tests/semantics/task_lifetime_escape"
    require(suite_dir.exists(), f"missing suite dir: {suite_dir}")

    negatives = [
        ("borrowed_detach_forget.tk", "E0456"),
        ("borrowed_detach_wrapper.tk", "E0456"),
        ("borrowed_detach_if.tk", "E0456"),
        ("borrowed_detach_loop.tk", "E0456"),
        ("borrowed_start_wrapper.tk", "E0456"),
        ("borrowed_record_argument.tk", "E0456"),
        ("borrowed_record_binding.tk", "E0456"),
        ("escape_option_container.tk", "E0456"),
        ("escape_record_container.tk", "E0456"),
        ("escape_return_container.tk", "E0455"),
        ("n3_reject_container_escape.tk", "E0456"),
    ]

    positives = [
        "safe_drop_consumer.tk",
        "legal_record_argument.tk",
        "legal_option_argument.tk",
        "p2_owned_task_in_vec.tk",
        "p3_owned_task_in_scope.tk",
        "r3_task_explicit_dep_safe_await.tk",
    ]

    print(f"Running TaskHandle lifetime escape gate tests using {tokac}...")
    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_dir = pathlib.Path(tmp_str)

        print("\nChecking negative probes (escape prevention & zero artifact guarantee):")
        for filename, expected_code in negatives:
            check_negative(tokac, suite_dir / filename, env, tmp_dir, expected_code)

        print("\nChecking positive probes (safe drop, legal containers & await patterns):")
        for filename in positives:
            check_positive(tokac, suite_dir / filename, env, tmp_dir)

    print("\nAll TaskHandle lifetime escape tests passed successfully.")


if __name__ == "__main__":
    main()
