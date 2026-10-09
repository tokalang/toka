#!/usr/bin/env python3
"""Run installed standard SDK R3/R5 candidate controls.
No compiler source checkout or private evidence inputs required.
"""
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

NEGATIVE_TESTS = [
    ("r3_task_direct_escape", "fail/r3_task_direct_escape.tk", "E0455"),
    ("r3_task_explicit_dep_local_escape", "fail/r3_task_explicit_dep_local_escape.tk", "E0455"),
    ("r3_task_helper_unannotated_escape", "fail/r3_task_helper_unannotated_escape.tk", "E0454"),
    ("r3_task_explicit_dep_free_escape", "fail/r3_task_explicit_dep_free_escape.tk", "E0455"),
    ("r3_task_helper_annotated_escape", "fail/r3_task_helper_annotated_escape.tk", "E0455"),
    ("r5_private_shape_rejected", "fail/r5_private_shape_rejected.tk", "E0405"),
]

POSITIVE_TESTS = [
    ("r3_task_return_positive", "pass/r3_task_return_positive.tk"),
    ("r3_task_explicit_dep_safe_await", "pass/r3_task_explicit_dep_safe_await.tk"),
    ("r5_trait_generic_param", "pass/r5_trait_generic_param.tk"),
    ("r5_trait_cross_module", "pass/r5_trait_cross_module.tk"),
]

AUTHORITATIVE_R3_R5_FIXTURES = {
    "fail/r3_task_direct_escape.tk": "130fd9d55b2352fa73d9053a31853af312cf344348fdd342b18b51c76071a231",
    "fail/r3_task_explicit_dep_free_escape.tk": "e047b184e92371772cdca9bc4fe4af79ad312b6fa628c963e1c56fd1bc882759",
    "fail/r3_task_explicit_dep_local_escape.tk": "d257ac12aa77155b1c3bf8b2f375d26d2ccdee39df95d6f415f28c95e84f3c24",
    "fail/r3_task_helper_annotated_escape.tk": "990ad6557dc597289263212bfb9d2425c654ca6333b79de96a6e391223b7fcdc",
    "fail/r3_task_helper_unannotated_escape.tk": "c8fd1e53ce6f2bad471ca8be35fb809c06883e429c4ec2f1af2e567e4d76de15",
    "fail/r5_private_shape_rejected.tk": "7f894a9368b9f8b4e3d3317b86e555cda280f986812a1a4024654c0ce295b7b9",
    "pass/r3_task_explicit_dep_safe_await.tk": "ffcaa4e742a804be7080e210294c9d44244f651b64e65f74d7b9bdb7db081eb8",
    "pass/r3_task_return_positive.tk": "2d943eec88d84da016b02141bd2348b2d9ef0c80d4ffcf807ec032c8fd21388f",
    "pass/r5_trait_cross_module.tk": "51f9d4d891d2788c313e78a2edca0aba8d8fba69487bfa6e51d45b16c9beec2a",
    "pass/r5_trait_generic_param.tk": "d24653367390f2277b11586d9ff34223bcb1dd5998b7ecd154db8b447d6d187f",
    "pass/r5_trait_module/mod.tk_lib": "a341b90504367d05363fa016766a1635209f9ae3e994b7b82566f9466f7ad409",
}
EXPECTED_R3_R5_FIXTURES_DIGEST = "dd7315ca33ee61a8303b392c57834178da27dcccd52d4a5ad9d812dba56ebd90"


def main():
    p = argparse.ArgumentParser(description="Verify R3/R5 bugfix controls against installed SDK")
    p.add_argument("--sdk", type=Path, required=True, help="Path to installed SDK root")
    p.add_argument("--revision", type=str, default=None, help="Expected candidate git revision SHA")
    p.add_argument("--version", type=str, default="v0.13.1", help="Expected version label")
    p.add_argument("--target", type=str, default=None, help="Platform target name")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r3_r5_fixtures directory")
    p.add_argument("--output", type=Path, required=True, help="Output directory for receipts")
    args = p.parse_args()

    sdk = args.sdk.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)

    # 1. Validate SDK identity descriptor
    identity_file = sdk / "sdk.json"
    if not identity_file.is_file():
        raise ValueError("SDK metadata missing: " + str(identity_file))
    identity = json.loads(identity_file.read_text(encoding="utf-8"))

    if (identity.get("schema") != "toka.sdk-identity" or
            type(identity.get("version")) is not int or identity["version"] != 1 or
            identity.get("version_label") != args.version or
            identity.get("source_dirty") is not False or
            identity.get("build_testing") is not False):
        raise ValueError("SDK metadata does not bind a clean frozen candidate for " + args.version)

    candidate_revision = identity.get("candidate_revision")
    if not isinstance(candidate_revision, str) or not re.fullmatch(r"[0-9a-f]{40}", candidate_revision):
        raise ValueError("Invalid candidate revision in sdk.json: " + str(candidate_revision))
    if args.revision and args.revision != candidate_revision:
        raise ValueError("Candidate revision mismatch: requested %s, found %s" % (args.revision, candidate_revision))

    target = args.target
    if not target:
        # Detect target from sdk directory name if format toka-<version>-<target>
        m = re.match(r"toka-v[0-9.]+-([a-z0-9_-]+)", sdk.name)
        target = m.group(1) if m else "unknown"

    # Verify tool binaries and versions
    tools = identity.get("tools", {})
    for tool_name in ("tokac", "toka", "tokafmt", "tokalsp"):
        binary = sdk / "bin" / (tool_name + (".exe" if sys.platform == "win32" else ""))
        if not binary.is_file():
            raise ValueError("Tool binary missing: " + str(binary))
        actual_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
        if actual_hash != tools.get(tool_name, {}).get("sha256"):
            raise ValueError("Tool binary digest mismatch for " + tool_name)

    # 2. Control script self-integrity and fixture manifest verification
    script_bytes = Path(__file__).resolve().read_bytes()
    control_script_sha256 = hashlib.sha256(script_bytes).hexdigest()
    bundle_manifest_file = Path(__file__).resolve().parent / "replay_bundle_manifest.json"
    if bundle_manifest_file.is_file():
        b_manifest = json.loads(bundle_manifest_file.read_text(encoding="utf-8"))
        expected_self_sha = b_manifest.get("modules", {}).get("test_r3_r5_installed.py")
        if expected_self_sha and control_script_sha256 != expected_self_sha:
            raise ValueError(f"Control script integrity mismatch: expected {expected_self_sha}, got {control_script_sha256}")

    fixtures_dir = args.fixtures.resolve() if args.fixtures else (Path(__file__).resolve().parent / "r3_r5_fixtures")
    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise ValueError("Fixture manifest missing: " + str(manifest_file))
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))

    if not isinstance(manifest, dict) or not manifest:
        raise ValueError("Fixture manifest must be a non-empty dict")
    if manifest != AUTHORITATIVE_R3_R5_FIXTURES:
        raise ValueError("Fixture manifest does not match authoritative fixtures mapping")

    hasher = hashlib.sha256()
    for rel_path, expected_hash in sorted(manifest.items()):
        fixture_file = fixtures_dir / rel_path
        if not fixture_file.is_file():
            raise ValueError("Fixture file missing: " + str(fixture_file))
        actual_hash = hashlib.sha256(fixture_file.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise ValueError(f"Fixture digest mismatch for {rel_path}: expected {expected_hash}, got {actual_hash}")
        hasher.update(rel_path.encode("utf-8"))
        hasher.update(expected_hash.encode("utf-8"))
    fixtures_digest = hasher.hexdigest()
    if fixtures_digest != EXPECTED_R3_R5_FIXTURES_DIGEST:
        raise ValueError(f"Fixtures digest mismatch: expected {EXPECTED_R3_R5_FIXTURES_DIGEST}, got {fixtures_digest}")

    # Setup execution environment
    env = {k: v for k, v in os.environ.items() if not k.startswith("TOKA")}
    env.update({
        "PATH": str(sdk / "bin") + os.pathsep + os.environ.get("PATH", ""),
        "TOKA_LIB": str(sdk / "lib"),
        "PYTHONDONTWRITEBYTECODE": "1",
    })

    tokac = sdk / "bin" / ("tokac.exe" if sys.platform == "win32" else "tokac")
    receipts = []
    checks = []

    def execute_command(name, argv, cwd):
        cmd_dir = out / ("command-" + name)
        cmd_dir.mkdir(parents=True, exist_ok=True)
        start_time = time.monotonic_ns()
        try:
            r = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, capture_output=True, timeout=120)
        except (OSError, subprocess.TimeoutExpired) as error:
            rec = {
                "name": name,
                "argv": list(map(str, argv)),
                "cwd": str(cwd),
                "exit_code": None,
                "termination": "not_started" if isinstance(error, OSError) else "timeout",
                "error": str(error),
            }
            (cmd_dir / "receipt.json").write_text(json.dumps(rec, indent=2))
            raise
        (cmd_dir / "stdout").write_bytes(r.stdout)
        (cmd_dir / "stderr").write_bytes(r.stderr)
        rec = {
            "name": name,
            "argv": list(map(str, argv)),
            "cwd": str(cwd),
            "exit_code": r.returncode if r.returncode >= 0 else None,
            "signal": -r.returncode if r.returncode < 0 else None,
            "execution_ms": (time.monotonic_ns() - start_time) / 1e6,
        }
        (cmd_dir / "receipt.json").write_text(json.dumps(rec, indent=2))
        receipts.append(rec)
        return r

    # 3. Negative Controls: 6 tests in 3 modes (check-only, object, IR)
    for test_id, rel_path, expected_diag in NEGATIVE_TESTS:
        fixture_path = fixtures_dir / rel_path

        # Mode A: check-only
        check_name = f"neg-{test_id}-check_only"
        r = execute_command(check_name, [str(tokac), "--check-only", str(fixture_path)], out)
        if r.returncode < 0:
            raise RuntimeError(f"Compiler crashed (signal {-r.returncode}) on negative check {test_id} (check-only)")
        if r.returncode != 1:
            raise RuntimeError(f"Expected compiler semantic rejection exit code 1, got {r.returncode} for {test_id} (check-only)")
        stderr_text = r.stderr.decode("utf-8", errors="replace")
        if f"error[{expected_diag}]" not in stderr_text:
            raise RuntimeError(f"Expected diagnostic {expected_diag} missing for {test_id} in check-only; got:\n{stderr_text}")
        checks.append({"name": check_name, "mode": "check_only", "result": "pass", "diagnostic": expected_diag})

        # Mode B: compile to object (-c -o)
        obj_name = f"neg-{test_id}-object"
        out_obj = out / f"test-{test_id}.o"
        if out_obj.exists():
            out_obj.unlink()
        r = execute_command(obj_name, [str(tokac), "-c", str(fixture_path), "-o", str(out_obj)], out)
        if r.returncode < 0:
            raise RuntimeError(f"Compiler crashed (signal {-r.returncode}) on negative check {test_id} (object mode)")
        if r.returncode != 1:
            raise RuntimeError(f"Expected compiler semantic rejection exit code 1, got {r.returncode} for {test_id} (object mode)")
        if out_obj.exists():
            raise RuntimeError(f"Object file emitted despite rejection for {test_id}")
        stderr_text = r.stderr.decode("utf-8", errors="replace")
        if f"error[{expected_diag}]" not in stderr_text:
            raise RuntimeError(f"Expected diagnostic {expected_diag} missing for {test_id} in object mode; got:\n{stderr_text}")
        checks.append({"name": obj_name, "mode": "object", "result": "pass", "diagnostic": expected_diag, "artifact_absent": True})

        # Mode C: emit LLVM IR (--emit-llvm -o)
        ir_name = f"neg-{test_id}-ir"
        out_ir = out / f"test-{test_id}.ll"
        if out_ir.exists():
            out_ir.unlink()
        r = execute_command(ir_name, [str(tokac), "--emit-llvm", str(fixture_path), "-o", str(out_ir)], out)
        if r.returncode < 0:
            raise RuntimeError(f"Compiler crashed (signal {-r.returncode}) on negative check {test_id} (IR mode)")
        if r.returncode != 1:
            raise RuntimeError(f"Expected compiler semantic rejection exit code 1, got {r.returncode} for {test_id} (IR mode)")
        if out_ir.exists():
            raise RuntimeError(f"LLVM IR file emitted despite rejection for {test_id}")
        stderr_text = r.stderr.decode("utf-8", errors="replace")
        if f"error[{expected_diag}]" not in stderr_text:
            raise RuntimeError(f"Expected diagnostic {expected_diag} missing for {test_id} in IR mode; got:\n{stderr_text}")
        checks.append({"name": ir_name, "mode": "ir", "result": "pass", "diagnostic": expected_diag, "artifact_absent": True})

    # 4. Positive Controls: 4 tests compiled and executed
    for test_id, rel_path in POSITIVE_TESTS:
        fixture_path = fixtures_dir / rel_path
        bin_target = out / f"pos-{test_id}.bin"
        if bin_target.exists():
            bin_target.unlink()

        # Compile
        compile_name = f"pos-{test_id}-compile"
        r = execute_command(compile_name, [str(tokac), str(fixture_path), "-o", str(bin_target)], out)
        if r.returncode != 0:
            raise RuntimeError(f"Positive control compilation failed for {test_id}: {r.stderr.decode('utf-8', errors='replace')}")
        if not bin_target.is_file():
            raise RuntimeError(f"Positive control binary was not produced for {test_id}")

        # Run
        run_name = f"pos-{test_id}-run"
        r = execute_command(run_name, [str(bin_target)], out)
        if r.returncode != 0:
            raise RuntimeError(f"Positive control execution failed for {test_id} with exit code {r.returncode}")
        checks.append({"name": run_name, "result": "pass", "exit_code": 0})

    # 5. Assemble receipt
    report = {
        "schema": "toka.r3-r5-installed-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": candidate_revision,
        "version_label": args.version,
        "target": target,
        "sdk_root": str(sdk),
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_r3_r5_installed.py",
        "control_script_sha256": control_script_sha256,
        "counts": {
            "total_checks": len(checks),
            "negative_checks": len(NEGATIVE_TESTS) * 3,
            "positive_checks": len(POSITIVE_TESTS),
            "passed": len(checks),
            "failed": 0,
        },
        "checks": checks,
        "receipts": receipts,
    }

    receipt_path = out / "r3_r5_receipt.json"
    receipt_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "schema": report["schema"],
        "version": report["version"],
        "result": report["result"],
        "candidate_revision": candidate_revision,
        "version_label": args.version,
        "target": target,
        "checks": len(checks),
    }))


if __name__ == "__main__":
    main()
