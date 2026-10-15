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
        ("forged_join.tk", "E0456"),
        ("forged_cancel_join.tk", "E0456"),
        ("renamed_noop_control.tk", "E0456"),
        ("forged_method_drain.tk", "E0456"),
        ("cross_module_forged_join.tk", "E0456"),
        ("r2_d1_forged_spawn_scoped_routes_to_return.tk", "E0456"),
        ("r2_d2a_mutable_sibling_launder.tk", "E0456"),
        ("r2_d2b_forged_spawn_scoped_sibling.tk", "E0456"),
        ("r2_d3_await_question_on_join_scoped.tk", "E0456"),
        ("adv6_user_impl_taskscope_method.tk", "E0456"),
        ("r3_d1_spawn_into_err_launder_negative.tk", "E0456"),
    ]

    positives = [
        "p1_c1_result_and_normal_order.tk",
        "c2_early_exit_cleanup_order.tk",
        "c3_parent_cancel_cleanup_order.tk",
        "level_a_retained_body_consumer.tk",
        "r3_d2_legit_cold_forward_positive.tk",
        "r3_d3a_pre_drain_cancel_order_positive.tk",
        "r3_d3b_mid_drain_second_cancel_order_positive.tk",
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

        # RFC §7 Level-A / REL016-R3: Source-Hidden Retained-Body TKI verification
        check_source_hidden_retained_body_tki(tokac, suite_dir, env, tmp_dir)

    print("\nAll Scoped Borrowed Tasks tests passed successfully.")


def check_source_hidden_retained_body_tki(tokac, suite_dir, env, tmp_dir):
    """
    RFC §7 Level-A / REL016-R3: Verify cross-module retained-body consumption and escape
    under strict source-hidden conditions (scoped_provider.tk removed/absent).
    """
    print("\nChecking Source-Hidden Retained-Body TKI (RFC §7 Level-A):")
    sh_dir = tmp_dir / "source_hidden_retained_body"
    sh_dir.mkdir(parents=True, exist_ok=True)

    # 1. Compile scoped_provider.tk to scoped_provider.o and scoped_provider.tki in sh_dir
    provider_src = suite_dir / "scoped_provider.tk"
    provider_obj = sh_dir / "scoped_provider.o"
    res = run([str(tokac), "-c", str(provider_src), "--emit-interface", "-o", str(provider_obj)], env=env, cwd=sh_dir)
    require(res.returncode == 0, f"failed to compile provider to .tki + .o:\n{res.stderr}")
    provider_tki = sh_dir / "scoped_provider.tki"
    require(provider_obj.exists() and provider_obj.stat().st_size > 0, "scoped_provider.o missing")
    require(provider_tki.exists() and provider_tki.stat().st_size > 0, "scoped_provider.tki missing")

    # Verify provider .tk source is NOT present in sh_dir
    require(not (sh_dir / "scoped_provider.tk").exists(), "provider source must not exist in consumer dir")

    # 2. Positive: consumer compiles against only .tki + .o, links and executes with exit 0
    consumer_src = suite_dir / "level_a_retained_body_consumer.tk"
    consumer_copy = sh_dir / "consumer.tk"
    consumer_copy.write_text(consumer_src.read_text())

    suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
    consumer_exe = sh_dir / f"consumer_exe{suffix}"
    compile_res = run([str(tokac), str(consumer_copy), str(provider_obj), "-o", str(consumer_exe)], env=env, cwd=sh_dir)
    require(compile_res.returncode == 0, f"failed to compile consumer against .tki + .o:\n{compile_res.stderr}")
    run_res = run([str(consumer_exe)], env=env, cwd=sh_dir)
    require(run_res.returncode == 0, f"consumer execution failed (exit {run_res.returncode}):\n{run_res.stderr}")
    print("  [PASS] Positive: source-hidden consumer compiled against .tki + .o and executed successfully (exit 0)")

    # 3. Negative: escape probe against only .tki must be rejected in 3 modes with E0455 and zero products
    escape_src = suite_dir / "level_a_cross_module_escape_negative.tk"
    escape_copy = sh_dir / "escape_neg.tk"
    escape_copy.write_text(escape_src.read_text())

    # Check-only
    chk = run([str(tokac), "--check-only", str(escape_copy)], env=env, cwd=sh_dir)
    require(chk.returncode == 1, f"source-hidden escape must fail check-only:\n{chk.stderr}")
    require("E0455" in chk.stderr, f"source-hidden escape check-only missing E0455:\n{chk.stderr}")

    # -c emission
    esc_obj = sh_dir / "escape_neg_fail.o"
    c_res = run([str(tokac), "-c", str(escape_copy), "-o", str(esc_obj)], env=env, cwd=sh_dir)
    require(c_res.returncode == 1, f"source-hidden escape must fail -c:\n{c_res.stderr}")
    require(not esc_obj.exists(), "source-hidden escape must not emit .o on error")
    require("E0455" in c_res.stderr, f"source-hidden escape -c missing E0455:\n{c_res.stderr}")

    # --emit-llvm emission
    esc_ll = sh_dir / "escape_neg_fail.ll"
    ll_res = run([str(tokac), "--emit-llvm", str(escape_copy), "-o", str(esc_ll)], env=env, cwd=sh_dir)
    require(ll_res.returncode == 1, f"source-hidden escape must fail --emit-llvm:\n{ll_res.stderr}")
    require(not esc_ll.exists(), "source-hidden escape must not emit .ll on error")
    require("E0455" in ll_res.stderr, f"source-hidden escape --emit-llvm missing E0455:\n{ll_res.stderr}")

    print("  [PASS] Negative: source-hidden escape rejected with E0455 across 3 modes with zero products")


if __name__ == "__main__":
    main()
