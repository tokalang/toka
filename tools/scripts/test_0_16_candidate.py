#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.16. No source compiler or private inputs."""
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

EXPECTED_016_FIXTURES_DIGEST = "7579bc4271da9eb1f1f413309b580fe8495b9a28c0b15fbe430a18b89267923e"


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
    p = argparse.ArgumentParser(description="Toka 0.16 installed candidate verification")
    p.add_argument("--sdk", type=Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.16.0", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r016_fixtures")
    p.add_argument("--output", type=Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    if not (sdk / "bin/tokac").is_file():
        raise SystemExit(f"Invalid SDK root: {sdk}/bin/tokac missing")

    fixtures_dir = a.fixtures.resolve() if a.fixtures else (Path(__file__).resolve().parent / "r016_fixtures")
    if not fixtures_dir.is_dir():
        raise SystemExit(f"Fixtures directory missing: {fixtures_dir}")

    fixtures_digest = compute_fixtures_digest(fixtures_dir)
    if fixtures_digest != EXPECTED_016_FIXTURES_DIGEST:
        raise SystemExit(f"Fixtures digest mismatch: got {fixtures_digest}, expected {EXPECTED_016_FIXTURES_DIGEST}")

    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise SystemExit("Missing r016_fixtures/manifest.json")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    for rel, exp_sha in manifest.items():
        actual_sha = sha256_file(fixtures_dir / rel)
        if actual_sha != exp_sha:
            raise SystemExit(f"Fixture corrupted: {rel} (got {actual_sha}, expected {exp_sha})")

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-016-{time.strftime('%Y%m%d-%H%M%S')}")
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

    # 2. Positive scoped tasks controls
    pos_cases = [
        ("scoped_normal_order", fixtures_dir / "pass/scoped_normal_order.tk", 0),
        ("scoped_early_exit", fixtures_dir / "pass/scoped_early_exit.tk", 0),
        ("scoped_cascade_cancel", fixtures_dir / "pass/scoped_cascade_cancel.tk", 0),
        ("forwarder_wrapper", fixtures_dir / "pass/forwarder_wrapper.tk", 0),
        ("pre_drain_cancel_order", fixtures_dir / "pass/pre_drain_cancel_order.tk", 0),
        ("mid_drain_cancel_barrier", fixtures_dir / "pass/mid_drain_cancel_barrier.tk", 0),
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

    # 4. Negative safety boundary controls (multi-mode zero-artifact checks)
    multi_mode_negatives = [
        ("return_escape", fixtures_dir / "fail/return_escape.tk", "E0455"),
        ("spawn_into_escape", fixtures_dir / "fail/spawn_into_escape.tk", "E0456"),
        ("forged_spawn_routes_return", fixtures_dir / "fail/forged_spawn_routes_return.tk", "E0456"),
        ("await_question_drain", fixtures_dir / "fail/await_question_drain.tk", "E0456"),
        ("r2_d2a_mutable_sibling_launder", fixtures_dir / "fail/r2_d2a_mutable_sibling_launder.tk", "E0456"),
        ("r2_d2b_forged_spawn_scoped_sibling", fixtures_dir / "fail/r2_d2b_forged_spawn_scoped_sibling.tk", "E0456"),
        ("forged_join", fixtures_dir / "fail/forged_join.tk", "E0456"),
        ("forged_cancel_join", fixtures_dir / "fail/forged_cancel_join.tk", "E0456"),
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
        ("detached_start", fixtures_dir / "fail/detached_start.tk", "E04583"),
        ("container_escape", fixtures_dir / "fail/container_escape.tk", "E0456"),
        ("mutation_conflict", fixtures_dir / "fail/mutation_conflict.tk", "E0440"),
        ("unjoined_scope_exit", fixtures_dir / "fail/unjoined_scope_exit.tk", "E0456"),
        ("borrowed_child_result", fixtures_dir / "fail/borrowed_child_result.tk", "E04658"),
    ]

    for name, src, diag in single_check_negatives:
        run(f"neg-{name}-check", [tokac_bin, "--check-only", str(src)], work, allow_fail=True, diagnostic=diag)

    # Clean temporary work directory
    shutil.rmtree(work, ignore_errors=True)

    report = {
        "schema": "toka.0.16-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_16_candidate.py",
        "control_script_sha256": control_script_sha256,
        "counts": {
            "total_receipts": len(receipts),
            "passed": len(receipts),
            "failed": 0,
            "groups": {
                "version": 4,
                "G-scoped-tasks-positive": 15,
                "G-scoped-tasks-negative": len(receipts) - 19,
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
        "groups": ["G-scoped-tasks"],
        "SDK_root": str(sdk),
        "receipts": receipts,
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
