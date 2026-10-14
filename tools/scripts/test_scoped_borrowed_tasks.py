#!/usr/bin/env python3
"""Regression test gate for Scoped Borrowed Tasks (0.16 Stage 2 Gate B)."""

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
    res = run([str(tokac), "--check-only", str(source)], env=env, cwd=source.parent)
    require(res.returncode == 1, f"{source.name} returncode {res.returncode} != 1 in check-only:\n{res.stderr}")
    require(expected_code in res.stderr, f"{source.name} missing expected {expected_code} in stderr:\n{res.stderr}")

    # 2. Object file emission failure: returncode 1, target error code, zero .o artifact
    obj = tmp_dir / f"{source.stem}_fail.o"
    if obj.exists():
        obj.unlink()
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj)], env=env, cwd=source.parent)
    require(obj_res.returncode == 1, f"{source.name} -c returncode {obj_res.returncode} != 1:\n{obj_res.stderr}")
    require(not obj.exists(), f"{source.name} must not emit .o on error")
    require(expected_code in obj_res.stderr, f"{source.name} -c missing expected {expected_code} in stderr:\n{obj_res.stderr}")

    # 3. LLVM IR emission failure: returncode 1, target error code, zero .ll artifact
    ll = tmp_dir / f"{source.stem}_fail.ll"
    if ll.exists():
        ll.unlink()
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll)], env=env, cwd=source.parent)
    require(ll_res.returncode == 1, f"{source.name} --emit-llvm returncode {ll_res.returncode} != 1:\n{ll_res.stderr}")
    require(not ll.exists(), f"{source.name} must not emit .ll on error")
    require(expected_code in ll_res.stderr, f"{source.name} --emit-llvm missing expected {expected_code} in stderr:\n{ll_res.stderr}")

    print(f"  [PASS] Negative: {source.name} (rejected with {expected_code}, zero products)")


def check_positive(tokac, source, env, tmp_dir):
    require(source.exists(), f"missing test source: {source}")

    # 1. --check-only: returncode 0
    res = run([str(tokac), "--check-only", str(source)], env=env, cwd=source.parent)
    require(res.returncode == 0, f"failed to check {source.name}:\n{res.stderr}")

    # 2. Object file emission
    obj_file = tmp_dir / f"{source.stem}.o"
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj_file)], env=env, cwd=source.parent)
    if not obj_file.exists() and (tmp_dir / f"{source.stem}.obj").exists():
        obj_file = tmp_dir / f"{source.stem}.obj"
    require(obj_res.returncode == 0 and obj_file.exists() and obj_file.stat().st_size > 0,
            f"Object file emission failed for {source.name}:\n{obj_res.stderr}")

    # 3. LLVM IR emission
    ll_file = tmp_dir / f"{source.stem}.ll"
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll_file)], env=env, cwd=source.parent)
    require(ll_res.returncode == 0 and ll_file.exists() and ll_file.stat().st_size > 0,
            f"LLVM-IR emission failed for {source.name}:\n{ll_res.stderr}")

    # 4. Compile & execute executable
    suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
    exe = tmp_dir / f"{source.stem}{suffix}"
    compiled = run([str(tokac), str(source), "-o", str(exe)], env=env, cwd=source.parent)
    require(compiled.returncode == 0, f"failed to compile {source.name}:\n{compiled.stderr}")
    if not exe.exists() and (tmp_dir / f"{source.stem}.exe").exists():
        exe = tmp_dir / f"{source.stem}.exe"
    executed = run([str(exe)], env=env, cwd=source.parent)
    require(executed.returncode == 0, f"failed to execute {source.name} (code {executed.returncode}):\n{executed.stderr}")

    print(f"  [PASS] Positive: {source.name} (compiled and executed successfully)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="Path to compiler build directory")
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    tokac = build_dir / "bin/tokac"
    if not tokac.exists() and (build_dir / "bin/tokac.exe").exists():
        tokac = build_dir / "bin/tokac.exe"
    elif not tokac.exists() and (build_dir / "bin/Release/tokac.exe").exists():
        tokac = build_dir / "bin/Release/tokac.exe"
    require(tokac.exists(), f"missing compiler: {tokac}")

    toka_lib = os.environ.get("TOKA_LIB", str(ROOT / "lib"))
    env = dict(os.environ, TOKA_LIB=toka_lib)

    suite_dir = ROOT / "tests/semantics/scoped_borrowed_tasks"
    require(suite_dir.exists(), f"missing suite dir: {suite_dir}")

    negatives = [
        ("n1_return_escape.tk", "E0455"),
        ("n2_detached_start.tk", "E04583"),
        ("n3_container_escape.tk", "E0456"),
        ("n4_parent_mutation_conflict.tk", "E0440"),
        ("n5a_unjoined_scope_exit.tk", "E0456"),
        ("n5b_borrowed_child_result.tk", "E04658"),
        ("level_a_cross_module_escape_negative.tk", "E0455"),
        ("bodyless_consumer_negative.tk", "E0456"),
        ("adv1_outer_scope_inner_buf1_wiped_by_buf2.tk", "E0456"),
        ("adv2_single_task_two_buffers_move_first.tk", "E0440"),
        ("adv3_unawaited_scope_close_bypass.tk", "E0456"),
        ("adv4_sync_drop_scope_helper_bypass.tk", "E0456"),
        ("adv5_c3_cancel_at_ordinary_await_unjoined_unwind.tk", "E0456"),
    ]

    positives = [
        "p1_c1_result_and_normal_order.tk",
        "c2_early_exit_cleanup_order.tk",
        "c3_parent_cancel_cleanup_order.tk",
        "level_a_retained_body_consumer.tk",
    ]

    print(f"Running Scoped Borrowed Tasks test suite using {tokac}...")
    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_dir = pathlib.Path(tmp_str)

        print("\nChecking negative probes (escape prevention & zero artifact guarantee):")
        for filename, expected_code in negatives:
            check_negative(tokac, suite_dir / filename, env, tmp_dir, expected_code)

        print("\nChecking positive probes (safe scoped API & convergence patterns):")
        for filename in positives:
            check_positive(tokac, suite_dir / filename, env, tmp_dir)

    print("\nAll Scoped Borrowed Tasks tests passed successfully.")


if __name__ == "__main__":
    main()
