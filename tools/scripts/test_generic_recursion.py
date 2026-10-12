#!/usr/bin/env python3
"""Regression test suite for 0.15 Generic Recursion (Free Functions, Direct & Mutual)."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROBES_DIR = ROOT / "tests/semantics/generic_recursion"

PROBE_CONFIGS = {
    # 1. Positive Recursive Specializations
    "P01_direct_recur_generic": {
        "category": "direct_recursion",
        "description": "countdown<T> homomorphic direct self-call",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P04_mutual_recur_generic": {
        "category": "mutual_recursion",
        "description": "is_even<T> <-> is_odd<T> homomorphic mutual recursion",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P06_mutual_recur_order_permuted": {
        "category": "mutual_recursion",
        "description": "is_odd<T> declared before is_even<T> declaration order permutation",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P07_generic_binary_search_recur": {
        "category": "algorithm",
        "description": "Generic recursive binary search algorithm",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P09_generic_tree_traversal": {
        "category": "algorithm",
        "description": "Generic recursive binary tree node count traversal",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P10_cross_module_consumer": {
        "category": "module_boundary",
        "description": "Cross-module consumer instantiation of generic recursive function",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
        "include_dir": PROBES_DIR,
        "extra_sources": ["P10_cross_module_lib.tk"],
    },
    "P11_explicit_borrow_dep_recur": {
        "category": "borrow_contract",
        "description": "Generic recursive function with explicit borrow return dependency (<- a)",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P12_valid_cede_cleanup_recur": {
        "category": "ownership_cede",
        "description": "Generic recursive function with valid cede consumption of Vec<T>",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },

    # 2. Positive Controls
    "P02_direct_recur_nongeneric_control": {
        "category": "control_group",
        "description": "Non-generic direct recursion countdown_i32 control",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P03_generic_nonrecur_control": {
        "category": "control_group",
        "description": "Generic non-recursive call caller<T> -> step<T> control (returns 4 by design)",
        "expected_exit_code": 0,
        "runtime_return_code": 4,
    },
    "P05_mutual_recur_nongeneric_control": {
        "category": "control_group",
        "description": "Non-generic mutual recursion is_even_i32 <-> is_odd_i32 control",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P08_generic_binary_search_iter_control": {
        "category": "control_group",
        "description": "Generic iterative binary search control",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P10_cross_module_lib": {
        "category": "control_group",
        "description": "Generic recursive library module declaration (uninstantiated source library)",
        "expected_exit_code": 0,
        "compile_only": True,
    },
    "P11_explicit_borrow_dep_control": {
        "category": "control_group",
        "description": "Non-recursive generic function with explicit borrow return dependency",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P12_valid_cede_cleanup_control": {
        "category": "control_group",
        "description": "Non-recursive generic function with valid cede consumption of Vec<T>",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },
    "P13_generic_async_nonrecur_control": {
        "category": "control_group",
        "description": "Non-recursive generic async function control (preserved existing capability)",
        "expected_exit_code": 0,
        "runtime_return_code": 0,
    },

    # 3. Negative Safety Boundary Probes
    "N01_recur_type_mismatch": {
        "category": "safety_boundary",
        "description": "Type mismatch in recursive return (expected E0408)",
        "expected_exit_code": 1,
        "required_diags": ["E0408"],
        "compile_only": True,
    },
    "N02_recur_trait_bound_violation": {
        "category": "safety_boundary",
        "description": "Unsatisfied trait bound on recursive generic specialization (expected E0606)",
        "expected_exit_code": 1,
        "required_diags": ["E0606"],
        "compile_only": True,
    },
    "N03_recur_illegal_borrow_aliasing": {
        "category": "safety_boundary",
        "description": "Illegal borrow conflict / double mutable borrow in recursion (expected E0441)",
        "expected_exit_code": 1,
        "required_diags": ["E0441"],
        "compile_only": True,
    },
    "N04_recur_double_cede": {
        "category": "safety_boundary",
        "description": "Use of moved value across recursive frame boundary (expected E0438)",
        "expected_exit_code": 1,
        "required_diags": ["E0438"],
        "compile_only": True,
    },
    "N05_polymorphic_recursion": {
        "category": "safety_boundary",
        "description": "Polymorphic/type-changing recursion f<T> -> f<Option<T>> (static E0406 guard)",
        "expected_exit_code": 1,
        "required_diags": ["E0406"],
        "compile_only": True,
    },
    "N06_true_mutual_with_error": {
        "category": "safety_boundary",
        "description": "Mutual cycle member error fails whole cycle closed (root semantic error E04576 preserved)",
        "expected_exit_code": 1,
        "required_diags": ["E04576"],
        "compile_only": True,
    },
    "N07_tainted_instance_reuse_control": {
        "category": "safety_boundary",
        "description": "Tainted failed recursion cache lookup cleanly rolls back without leaking (root semantic error E04576 preserved)",
        "expected_exit_code": 1,
        "required_diags": ["E04576"],
        "compile_only": True,
    },
    "N08_async_recursion_rejected": {
        "category": "safety_boundary",
        "description": "Asynchronous generic recursion rejected out of scope (expected E0406, no artifacts)",
        "expected_exit_code": 1,
        "required_diags": ["E0406"],
        "compile_only": True,
    },
    "N09_async_mutual_rejected": {
        "category": "safety_boundary",
        "description": "Asynchronous generic mutual recursion rejected (expected E0406)",
        "expected_exit_code": 1,
        "required_diags": ["E0406"],
        "compile_only": True,
    },
    "N10_mixed_cycle_async_entry_rejected": {
        "category": "safety_boundary",
        "description": "Mixed sync/async recursion cycle with async entry rejected (expected E0406)",
        "expected_exit_code": 1,
        "required_diags": ["E0406"],
        "compile_only": True,
    },
    "N11_mixed_cycle_sync_entry_rejected": {
        "category": "safety_boundary",
        "description": "Mixed sync/async recursion cycle with sync entry rejected (expected E04661)",
        "expected_exit_code": 1,
        "required_diags": ["E04661"],
        "compile_only": True,
    },
}


def main():
    parser = argparse.ArgumentParser(description="0.15 Generic Recursion Test Suite")
    parser.add_argument("--build-dir", type=Path, default=None, help="CMake build directory containing bin/tokac")
    parser.add_argument("--tokac", type=Path, default=None, help="Explicit path to tokac compiler binary")
    parser.add_argument("--output-dir", type=Path, default=None, help="Output directory to emit test report JSON")
    args = parser.parse_args()

    if args.tokac:
        tokac = args.tokac.resolve()
    elif args.build_dir:
        tokac = (args.build_dir / "bin/tokac").resolve()
    else:
        for candidate in (
            ROOT / "build/bin/tokac",
            ROOT.parent / "builds/fix-r3-task-escape/bin/tokac",
        ):
            if candidate.exists():
                tokac = candidate
                break
        else:
            parser.error("Either --build-dir or --tokac must be specified.")

    if not tokac.exists():
        sys.exit(f"Error: tokac binary not found at {tokac}")

    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    print(f"=== 0.15 Generic Recursion Test Suite ===")
    print(f"Compiler: {tokac}")
    print(f"Library:  {env['TOKA_LIB']}")
    print(f"Probes:   {PROBES_DIR}\n")

    passed_count = 0
    total_count = 0

    with tempfile.TemporaryDirectory(prefix="toka-grecur-") as temp_dir:
        work_dir = Path(temp_dir)

        # 1. Run Directed Probes
        for probe_id, cfg in PROBE_CONFIGS.items():
            total_count += 1
            src_file = PROBES_DIR / f"{probe_id}.tk"
            extra_sources = [PROBES_DIR / s for s in cfg.get("extra_sources", [])]
            bin_file = work_dir / f"{probe_id}_bin"

            cmd = [str(tokac)]
            if "include_dir" in cfg:
                cmd.extend(["-I", str(cfg["include_dir"])])

            if cfg.get("compile_only", False):
                obj_file = work_dir / f"{probe_id}.o"
                cmd.extend(["-c", str(src_file), "-o", str(obj_file)])
            else:
                cmd.extend([str(src_file)] + [str(s) for s in extra_sources] + ["-o", str(bin_file)])

            c_res = subprocess.run(cmd, env=env, cwd=PROBES_DIR, capture_output=True, text=True, timeout=30)
            combined_diag = c_res.stdout + c_res.stderr

            ok = True
            err_reasons = []

            if c_res.returncode != cfg["expected_exit_code"]:
                ok = False
                err_reasons.append(f"compile exit {c_res.returncode} != expected {cfg['expected_exit_code']}")

            for d in cfg.get("required_diags", []):
                if d not in combined_diag:
                    ok = False
                    err_reasons.append(f"missing diagnostic {d}")

            if ok and c_res.returncode == 0 and not cfg.get("compile_only", False):
                try:
                    r_res = subprocess.run([str(bin_file)], env=env, cwd=work_dir, capture_output=True, text=True, timeout=10)
                    exp_r = cfg.get("runtime_return_code", 0)
                    if r_res.returncode != exp_r:
                        ok = False
                        err_reasons.append(f"runtime exit {r_res.returncode} != expected {exp_r}")
                except Exception as e:
                    ok = False
                    err_reasons.append(f"runtime error: {e}")

            if ok:
                passed_count += 1
                print(f"  [PASS] {probe_id:38s} ({cfg['category']})")
            else:
                print(f"  [FAIL] {probe_id:38s} Reason: {'; '.join(err_reasons)}")
                if combined_diag:
                    print(f"         output: {combined_diag.strip()[:200]}")

        # 2. Cleanup & Destructor Exact Count Audit
        total_count += 1
        cleanup_src = PROBES_DIR / "test_cleanup_drop.tk"
        cleanup_bin = work_dir / "cleanup_drop_bin"
        c_res = subprocess.run([str(tokac), str(cleanup_src), "-o", str(cleanup_bin)], env=env, cwd=work_dir, capture_output=True, text=True, timeout=30)
        if c_res.returncode == 0:
            r_res = subprocess.run([str(cleanup_bin)], env=env, cwd=work_dir, capture_output=True, text=True, timeout=10)
            if r_res.returncode == 0:
                passed_count += 1
                print(f"  [PASS] {'cleanup_drop_tracking':38s} (cede handoff drop=1, frame drops=6)")
            else:
                print(f"  [FAIL] {'cleanup_drop_tracking':38s} runtime exit {r_res.returncode}")
        else:
            print(f"  [FAIL] {'cleanup_drop_tracking':38s} compile exit {c_res.returncode}: {c_res.stderr.strip()}")

        # 3. Source-Hidden TKI Compilation & Linkage with Negative Fallback Controls
        total_count += 1
        tki_provider_src = PROBES_DIR / "tki_provider.tk"
        tki_consumer_src = PROBES_DIR / "tki_consumer.tk"
        provider_stage_dir = work_dir / "tki_provider_stage"
        provider_stage_dir.mkdir(parents=True, exist_ok=True)
        provider_copy = provider_stage_dir / "tki_provider.tk"
        shutil.copy2(tki_provider_src, provider_copy)

        c1 = subprocess.run(
            [str(tokac), str(provider_copy), "-c", "-o", str(provider_stage_dir / "provider.o")],
            env=env, cwd=provider_stage_dir, capture_output=True, text=True, timeout=30
        )
        tki_file = provider_stage_dir / "provider.tki"
        o_file = provider_stage_dir / "provider.o"

        if c1.returncode == 0 and tki_file.exists() and o_file.exists():
            # Physically remove provider source so it cannot be reached
            provider_copy.unlink()

            consumer_stage_dir = work_dir / "tki_consumer_stage"
            consumer_stage_dir.mkdir(parents=True, exist_ok=True)
            consumer_copy = consumer_stage_dir / "tki_consumer.tk"
            shutil.copy2(tki_consumer_src, consumer_copy)
            shutil.copy2(tki_file, consumer_stage_dir / "provider.tki")
            shutil.copy2(o_file, consumer_stage_dir / "provider.o")

            # Clean provider stage to eliminate alternate source/cache routes
            shutil.rmtree(provider_stage_dir, ignore_errors=True)

            # Negative control: missing .tki must fail with exit 1
            missing_stage = work_dir / "tki_missing_stage"
            missing_stage.mkdir(parents=True, exist_ok=True)
            missing_consumer = missing_stage / "tki_consumer.tk"
            shutil.copy2(tki_consumer_src, missing_consumer)
            c_missing = subprocess.run(
                [str(tokac), "-I", str(missing_stage), str(missing_consumer), str(consumer_stage_dir / "provider.o"), "-o", str(missing_stage / "bad.bin")],
                env=env, cwd=missing_stage, capture_output=True, text=True, timeout=30
            )
            missing_ok = (c_missing.returncode == 1)
            shutil.rmtree(missing_stage, ignore_errors=True)

            # Negative control: corrupted .tki must fail with exit 1
            corrupt_stage = work_dir / "tki_corrupt_stage"
            corrupt_stage.mkdir(parents=True, exist_ok=True)
            corrupt_consumer = corrupt_stage / "tki_consumer.tk"
            shutil.copy2(tki_consumer_src, corrupt_consumer)
            (corrupt_stage / "provider.tki").write_text("// corrupt header\ninvalid\n", encoding="utf-8")
            c_corrupt = subprocess.run(
                [str(tokac), "-I", str(corrupt_stage), str(corrupt_consumer), str(consumer_stage_dir / "provider.o"), "-o", str(corrupt_stage / "bad.bin")],
                env=env, cwd=corrupt_stage, capture_output=True, text=True, timeout=30
            )
            corrupt_ok = (c_corrupt.returncode == 1)
            shutil.rmtree(corrupt_stage, ignore_errors=True)

            # Positive build against source-hidden .tki and .o
            tki_bin = consumer_stage_dir / "tki_bin"
            c2 = subprocess.run(
                [str(tokac), "-I", str(consumer_stage_dir), str(consumer_copy), str(consumer_stage_dir / "provider.o"), "-o", str(tki_bin)],
                env=env, cwd=consumer_stage_dir, capture_output=True, text=True, timeout=30
            )
            if missing_ok and corrupt_ok and c2.returncode == 0 and tki_bin.exists():
                r_tki = subprocess.run([str(tki_bin)], env=env, cwd=consumer_stage_dir, capture_output=True, text=True, timeout=10)
                if r_tki.returncode == 0:
                    passed_count += 1
                    print(f"  [PASS] {'source_hidden_tki_boundary':38s} (provider source absent; recur_sum=55; neg controls pass)")
                else:
                    print(f"  [FAIL] {'source_hidden_tki_boundary':38s} runtime exit {r_tki.returncode}")
            else:
                print(f"  [FAIL] {'source_hidden_tki_boundary':38s} (missing_ok={missing_ok}, corrupt_ok={corrupt_ok}, consumer_exit={c2.returncode})")
        else:
            print(f"  [FAIL] {'source_hidden_tki_boundary':38s} provider build exit {c1.returncode}: {c1.stderr.strip()}")

        # 4. Async Boundary Multi-Mode Zero-Artifact Audit
        total_count += 1
        n08_src = PROBES_DIR / "N08_async_recursion_rejected.tk"
        p13_src = PROBES_DIR / "P13_generic_async_nonrecur_control.tk"
        async_stage = work_dir / "async_audit"
        async_stage.mkdir(parents=True, exist_ok=True)

        c_check = subprocess.run([str(tokac), "--check-only", str(n08_src)], env=env, cwd=async_stage, capture_output=True, text=True, timeout=30)
        check_ok = (c_check.returncode == 1 and "E0406" in (c_check.stdout + c_check.stderr))

        n08_o = async_stage / "n08.o"
        if n08_o.exists(): n08_o.unlink()
        c_obj = subprocess.run([str(tokac), "-c", str(n08_src), "-o", str(n08_o)], env=env, cwd=async_stage, capture_output=True, text=True, timeout=30)
        obj_ok = (c_obj.returncode == 1 and "E0406" in (c_obj.stdout + c_obj.stderr) and not n08_o.exists())

        n08_ll = async_stage / "n08.ll"
        if n08_ll.exists(): n08_ll.unlink()
        c_ir = subprocess.run([str(tokac), "--emit-llvm", "-c", str(n08_src), "-o", str(n08_ll)], env=env, cwd=async_stage, capture_output=True, text=True, timeout=30)
        ir_ok = (c_ir.returncode == 1 and "E0406" in (c_ir.stdout + c_ir.stderr) and not n08_ll.exists())

        p13_bin = async_stage / "p13_bin"
        c_p13 = subprocess.run([str(tokac), str(p13_src), "-o", str(p13_bin)], env=env, cwd=async_stage, capture_output=True, text=True, timeout=30)
        p13_ok = False
        if c_p13.returncode == 0:
            r_p13 = subprocess.run([str(p13_bin)], env=env, cwd=async_stage, capture_output=True, text=True, timeout=10)
            p13_ok = (r_p13.returncode == 0)

        if check_ok and obj_ok and ir_ok and p13_ok:
            passed_count += 1
            print(f"  [PASS] {'async_boundary_zero_artifact':38s} (check/obj/IR all rejected with 0 artifacts)")
        else:
            print(f"  [FAIL] {'async_boundary_zero_artifact':38s} (check={check_ok}, obj={obj_ok}, ir={ir_ok}, p13={p13_ok})")

    print(f"\n==========================================")
    print(f"Summary: {passed_count}/{total_count} passed ({passed_count * 100 // total_count}%)")
    print(f"==========================================")

    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        report_data = {
            "schema": "toka.generic-recursion-gate",
            "version": 1,
            "result": "pass" if passed_count == total_count else "fail",
            "tokac": str(tokac),
            "total_probes": total_count,
            "passed_probes": passed_count,
            "failed_probes": total_count - passed_count,
        }
        (args.output_dir / "generic-recursion-report.json").write_text(json.dumps(report_data, indent=2) + "\n", encoding="utf-8")

    if passed_count != total_count:
        sys.exit(1)


if __name__ == "__main__":
    main()
