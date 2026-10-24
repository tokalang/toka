#!/usr/bin/env python3
"""Regression test gate for Compositional Convergence (0.17 Stage 2 Gate).

Verifies fail-closed escape prevention (negative probes & bodyless TKI) across
3 compiler modes (--check-only, -c, --emit-llvm) with zero artifact emission,
and legitimate task carrier compositions (positive probes) across all 3 modes
plus direct native execution verifying rc == 0.
"""

import argparse
import datetime
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command, env=None, cwd=ROOT):
    t0 = time.monotonic()
    res = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)
    dt_ms = round((time.monotonic() - t0) * 1000, 2)
    return res, dt_ms


def require(condition, message):
    if not condition:
        print(f"[FAIL] {message}", file=sys.stderr)
        raise RuntimeError(message)


def check_negative_probe(tokac, source, modules_dir, env, tmp_dir, expected_code):
    require(source.exists(), f"missing test source: {source}")
    probe_receipt = {
        "source": str(source.name),
        "expected_code": expected_code,
        "modes": {}
    }

    # 1. --check-only: returncode 1, error code in stderr
    chk_cmd = [str(tokac), "-I", str(modules_dir), "--check-only", str(source)]
    res, dt_ms = run(chk_cmd, env=env, cwd=source.parent)
    require(res.returncode == 1, f"{source.name} returncode {res.returncode} != 1 in check-only:\n{res.stderr}")
    require(expected_code in res.stderr, f"{source.name} missing expected {expected_code} in check-only stderr:\n{res.stderr}")
    probe_receipt["modes"]["--check-only"] = {
        "command": chk_cmd,
        "returncode": res.returncode,
        "duration_ms": dt_ms,
        "matched_error": expected_code,
        "stderr_head": res.stderr[:300]
    }

    # 2. Object file emission failure: returncode 1, target error code, zero .o/.obj artifact
    obj = tmp_dir / f"{source.stem}_fail.o"
    if obj.exists():
        obj.unlink()
    c_cmd = [str(tokac), "-I", str(modules_dir), "-c", str(source), "-o", str(obj)]
    c_res, dt_ms = run(c_cmd, env=env, cwd=source.parent)
    require(c_res.returncode == 1, f"{source.name} -c returncode {c_res.returncode} != 1:\n{c_res.stderr}")
    require(not obj.exists(), f"{source.name} must not emit .o on error")
    if (tmp_dir / f"{source.stem}_fail.obj").exists():
        require(False, f"{source.name} must not emit .obj on error")
    require(expected_code in c_res.stderr, f"{source.name} -c missing expected {expected_code} in stderr:\n{c_res.stderr}")
    probe_receipt["modes"]["-c"] = {
        "command": c_cmd,
        "returncode": c_res.returncode,
        "duration_ms": dt_ms,
        "matched_error": expected_code,
        "artifact_emitted": False,
        "stderr_head": c_res.stderr[:300]
    }

    # 3. LLVM IR emission failure: returncode 1, target error code, zero .ll artifact
    ll = tmp_dir / f"{source.stem}_fail.ll"
    if ll.exists():
        ll.unlink()
    ll_cmd = [str(tokac), "-I", str(modules_dir), "--emit-llvm", str(source), "-o", str(ll)]
    ll_res, dt_ms = run(ll_cmd, env=env, cwd=source.parent)
    require(ll_res.returncode == 1, f"{source.name} --emit-llvm returncode {ll_res.returncode} != 1:\n{ll_res.stderr}")
    require(not ll.exists(), f"{source.name} must not emit .ll on error")
    require(expected_code in ll_res.stderr, f"{source.name} --emit-llvm missing expected {expected_code} in stderr:\n{ll_res.stderr}")
    probe_receipt["modes"]["--emit-llvm"] = {
        "command": ll_cmd,
        "returncode": ll_res.returncode,
        "duration_ms": dt_ms,
        "matched_error": expected_code,
        "artifact_emitted": False,
        "stderr_head": ll_res.stderr[:300]
    }

    print(f"  [PASS] Negative: {source.name} (rejected with {expected_code}, zero products)")
    return probe_receipt


def check_positive_probe(tokac, source, modules_dir, env, tmp_dir):
    require(source.exists(), f"missing test source: {source}")
    probe_receipt = {
        "source": str(source.name),
        "modes": {},
        "execution": {}
    }

    # 1. --check-only: returncode 0
    chk_cmd = [str(tokac), "-I", str(modules_dir), "--check-only", str(source)]
    res, dt_ms = run(chk_cmd, env=env, cwd=source.parent)
    require(res.returncode == 0, f"failed to check {source.name}:\n{res.stderr}")
    probe_receipt["modes"]["--check-only"] = {
        "command": chk_cmd,
        "returncode": res.returncode,
        "duration_ms": dt_ms
    }

    # 2. Object file emission: returncode 0, file exists and > 0 bytes
    obj_file = tmp_dir / f"{source.stem}.o"
    if obj_file.exists():
        obj_file.unlink()
    c_cmd = [str(tokac), "-I", str(modules_dir), "-c", str(source), "-o", str(obj_file)]
    obj_res, dt_ms = run(c_cmd, env=env, cwd=source.parent)
    if not obj_file.exists() and (tmp_dir / f"{source.stem}.obj").exists():
        obj_file = tmp_dir / f"{source.stem}.obj"
    require(obj_res.returncode == 0 and obj_file.exists() and obj_file.stat().st_size > 0,
            f"Object file emission failed for {source.name}:\n{obj_res.stderr}")
    probe_receipt["modes"]["-c"] = {
        "command": c_cmd,
        "returncode": obj_res.returncode,
        "duration_ms": dt_ms,
        "artifact_size": obj_file.stat().st_size
    }

    # 3. LLVM IR emission: returncode 0, file exists and > 0 bytes
    ll_file = tmp_dir / f"{source.stem}.ll"
    if ll_file.exists():
        ll_file.unlink()
    ll_cmd = [str(tokac), "-I", str(modules_dir), "--emit-llvm", str(source), "-o", str(ll_file)]
    ll_res, dt_ms = run(ll_cmd, env=env, cwd=source.parent)
    require(ll_res.returncode == 0 and ll_file.exists() and ll_file.stat().st_size > 0,
            f"LLVM-IR emission failed for {source.name}:\n{ll_res.stderr}")
    probe_receipt["modes"]["--emit-llvm"] = {
        "command": ll_cmd,
        "returncode": ll_res.returncode,
        "duration_ms": dt_ms,
        "artifact_size": ll_file.stat().st_size
    }

    # 4. Compile & execute standalone binary: returncode 0
    suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
    exe = tmp_dir / f"{source.stem}{suffix}"
    if exe.exists():
        exe.unlink()
    build_cmd = [str(tokac), "-I", str(modules_dir), str(source), "-o", str(exe)]
    compiled, build_ms = run(build_cmd, env=env, cwd=source.parent)
    require(compiled.returncode == 0, f"failed to compile executable for {source.name}:\n{compiled.stderr}")
    if not exe.exists() and (tmp_dir / f"{source.stem}.exe").exists():
        exe = tmp_dir / f"{source.stem}.exe"
    require(exe.exists(), f"missing compiled executable for {source.name}")

    executed, run_ms = run([str(exe)], env=env, cwd=source.parent)
    require(executed.returncode == 0, f"failed to execute {source.name} (exit code {executed.returncode}):\n{executed.stderr}")
    probe_receipt["execution"] = {
        "compile_command": build_cmd,
        "compile_duration_ms": build_ms,
        "exe_path": str(exe),
        "returncode": executed.returncode,
        "run_duration_ms": run_ms,
        "stdout": executed.stdout,
        "stderr": executed.stderr
    }

    print(f"  [PASS] Positive: {source.name} (compiled and executed successfully with exit code 0)")
    return probe_receipt


def check_portable_ext_provider(tokac, modules_dir, env, tmp_dir):
    print("\nChecking cross-platform portability of ext_provider.tki (zero host path/triple dependency):")
    foreign_dir = tmp_dir / "portable_ext_verification"
    foreign_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(modules_dir / "ext_provider.tki", foreign_dir / "ext_provider.tki")

    consumer_tk = foreign_dir / "consumer.tk"
    consumer_tk.write_text('''
import ext_provider::{fwd_external}
import core/task::{TaskHandle}
import std/task::{block_on}
fn dummy() -> async i32 { return 42:i32 }
pub fn main() -> i32 {
    auto d = dummy()
    auto t1 = fwd_external(cede d)
    t1.start
    auto res = block_on<i32>(t1)
    if res != 42:i32 { return 1:i32 }
    return 0:i32
}
''')
    suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
    exe = foreign_dir / f"consumer_exe{suffix}"
    comp_res, comp_ms = run([str(tokac), "-I", str(foreign_dir), str(consumer_tk), "-o", str(exe)],
                            env=env, cwd=foreign_dir)
    require(comp_res.returncode == 0, f"Portable compilation failed in foreign dir:\n{comp_res.stderr}")
    run_res, run_ms = run([str(exe)], env=env, cwd=foreign_dir)
    require(run_res.returncode == 0, f"Portable execution failed in foreign dir (exit {run_res.returncode}):\n{run_res.stderr}")
    print("  [PASS] Portability: ext_provider.tki compiled and executed successfully in isolated foreign directory (exit 0)")
    return {
        "status": "PASS",
        "compile_ms": comp_ms,
        "run_ms": run_ms,
        "foreign_dir": str(foreign_dir)
    }



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="Path to compiler build directory")
    parser.add_argument("--suite-dir", default=None, help="Optional suite directory for compositional convergence")
    parser.add_argument("--output-receipt", default=None, help="Optional output path for JSON execution receipt")
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    tokac = build_dir / "bin/tokac"
    if not tokac.exists() and (build_dir / "bin/tokac.exe").exists():
        tokac = build_dir / "bin/tokac.exe"
    elif not tokac.exists() and (build_dir / "bin/Release/tokac.exe").exists():
        tokac = build_dir / "bin/Release/tokac.exe"
    require(tokac.exists(), f"missing compiler: {tokac}")

    toka_lib = os.environ.get("TOKA_LIB")
    if not toka_lib:
        if (build_dir / "lib").is_dir():
            toka_lib = str(build_dir / "lib")
        else:
            toka_lib = str(ROOT / "lib")
    env = dict(os.environ, TOKA_LIB=toka_lib)

    suite_dir = None
    if args.suite_dir:
        cand = pathlib.Path(args.suite_dir).resolve()
        if cand.is_dir():
            suite_dir = cand
    if suite_dir is None:
        for cand in [
            pathlib.Path(__file__).resolve().parent / "r018_fixtures",
            ROOT / "tests/semantics/compositional_convergence",
        ]:
            if cand.is_dir() and (cand / "modules").is_dir():
                suite_dir = cand
                break
    require(suite_dir is not None and suite_dir.exists(), f"missing suite dir: {suite_dir}")
    modules_dir = suite_dir / "modules"
    require(modules_dir.exists(), f"missing modules dir: {modules_dir}")

    negatives = [
        ("leak1_d3_m2_split_var.tk", "E04584"),
        ("leak2_d3_m3_unboxed_field.tk", "E04584"),
        ("leak3_d6_m2_wrapper_pass_buf.tk", "E0456"),
        ("leak4_d3_m4_fwd_external_start.tk", "E04584"),
        ("leak5_d6_m3_inline_proj.tk", "E0456"),
        ("leak6_d4_m3_field_escape.tk", "E0456"),
        ("leak7_d5_m3_dual_scope.tk", "E0456"),
        ("leak8_cast_cede_leak3_outer_whole_reassign.tk", "E0456"),
        ("leak9_dual_carrier_method_spawn_s1_join_s2.tk", "E0456"),
        ("leak10_method_receiver_start_bypass_then_join_empty_s1_split_var.tk", "E0456"),
        ("leak11_mixed_carrier_wrapper_fn.tk", "E0456"),
        ("r2_p0_1a_nested_carrier_split_var_start.tk", "E04584"),
        ("r2_p0_1b_nested_carrier_inline_start.tk", "E04584"),
        ("r2_p0_1c_nested_carrier_two_step_var_start.tk", "E04584"),
        ("r2_p0_2a_effects_self_taskscope_start_bypass.tk", "E0456"),
        ("r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass.tk", "E0456"),
        ("r3_p0_1a_holder_method_set_task_then_start.tk", "E04584"),
        ("r3_p0_1b_holder_reassign_via_fwd_call_then_start.tk", "E04584"),
        ("r3_p0_2a_effects_self_indirect_helper_start.tk", "E0456"),
        ("r4_p0_1_nested_field_assign_then_start.tk", "E04584"),
        ("r4_p0_2a_effects_self_match_start.tk", "E0456"),
        ("r4_p0_2b_effects_self_bodyless_tki_call.tk", "E0456"),
        ("r5_neg1_borrowed_self_field_postfix_start.tk", "E04584"),
        ("r5_neg2_cede_self_carrier_with_borrowed_task_start.tk", "E04584"),
        ("v0171_neg1_cede_self_borrowed_carrier_return.tk", "E0455"),
        ("v0171_neg2_cede_arg_borrowed_carrier_return.tk", "E0455"),
        ("v0171_neg3_inline_ctor_borrowed_carrier_return.tk", "E0455"),
        ("v0171_neg4_ref_carrier_multi_field_return.tk", "E0455"),
        ("v0171_neg5_cede_self_ref_carrier_start.tk", "E04584"),
        ("v0171_neg6_explicit_dep_clause_with_ceded_borrow.tk", "E0455"),
        ("v0171_neg7_cede_self_sync_detach_inner.tk", "E0456"),
        ("v018_s1_neg1_forged_block_on_start.tk", "E0456"),
        ("v018_s1_neg2_untrusted_cede_self_impl_detach.tk", "E0456"),
        ("v018_s1_neg3_generic_async_close_return_borrowed.tk", "E0455"),
        ("v018_s1_neg4_generic_async_close_start_borrowed.tk", "E04584"),
        ("v018_s1_neg5_ref_carrier_generic_close_return.tk", "E0455"),
        ("v018_s1_neg6_ref_carrier_generic_close_start.tk", "E04584"),
        ("v018_s1_neg7_unrelaxed_cede_self_mut.tk", "E0618"),
        ("v018_s1_neg8_mismatch_trait_cede_self_impl_init.tk", "E0428"),
    ]

    bodyless_negatives = [
        ("neg1_bodyless_consumer.tk", "E0456"),
        ("neg2_bodyless_fwd.tk", "E0456"),
        ("neg3_bodyless_scoped.tk", "E0456"),
    ]

    positives = [
        "pos1_d1_m3_carrier_consume.tk",
        "pos2_d2_m3_carrier_fwd.tk",
        "pos3_d5_m2_split_wrapper.tk",
        "pos4_d1_m4_retained_consume.tk",
        "pos5_d2_m4_retained_fwd.tk",
        "pos6_d5_m4_retained_scoped.tk",
        "pos7_owned_task_start.tk",
        "pos8_dual_scope_drain_both.tk",
        "pos9_owned_carrier_split_var_start.tk",
        "pos10_owned_fwd_external_start.tk",
        "r2_p1_1a_two_tasks_inline_owned_start_pos.tk",
        "r2_p1_1b_two_tasks_split_var_owned_start_pos.tk",
        "r3_p1_1a_two_tasks_fwd_call_owned_start_pos.tk",
        "r5_pos1_cede_self_receiver_start.tk",
        "v0171_pos1_cede_param_drop_matrix.tk",
        "v0171_pos2_owned_and_local_await_carriers.tk",
        "v018_s1_pos1_async_close_7_lifecycle_paths.tk",
        "v018_s1_pos2_borrowed_and_ref_carrier_local_await_and_generic.tk",
    ]

    receipt = {
        "schema": "toka.compositional-convergence-receipt.v1",
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
            "python_version": platform.python_version()
        },
        "compiler": str(tokac),
        "counts": {
            "negative_probes": len(negatives),
            "bodyless_negative_probes": len(bodyless_negatives),
            "positive_probes": len(positives),
            "total_probes": len(negatives) + len(bodyless_negatives) + len(positives)
        },
        "results": {
            "negative_probes": {},
            "bodyless_negative_probes": {},
            "positive_probes": {}
        }
    }

    print(f"Running Compositional Convergence test suite using {tokac}...")
    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_dir = pathlib.Path(tmp_str)

        print(f"\nChecking negative probes ({len(negatives)} items, fail-closed & zero artifacts):")
        neg_dir = suite_dir / "negative_probes"
        for filename, expected_code in negatives:
            rec = check_negative_probe(tokac, neg_dir / filename, modules_dir, env, tmp_dir, expected_code)
            receipt["results"]["negative_probes"][filename] = rec

        print(f"\nChecking bodyless negative probes ({len(bodyless_negatives)} items, fail-closed & zero artifacts):")
        bodyless_dir = suite_dir / "bodyless_negative_probes"
        for filename, expected_code in bodyless_negatives:
            rec = check_negative_probe(tokac, bodyless_dir / filename, modules_dir, env, tmp_dir, expected_code)
            receipt["results"]["bodyless_negative_probes"][filename] = rec

        print(f"\nChecking positive probes ({len(positives)} items, 3 compilation modes + native execution rc==0):")
        pos_dir = suite_dir / "positive_probes"
        for filename in positives:
            rec = check_positive_probe(tokac, pos_dir / filename, modules_dir, env, tmp_dir)
            receipt["results"]["positive_probes"][filename] = rec

        # Verify ext_provider.tki portability in isolated foreign directory
        port_rec = check_portable_ext_provider(tokac, modules_dir, env, tmp_dir)
        receipt["results"]["portability_check"] = port_rec

    receipt["status"] = "ALL_PASSED"
    total_probes = receipt["counts"]["total_probes"]
    pos_count = receipt["counts"]["positive_probes"]
    print(f"\nAll {total_probes} Compositional Convergence probes passed successfully (including {pos_count}/{pos_count} native execution rc=0 & portable ext_provider).")

    # Output receipt
    output_path = pathlib.Path(args.output_receipt).resolve() if args.output_receipt else None

    if output_path:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(f"Saved execution receipt to: {output_path}")


if __name__ == "__main__":
    main()
