#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.15. No source compiler or private inputs."""
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

EXPECTED_015_FIXTURES_DIGEST = "19d90ad6d157e4f12a3ddab1ddd73721df11f5f321c68a0e6ac3877792f191a4"


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
    p = argparse.ArgumentParser(description="Toka 0.15 installed candidate verification")
    p.add_argument("--sdk", type=Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.15.0", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r015_fixtures")
    p.add_argument("--output", type=Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    if not (sdk / "bin/tokac").is_file():
        raise SystemExit(f"Invalid SDK root: {sdk}/bin/tokac missing")

    fixtures_dir = a.fixtures.resolve() if a.fixtures else (Path(__file__).resolve().parent / "r015_fixtures")
    if not fixtures_dir.is_dir():
        raise SystemExit(f"Fixtures directory missing: {fixtures_dir}")

    fixtures_digest = compute_fixtures_digest(fixtures_dir)
    if fixtures_digest != EXPECTED_015_FIXTURES_DIGEST:
        raise SystemExit(f"Fixtures digest mismatch: got {fixtures_digest}, expected {EXPECTED_015_FIXTURES_DIGEST}")

    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise SystemExit("Missing r015_fixtures/manifest.json")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    for rel, exp_sha in manifest.items():
        actual_sha = sha256_file(fixtures_dir / rel)
        if actual_sha != exp_sha:
            raise SystemExit(f"Fixture corrupted: {rel} (got {actual_sha}, expected {exp_sha})")

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-015-{time.strftime('%Y%m%d-%H%M%S')}")
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

    # 2. Positive generic recursion controls
    pos_cases = [
        ("recur_direct", fixtures_dir / "pass/recur_direct.tk", 0),
        ("recur_mutual", fixtures_dir / "pass/recur_mutual.tk", 0),
        ("recur_order_permuted", fixtures_dir / "pass/recur_order_permuted.tk", 0),
        ("recur_bsearch", fixtures_dir / "pass/recur_bsearch.tk", 0),
        ("recur_tree", fixtures_dir / "pass/recur_tree.tk", 0),
        ("recur_borrow", fixtures_dir / "pass/recur_borrow.tk", 0),
        ("recur_cede", fixtures_dir / "pass/recur_cede.tk", 0),
        ("cleanup_drop", fixtures_dir / "pass/cleanup_drop.tk", 0),
        ("async_nonrecur_control", fixtures_dir / "pass/async_nonrecur_control.tk", 0),
    ]

    for name, src, exp_ret in pos_cases:
        bin_out = work / f"{name}_bin"
        run(f"pos-{name}-compile", [tokac_bin, str(src), "-o", str(bin_out)], work)
        r_run = run(f"pos-{name}-run", [str(bin_out)], work)
        if r_run.returncode != exp_ret:
            raise RuntimeError(f"Positive run {name} returned {r_run.returncode}, expected {exp_ret}")

    # Sourceless TKI compilation & execution with physical source hiding and negative fallback controls
    provider_src = fixtures_dir / "sourceless/tki_provider.tk"
    consumer_src = fixtures_dir / "sourceless/tki_consumer.tk"

    # Step A: Compile provider in an isolated directory
    provider_stage = work / "tki_provider_stage"
    provider_stage.mkdir(parents=True, exist_ok=True)
    provider_copy = provider_stage / "tki_provider.tk"
    shutil.copy2(provider_src, provider_copy)

    run("pos-tki-provider-compile", [tokac_bin, str(provider_copy), "-c", "-o", str(provider_stage / "provider.o")], provider_stage)
    provider_tki = provider_stage / "provider.tki"
    provider_o = provider_stage / "provider.o"
    if not provider_tki.is_file() or not provider_o.is_file():
        raise RuntimeError("TKI provider compilation did not emit provider.tki and provider.o")

    # Record hashes of emitted interface and object
    provider_tki_sha = sha256_file(provider_tki)
    provider_o_sha = sha256_file(provider_o)

    # Physically delete provider source so it is completely inaccessible
    provider_copy.unlink()

    # Step B: Setup consumer stage containing only consumer source, provider.tki, and provider.o
    consumer_stage = work / "tki_consumer_stage"
    consumer_stage.mkdir(parents=True, exist_ok=True)
    consumer_copy = consumer_stage / "tki_consumer.tk"
    shutil.copy2(consumer_src, consumer_copy)
    shutil.copy2(provider_tki, consumer_stage / "provider.tki")
    shutil.copy2(provider_o, consumer_stage / "provider.o")
    consumed_tki_sha = sha256_file(consumer_stage / "provider.tki")
    consumed_o_sha = sha256_file(consumer_stage / "provider.o")

    # Clean provider stage entirely to guarantee no alternative source or cache routes exist
    shutil.rmtree(provider_stage, ignore_errors=True)

    # Negative Control 1: Missing .tki interface must fail closed with exit 1 (no source fallback)
    missing_stage = work / "tki_missing_stage"
    missing_stage.mkdir(parents=True, exist_ok=True)
    missing_consumer = missing_stage / "tki_consumer.tk"
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

    # Negative Control 2: Corrupted .tki interface must fail closed with exit 1 (no source fallback)
    corrupt_stage = work / "tki_corrupt_stage"
    corrupt_stage.mkdir(parents=True, exist_ok=True)
    corrupt_consumer = corrupt_stage / "tki_consumer.tk"
    shutil.copy2(consumer_src, corrupt_consumer)
    (corrupt_stage / "provider.tki").write_text("// corrupted tki interface header\ninvalid content\n", encoding="utf-8")
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

    # 3. Negative safety boundary controls
    # Multi-mode zero-artifact checks for async recursion
    async_fix = fixtures_dir / "fail/async_recur_direct.tk"
    run("neg-async-direct-check", [tokac_bin, "--check-only", str(async_fix)], work, allow_fail=True, diagnostic="E0406")

    obj_out = work / "async_direct.o"
    obj_out.unlink(missing_ok=True)
    run("neg-async-direct-object", [tokac_bin, "-c", str(async_fix), "-o", str(obj_out)], work, allow_fail=True, diagnostic="E0406", artifact_absent=True)
    if obj_out.exists():
        raise RuntimeError("Async recursion produced object file")

    ir_out = work / "async_direct.ll"
    ir_out.unlink(missing_ok=True)
    run("neg-async-direct-ir", [tokac_bin, "--emit-llvm", "-c", str(async_fix), "-o", str(ir_out)], work, allow_fail=True, diagnostic="E0406", artifact_absent=True)
    if ir_out.exists():
        raise RuntimeError("Async recursion produced LLVM IR file")

    compile_out = work / "async_direct.bin"
    compile_out.unlink(missing_ok=True)
    run("neg-async-direct-compile", [tokac_bin, str(async_fix), "-o", str(compile_out)], work, allow_fail=True, diagnostic="E0406", artifact_absent=True)
    if compile_out.exists():
        raise RuntimeError("Async recursion produced binary file")

    # Additional negative checks
    neg_checks = [
        ("async_mutual", fixtures_dir / "fail/async_recur_mutual.tk", "E0406"),
        ("async_cycle_mixed", fixtures_dir / "fail/async_cycle_mixed.tk", "E0406"),
        ("poly_recur", fixtures_dir / "fail/poly_recur.tk", "E0406"),
        ("type_mismatch", fixtures_dir / "fail/type_mismatch.tk", "E0408"),
        ("trait_bound", fixtures_dir / "fail/trait_bound.tk", "E0606"),
        ("borrow_conflict", fixtures_dir / "fail/borrow_conflict.tk", "E0441"),
        ("double_cede", fixtures_dir / "fail/double_cede.tk", "E0438"),
        ("mutual_error", fixtures_dir / "fail/mutual_error.tk", "E04576"),
        ("tainted_reuse", fixtures_dir / "fail/tainted_reuse.tk", "E04576"),
    ]

    for name, src, diag in neg_checks:
        run(f"neg-{name}-check", [tokac_bin, "--check-only", str(src)], work, allow_fail=True, diagnostic=diag)

    # Clean temporary work directory
    shutil.rmtree(work, ignore_errors=True)

    report = {
        "schema": "toka.0.15-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_15_candidate.py",
        "control_script_sha256": control_script_sha256,
        "counts": {
            "total_receipts": len(receipts),
            "passed": len(receipts),
            "failed": 0,
            "groups": {
                "version": 4,
                "G-recursion-positive": 21,
                "G-recursion-negative": 13,
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
            "consumer_result": "pass",
            "provider_source_present": False,
        },
        "groups": ["G-recursion"],
        "SDK_root": str(sdk),
        "receipts": receipts,
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
