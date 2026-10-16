#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.17. No source compiler or private inputs."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

EXPECTED_017_FIXTURES_DIGEST = "7c18eb0f463654ab8ed3d4c8d5fbf2e75bf3c0a5a18e6219dd5ef66663c3e19e"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compute_fixtures_digest(fixtures_dir: Path) -> str:
    hasher = hashlib.sha256()
    for p in sorted(fixtures_dir.rglob("*.tk")):
        rel = p.relative_to(fixtures_dir).as_posix()
        data = p.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        hasher.update(rel.encode("utf-8"))
        hasher.update(sha.encode("utf-8"))
    return hasher.hexdigest()


def main():
    p = argparse.ArgumentParser(description="Toka 0.17 installed candidate verification")
    p.add_argument("--sdk", type=Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.17.0", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r017_fixtures")
    p.add_argument("--output", type=Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    if not (sdk / "bin/tokac").is_file():
        raise SystemExit(f"Invalid SDK root: {sdk}/bin/tokac missing")

    fixtures_dir = a.fixtures.resolve() if a.fixtures else (Path(__file__).resolve().parent / "r017_fixtures")
    if not fixtures_dir.is_dir():
        raise SystemExit(f"Fixtures directory missing: {fixtures_dir}")

    fixtures_digest = compute_fixtures_digest(fixtures_dir)
    if fixtures_digest != EXPECTED_017_FIXTURES_DIGEST:
        raise SystemExit(f"Fixtures digest mismatch: got {fixtures_digest}, expected {EXPECTED_017_FIXTURES_DIGEST}")

    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise SystemExit("Missing r017_fixtures/manifest.json")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    for rel, exp_sha in manifest.items():
        actual_sha = sha256_file(fixtures_dir / rel)
        if actual_sha != exp_sha:
            raise SystemExit(f"Fixture corrupted: {rel} (got {actual_sha}, expected {exp_sha})")

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-017-{time.strftime('%Y%m%d-%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)

    sdk_json_path = sdk / "sdk.json"
    if not sdk_json_path.is_file():
        raise SystemExit(f"Missing sdk.json in SDK root: {sdk_json_path}")
    sdk_meta = json.loads(sdk_json_path.read_text(encoding="utf-8"))

    if sdk_meta.get("candidate_revision") != a.revision:
        raise SystemExit(f"SDK revision mismatch: sdk has {sdk_meta.get('candidate_revision')}, expected {a.revision}")
    if sdk_meta.get("version_label") != a.version:
        raise SystemExit(f"SDK version mismatch: sdk has {sdk_meta.get('version_label')}, expected {a.version}")
    if sdk_meta.get("build_testing") is not False:
        raise SystemExit(f"Standard candidate SDK must have build_testing=false: {sdk_meta.get('build_testing')}")

    target = a.target or sdk_meta.get("target") or "unknown"
    control_script_sha256 = sha256_file(Path(__file__).resolve())

    env = dict(os.environ)
    env["PATH"] = f"{sdk / 'bin'}:{env.get('PATH', '')}"
    if (sdk / "lib").is_dir():
        env["TOKA_LIB"] = str(sdk / "lib")

    tokac_bin = str(sdk / "bin/tokac")
    receipts = []

    def run(name, cmd, cwd, allow_fail=False, diagnostic=None, artifact_absent=False):
        t0 = time.time()
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=60)
        elapsed = time.time() - t0
        combined = r.stdout + r.stderr

        ok = True
        err_msg = ""
        signal_val = None
        termination_val = None

        if r.returncode < 0:
            ok = False
            signal_val = -r.returncode
            termination_val = f"terminated by signal {signal_val}"
            err_msg = termination_val
        elif not allow_fail and r.returncode != 0:
            ok = False
            err_msg = f"command failed with exit {r.returncode}"
        elif allow_fail and r.returncode != 1:
            ok = False
            err_msg = f"negative command expected exit 1, got {r.returncode}"

        if diagnostic and diagnostic not in combined:
            ok = False
            err_msg = f"missing required diagnostic {diagnostic}"

        rec = {
            "name": name,
            "argv": [str(c) for c in cmd],
            "cmd": [str(c) for c in cmd],
            "cwd": str(cwd),
            "exit_code": r.returncode,
            "signal": signal_val,
            "termination": termination_val,
            "error": None if ok else err_msg,
            "result": "pass" if ok else "fail",
            "stdout": r.stdout,
            "stderr": r.stderr,
            "stdout_sha256": sha256_bytes(r.stdout.encode("utf-8")),
            "stderr_sha256": sha256_bytes(r.stderr.encode("utf-8")),
            "execution_ms": round(elapsed * 1000.0, 2),
            "elapsed_seconds": round(elapsed, 4),
            "ok": ok,
        }
        if diagnostic:
            rec["diagnostic"] = diagnostic
        if artifact_absent:
            rec["artifact_absent"] = True
        if not ok:
            receipts.append(rec)
            raise RuntimeError(f"Step {name} failed: {err_msg}\n{combined}")
        receipts.append(rec)
        return r

    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)

    # 1. Four tool versions
    expected_v = a.version.removeprefix("v")
    versions = {}
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        tool_bin = str(sdk / f"bin/{tool}")
        r = run(f"version-{tool}", [tool_bin, "--version"], work)
        if expected_v not in r.stdout and expected_v not in r.stderr:
            raise RuntimeError(f"{tool} version output missing {expected_v}")
        versions[tool] = r.stdout.strip()

    # 2. Positive compositional consistency controls
    pos_cases = [
        ("pos1_d1_m3_carrier_consume", fixtures_dir / "pass/pos1_d1_m3_carrier_consume.tk", 0),
        ("pos2_d2_m3_carrier_fwd", fixtures_dir / "pass/pos2_d2_m3_carrier_fwd.tk", 0),
        ("pos3_d5_m2_split_wrapper", fixtures_dir / "pass/pos3_d5_m2_split_wrapper.tk", 0),
        ("pos7_owned_task_start", fixtures_dir / "pass/pos7_owned_task_start.tk", 0),
        ("pos8_dual_scope_drain_both", fixtures_dir / "pass/pos8_dual_scope_drain_both.tk", 0),
        ("pos9_owned_carrier_split_var_start", fixtures_dir / "pass/pos9_owned_carrier_split_var_start.tk", 0),
        ("r2_p1_1a_two_tasks_inline_owned_start_pos", fixtures_dir / "pass/r2_p1_1a_two_tasks_inline_owned_start_pos.tk", 0),
        ("r2_p1_1b_two_tasks_split_var_owned_start_pos", fixtures_dir / "pass/r2_p1_1b_two_tasks_split_var_owned_start_pos.tk", 0),
        ("r3_p1_1a_two_tasks_fwd_call_owned_start_pos", fixtures_dir / "pass/r3_p1_1a_two_tasks_fwd_call_owned_start_pos.tk", 0),
        ("r5_pos1_cede_self_receiver_start", fixtures_dir / "pass/r5_pos1_cede_self_receiver_start.tk", 0),
    ]

    for name, src, exp_ret in pos_cases:
        bin_out = work / f"{name}_bin"
        run(f"pos-{name}-compile", [tokac_bin, str(src), "-o", str(bin_out)], work)
        r_run = run(f"pos-{name}-run", [str(bin_out)], work)
        if r_run.returncode != exp_ret:
            raise RuntimeError(f"Positive run {name} returned {r_run.returncode}, expected {exp_ret}")

    # 3. Sourceless TKI compilation & execution with physical source hiding and negative fallback controls
    provider_src = fixtures_dir / "sourceless/scoped_provider.tk"
    consumer_src = fixtures_dir / "sourceless/level_a_retained_body_consumer.tk"

    # Step A: Compile provider in an isolated directory
    provider_stage = work / "tki_provider_stage"
    provider_stage.mkdir(parents=True, exist_ok=True)
    provider_copy = provider_stage / "scoped_provider.tk"
    shutil.copy2(provider_src, provider_copy)

    run("pos-tki-provider-compile", [tokac_bin, "-c", str(provider_copy), "--emit-interface", "-o", str(provider_stage / "provider.o")], provider_stage)
    provider_tki = provider_stage / "provider.tki"
    provider_o = provider_stage / "provider.o"
    if not provider_tki.is_file() or not provider_o.is_file():
        raise RuntimeError("TKI provider compilation did not emit provider.tki and provider.o")

    # Record hashes of emitted interface and object
    provider_tki_sha = sha256_file(provider_tki)
    provider_o_sha = sha256_file(provider_o)

    # Physically delete provider source so it is completely inaccessible
    provider_copy.unlink()

    # Step B: Setup consumer stage containing only consumer source, scoped_provider.tki, and provider.o
    consumer_stage = work / "tki_consumer_stage"
    consumer_stage.mkdir(parents=True, exist_ok=True)
    consumer_copy = consumer_stage / "level_a_retained_body_consumer.tk"
    shutil.copy2(consumer_src, consumer_copy)
    shutil.copy2(provider_tki, consumer_stage / "scoped_provider.tki")
    shutil.copy2(provider_o, consumer_stage / "provider.o")
    consumed_tki_sha = sha256_file(consumer_stage / "scoped_provider.tki")
    consumed_o_sha = sha256_file(consumer_stage / "provider.o")

    # Clean provider stage entirely to guarantee no alternative source or cache routes exist
    shutil.rmtree(provider_stage, ignore_errors=True)

    # Negative Control 1: Missing .tki interface must fail closed with exit 1
    missing_stage = work / "tki_missing_stage"
    missing_stage.mkdir(parents=True, exist_ok=True)
    missing_consumer = missing_stage / "consumer.tk"
    shutil.copy2(consumer_src, missing_consumer)
    missing_cmd = [tokac_bin, "-I", str(missing_stage), str(missing_consumer), str(consumer_stage / "provider.o"), "-o", str(missing_stage / "bad_bin")]
    t0_missing = time.time()
    r_missing = subprocess.run(
        missing_cmd,
        env=env, cwd=missing_stage, capture_output=True, text=True, timeout=30
    )
    elapsed_missing = time.time() - t0_missing
    if (missing_stage / "bad_bin").exists():
        raise RuntimeError("Missing TKI control produced binary")
    if r_missing.returncode != 1:
        raise RuntimeError(f"Missing TKI control failed: expected exit 1, got {r_missing.returncode}")
    shutil.rmtree(missing_stage, ignore_errors=True)

    # Negative Control 2: Corrupted .tki interface must fail closed with exit 1
    corrupt_stage = work / "tki_corrupt_stage"
    corrupt_stage.mkdir(parents=True, exist_ok=True)
    corrupt_consumer = corrupt_stage / "consumer.tk"
    shutil.copy2(consumer_src, corrupt_consumer)
    (corrupt_stage / "scoped_provider.tki").write_text("// corrupted tki interface header\ninvalid content\n", encoding="utf-8")
    corrupt_cmd = [tokac_bin, "-I", str(corrupt_stage), str(corrupt_consumer), str(consumer_stage / "provider.o"), "-o", str(corrupt_stage / "bad_bin")]
    t0_corrupt = time.time()
    r_corrupt = subprocess.run(
        corrupt_cmd,
        env=env, cwd=corrupt_stage, capture_output=True, text=True, timeout=30
    )
    elapsed_corrupt = time.time() - t0_corrupt
    if (corrupt_stage / "bad_bin").exists():
        raise RuntimeError("Corrupt TKI control produced binary")
    if r_corrupt.returncode != 1:
        raise RuntimeError(f"Corrupt TKI control failed: expected exit 1, got {r_corrupt.returncode}")
    shutil.rmtree(corrupt_stage, ignore_errors=True)

    # Positive Step C: Consumer compiles against provider.tki + provider.o without provider source
    tki_bin = consumer_stage / "tki_bin"
    run("pos-tki-consumer-compile", [tokac_bin, "-I", str(consumer_stage), str(consumer_copy), str(consumer_stage / "provider.o"), "-o", str(tki_bin)], consumer_stage)
    r_tki = run("pos-tki-consumer-run", [str(tki_bin)], consumer_stage)
    if r_tki.returncode != 0:
        raise RuntimeError(f"TKI consumer run failed: exit={r_tki.returncode}, output={r_tki.stdout}")

    # Negative Control 3: Source-hidden Retained-Body escape negative across --check-only, -c, and --emit-llvm
    escape_stage = work / "tki_escape_stage"
    escape_stage.mkdir(parents=True, exist_ok=True)
    escape_src = fixtures_dir / "sourceless/level_a_cross_module_escape_negative.tk"
    escape_copy = escape_stage / "level_a_cross_module_escape_negative.tk"
    shutil.copy2(escape_src, escape_copy)
    shutil.copy2(consumer_stage / "scoped_provider.tki", escape_stage / "scoped_provider.tki")

    escape_check_cmd = [tokac_bin, "-I", str(escape_stage), "--check-only", str(escape_copy)]
    t0_esc_chk = time.time()
    r_esc_chk = subprocess.run(escape_check_cmd, env=env, cwd=escape_stage, capture_output=True, text=True, timeout=30)
    el_esc_chk = time.time() - t0_esc_chk
    if r_esc_chk.returncode != 1 or "E0455" not in r_esc_chk.stderr:
        raise RuntimeError(f"Source-hidden escape check failed: exit={r_esc_chk.returncode}, stderr={r_esc_chk.stderr}")

    escape_o = escape_stage / "escape.o"
    escape_obj_cmd = [tokac_bin, "-I", str(escape_stage), "-c", str(escape_copy), "-o", str(escape_o)]
    t0_esc_obj = time.time()
    r_esc_obj = subprocess.run(escape_obj_cmd, env=env, cwd=escape_stage, capture_output=True, text=True, timeout=30)
    el_esc_obj = time.time() - t0_esc_obj
    if escape_o.exists() or r_esc_obj.returncode != 1 or "E0455" not in r_esc_obj.stderr:
        raise RuntimeError(f"Source-hidden escape object failed: exit={r_esc_obj.returncode}, product_exists={escape_o.exists()}")

    escape_ll = escape_stage / "escape.ll"
    escape_ir_cmd = [tokac_bin, "-I", str(escape_stage), "--emit-llvm", "-c", str(escape_copy), "-o", str(escape_ll)]
    t0_esc_ir = time.time()
    r_esc_ir = subprocess.run(escape_ir_cmd, env=env, cwd=escape_stage, capture_output=True, text=True, timeout=30)
    el_esc_ir = time.time() - t0_esc_ir
    if escape_ll.exists() or r_esc_ir.returncode != 1 or "E0455" not in r_esc_ir.stderr:
        raise RuntimeError(f"Source-hidden escape IR failed: exit={r_esc_ir.returncode}, product_exists={escape_ll.exists()}")

    escape_bin = escape_stage / "escape_bin"
    escape_bin_cmd = [tokac_bin, "-I", str(escape_stage), str(escape_copy), str(consumer_stage / "provider.o"), "-o", str(escape_bin)]
    t0_esc_bin = time.time()
    r_esc_bin = subprocess.run(escape_bin_cmd, env=env, cwd=escape_stage, capture_output=True, text=True, timeout=30)
    el_esc_bin = time.time() - t0_esc_bin
    if escape_bin.exists() or r_esc_bin.returncode != 1 or "E0455" not in r_esc_bin.stderr:
        raise RuntimeError(f"Source-hidden escape binary failed: exit={r_esc_bin.returncode}, product_exists={escape_bin.exists()}")

    shutil.rmtree(escape_stage, ignore_errors=True)

    # 4. Negative compositional consistency controls (multi-mode zero-artifact checks)
    multi_mode_negatives = [
        ("leak1_d3_m2_split_var", fixtures_dir / "fail/leak1_d3_m2_split_var.tk", "E04584"),
        ("leak2_d3_m3_unboxed_field", fixtures_dir / "fail/leak2_d3_m3_unboxed_field.tk", "E04584"),
        ("leak6_d4_m3_field_escape", fixtures_dir / "fail/leak6_d4_m3_field_escape.tk", "E0456"),
        ("leak7_d5_m3_dual_scope", fixtures_dir / "fail/leak7_d5_m3_dual_scope.tk", "E0456"),
        ("leak8_cast_cede_leak3_outer_whole_reassign", fixtures_dir / "fail/leak8_cast_cede_leak3_outer_whole_reassign.tk", "E0456"),
        ("leak9_dual_carrier_method_spawn_s1_join_s2", fixtures_dir / "fail/leak9_dual_carrier_method_spawn_s1_join_s2.tk", "E0456"),
        ("leak10_method_receiver_start_bypass_then_join_empty_s1_split_var", fixtures_dir / "fail/leak10_method_receiver_start_bypass_then_join_empty_s1_split_var.tk", "E0456"),
        ("r2_p0_1a_nested_carrier_split_var_start", fixtures_dir / "fail/r2_p0_1a_nested_carrier_split_var_start.tk", "E04584"),
        ("r2_p0_2a_effects_self_taskscope_start_bypass", fixtures_dir / "fail/r2_p0_2a_effects_self_taskscope_start_bypass.tk", "E0456"),
        ("r3_p0_1a_holder_method_set_task_then_start", fixtures_dir / "fail/r3_p0_1a_holder_method_set_task_then_start.tk", "E04584"),
        ("r4_p0_1_nested_field_assign_then_start", fixtures_dir / "fail/r4_p0_1_nested_field_assign_then_start.tk", "E04584"),
        ("r5_neg1_borrowed_self_field_postfix_start", fixtures_dir / "fail/r5_neg1_borrowed_self_field_postfix_start.tk", "E04584"),
        ("r5_neg2_cede_self_carrier_with_borrowed_task_start", fixtures_dir / "fail/r5_neg2_cede_self_carrier_with_borrowed_task_start.tk", "E04584"),
    ]

    for base_name, src, diag in multi_mode_negatives:
        run(f"neg-{base_name}-check", [tokac_bin, "--check-only", str(src)], work, allow_fail=True, diagnostic=diag)

        obj_out = work / f"{base_name}.o"
        obj_out.unlink(missing_ok=True)
        run(f"neg-{base_name}-object", [tokac_bin, "-c", str(src), "-o", str(obj_out)], work, allow_fail=True, diagnostic=diag, artifact_absent=True)
        if obj_out.exists():
            raise RuntimeError(f"{base_name} produced object file")

        ir_out = work / f"{base_name}.ll"
        ir_out.unlink(missing_ok=True)
        run(f"neg-{base_name}-ir", [tokac_bin, "--emit-llvm", "-c", str(src), "-o", str(ir_out)], work, allow_fail=True, diagnostic=diag, artifact_absent=True)
        if ir_out.exists():
            raise RuntimeError(f"{base_name} produced LLVM IR file")

        compile_out = work / f"{base_name}.bin"
        compile_out.unlink(missing_ok=True)
        run(f"neg-{base_name}-compile", [tokac_bin, str(src), "-o", str(compile_out)], work, allow_fail=True, diagnostic=diag, artifact_absent=True)
        if compile_out.exists():
            raise RuntimeError(f"{base_name} produced binary file")

    # Additional negative check-only controls
    single_check_negatives = [
        ("r2_p0_1b_nested_carrier_inline_start", fixtures_dir / "fail/r2_p0_1b_nested_carrier_inline_start.tk", "E04584"),
        ("r2_p0_1c_nested_carrier_two_step_var_start", fixtures_dir / "fail/r2_p0_1c_nested_carrier_two_step_var_start.tk", "E04584"),
        ("r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass", fixtures_dir / "fail/r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass.tk", "E0456"),
        ("r3_p0_1b_holder_reassign_via_fwd_call_then_start", fixtures_dir / "fail/r3_p0_1b_holder_reassign_via_fwd_call_then_start.tk", "E04584"),
        ("r3_p0_2a_effects_self_indirect_helper_start", fixtures_dir / "fail/r3_p0_2a_effects_self_indirect_helper_start.tk", "E0456"),
        ("r4_p0_2a_effects_self_match_start", fixtures_dir / "fail/r4_p0_2a_effects_self_match_start.tk", "E0456"),
    ]

    for name, src, diag in single_check_negatives:
        run(f"neg-{name}-check", [tokac_bin, "--check-only", str(src)], work, allow_fail=True, diagnostic=diag)

    # Clean temporary work directory
    shutil.rmtree(work, ignore_errors=True)

    report = {
        "schema": "toka.0.17-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_17_candidate.py",
        "control_script_sha256": control_script_sha256,
        "counts": {
            "total_receipts": len(receipts),
            "passed": len(receipts),
            "failed": 0,
            "groups": {
                "version": 4,
                "G-compositional-convergence-positive": 23,
                "G-compositional-convergence-negative": len(receipts) - 27,
            },
        },
        "four_tool_versions": versions,
        "source_hidden_tki": {
            "provider_tki_sha256": provider_tki_sha,
            "provider_object_sha256": provider_o_sha,
            "consumed_tki_sha256": consumed_tki_sha,
            "consumed_object_sha256": consumed_o_sha,
            "missing_interface_rejected": True,
            "corrupt_interface_rejected": True,
            "missing_interface_command": {
                "argv": [str(c) for c in missing_cmd],
                "cmd": [str(c) for c in missing_cmd],
                "cwd": str(missing_stage),
                "exit_code": r_missing.returncode,
                "signal": None,
                "termination": None,
                "error": None,
                "ok": True,
                "result": "pass",
                "artifact_absent": True,
                "stdout": r_missing.stdout,
                "stderr": r_missing.stderr,
                "stdout_sha256": sha256_bytes(r_missing.stdout.encode("utf-8")),
                "stderr_sha256": sha256_bytes(r_missing.stderr.encode("utf-8")),
                "execution_ms": round(elapsed_missing * 1000.0, 2),
                "elapsed_seconds": round(elapsed_missing, 4),
            },
            "corrupt_interface_command": {
                "argv": [str(c) for c in corrupt_cmd],
                "cmd": [str(c) for c in corrupt_cmd],
                "cwd": str(corrupt_stage),
                "exit_code": r_corrupt.returncode,
                "signal": None,
                "termination": None,
                "error": None,
                "ok": True,
                "result": "pass",
                "artifact_absent": True,
                "stdout": r_corrupt.stdout,
                "stderr": r_corrupt.stderr,
                "stdout_sha256": sha256_bytes(r_corrupt.stdout.encode("utf-8")),
                "stderr_sha256": sha256_bytes(r_corrupt.stderr.encode("utf-8")),
                "execution_ms": round(elapsed_corrupt * 1000.0, 2),
                "elapsed_seconds": round(elapsed_corrupt, 4),
            },
            "escape_negative_rejected": True,
            "escape_negative_diagnostic": "E0455",
            "escape_negative_commands": {
                "check": {
                    "argv": [str(c) for c in escape_check_cmd],
                    "cmd": [str(c) for c in escape_check_cmd],
                    "cwd": str(escape_stage),
                    "exit_code": r_esc_chk.returncode,
                    "signal": None,
                    "termination": None,
                    "error": None,
                    "ok": True,
                    "result": "pass",
                    "diagnostic": "E0455",
                    "artifact_absent": True,
                    "stdout": r_esc_chk.stdout,
                    "stderr": r_esc_chk.stderr,
                    "stdout_sha256": sha256_bytes(r_esc_chk.stdout.encode("utf-8")),
                    "stderr_sha256": sha256_bytes(r_esc_chk.stderr.encode("utf-8")),
                    "execution_ms": round(el_esc_chk * 1000.0, 2),
                    "elapsed_seconds": round(el_esc_chk, 4),
                },
                "object": {
                    "argv": [str(c) for c in escape_obj_cmd],
                    "cmd": [str(c) for c in escape_obj_cmd],
                    "cwd": str(escape_stage),
                    "exit_code": r_esc_obj.returncode,
                    "signal": None,
                    "termination": None,
                    "error": None,
                    "ok": True,
                    "result": "pass",
                    "diagnostic": "E0455",
                    "artifact_absent": not escape_o.exists(),
                    "stdout": r_esc_obj.stdout,
                    "stderr": r_esc_obj.stderr,
                    "stdout_sha256": sha256_bytes(r_esc_obj.stdout.encode("utf-8")),
                    "stderr_sha256": sha256_bytes(r_esc_obj.stderr.encode("utf-8")),
                    "execution_ms": round(el_esc_obj * 1000.0, 2),
                    "elapsed_seconds": round(el_esc_obj, 4),
                },
                "ir": {
                    "argv": [str(c) for c in escape_ir_cmd],
                    "cmd": [str(c) for c in escape_ir_cmd],
                    "cwd": str(escape_stage),
                    "exit_code": r_esc_ir.returncode,
                    "signal": None,
                    "termination": None,
                    "error": None,
                    "ok": True,
                    "result": "pass",
                    "diagnostic": "E0455",
                    "artifact_absent": not escape_ll.exists(),
                    "stdout": r_esc_ir.stdout,
                    "stderr": r_esc_ir.stderr,
                    "stdout_sha256": sha256_bytes(r_esc_ir.stdout.encode("utf-8")),
                    "stderr_sha256": sha256_bytes(r_esc_ir.stderr.encode("utf-8")),
                    "execution_ms": round(el_esc_ir * 1000.0, 2),
                    "elapsed_seconds": round(el_esc_ir, 4),
                },
                "compile": {
                    "argv": [str(c) for c in escape_bin_cmd],
                    "cmd": [str(c) for c in escape_bin_cmd],
                    "cwd": str(escape_stage),
                    "exit_code": r_esc_bin.returncode,
                    "signal": None,
                    "termination": None,
                    "error": None,
                    "ok": True,
                    "result": "pass",
                    "diagnostic": "E0455",
                    "artifact_absent": not escape_bin.exists(),
                    "stdout": r_esc_bin.stdout,
                    "stderr": r_esc_bin.stderr,
                    "stdout_sha256": sha256_bytes(r_esc_bin.stdout.encode("utf-8")),
                    "stderr_sha256": sha256_bytes(r_esc_bin.stderr.encode("utf-8")),
                    "execution_ms": round(el_esc_bin * 1000.0, 2),
                    "elapsed_seconds": round(el_esc_bin, 4),
                },
            },
            "consumer_result": "pass",
            "provider_source_present": False,
        },
        "groups": ["G-compositional-convergence"],
        "SDK_root": str(sdk),
        "receipts": receipts,
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
