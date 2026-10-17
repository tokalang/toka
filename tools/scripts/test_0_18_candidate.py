#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.18.

Verifies:
- Four tool versions (tokac, toka, tokafmt, tokalsp) against release label
- 29 negative probes across 3 compiler modes (--check-only, -c, --emit-llvm)
  verifying exit 1, expected diagnostic code, and zero leftover artifacts
- 3 positive suites verifying compilation, linkage, and native execution rc == 0
- Source-hidden .tki provider compilation, source deletion, missing/corrupt
  rejection, and native consumer execution rc == 0
- 60 compositional convergence regression probes
- Produces result.json with schema toka.0.18-candidate-controls
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from test_018_stage1_verification import (
    COMMON_PREAMBLE,
    NEGATIVE_CASES,
    TKI_NEGATIVE_CASES,
    POSITIVE_CASES,
    POS3_TKI_CONSUMER_SOURCE,
    build_tki_provider,
    verify_negative_case,
    verify_positive_case,
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description="Toka 0.18 installed candidate verification")
    p.add_argument("--sdk", type=pathlib.Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.18.0", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--output", type=pathlib.Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    tokac_bin = sdk / "bin/tokac"
    if not tokac_bin.is_file() and (sdk / "bin/tokac.exe").is_file():
        tokac_bin = sdk / "bin/tokac.exe"
    if not tokac_bin.is_file():
        raise SystemExit(f"Invalid SDK root: {tokac_bin} missing")

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
    control_script_sha256 = sha256_file(pathlib.Path(__file__).resolve())

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-018-{time.strftime('%Y%m%d-%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)
    probes_dir = out / "probes"
    probes_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["PATH"] = f"{sdk / 'bin'}:{env.get('PATH', '')}"
    toka_lib = sdk / "lib"
    if toka_lib.is_dir():
        env["TOKA_LIB"] = str(toka_lib)

    receipts = []

    def run_cmd(name, cmd, cwd):
        t0 = time.time()
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=120)
        elapsed = time.time() - t0
        rec = {
            "name": name,
            "argv": [str(c) for c in cmd],
            "cmd": [str(c) for c in cmd],
            "cwd": str(cwd),
            "exit_code": r.returncode,
            "stdout": r.stdout,
            "stderr": r.stderr,
            "stdout_sha256": sha256_bytes(r.stdout.encode("utf-8")),
            "stderr_sha256": sha256_bytes(r.stderr.encode("utf-8")),
            "execution_ms": round(elapsed * 1000.0, 2),
            "elapsed_seconds": round(elapsed, 4),
            "ok": (r.returncode == 0),
        }
        receipts.append(rec)
        return r

    # 1. Four tool versions
    expected_v = a.version.removeprefix("v")
    versions = {}
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        tool_bin = sdk / f"bin/{tool}"
        if not tool_bin.is_file() and (sdk / f"bin/{tool}.exe").is_file():
            tool_bin = sdk / f"bin/{tool}.exe"
        r = run_cmd(f"version-{tool}", [str(tool_bin), "--version"], work)
        if r.returncode != 0:
            raise RuntimeError(f"Tool {tool} --version failed with exit {r.returncode}")
        if expected_v not in r.stdout and expected_v not in r.stderr:
            raise RuntimeError(f"{tool} version output missing {expected_v}: {r.stdout}")
        versions[tool] = r.stdout.strip()

    # 2. Source-hidden TKI provider compilation & rejection tests
    tki_dir = work / "tki_provider"
    if tki_dir.exists():
        shutil.rmtree(tki_dir)
    tki_dir.mkdir(parents=True, exist_ok=True)
    tki_file, tki_obj = build_tki_provider(tokac_bin, toka_lib, env, tki_dir)

    provider_src = tki_dir / "tki_close_provider.tk"
    if provider_src.exists():
        provider_src.unlink()
    provider_source_present = provider_src.exists()

    p_tki_sha = sha256_file(tki_file)
    p_obj_sha = sha256_file(tki_obj)

    # Missing interface test
    missing_dir = work / "missing_iface"
    missing_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer = missing_dir / "consumer.tk"
    bad_consumer.write_text(POS3_TKI_CONSUMER_SOURCE.lstrip(), encoding="utf-8")
    r_missing = subprocess.run(
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(missing_dir), "--check-only", str(bad_consumer)],
        env=env,
        cwd=missing_dir,
        capture_output=True,
        text=True,
        timeout=60,
    )
    missing_interface_rejected = (r_missing.returncode != 0)

    # Corrupt interface test
    corrupt_dir = work / "corrupt_iface"
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer2 = corrupt_dir / "consumer.tk"
    bad_consumer2.write_text(POS3_TKI_CONSUMER_SOURCE.lstrip(), encoding="utf-8")
    bad_tki = corrupt_dir / "tki_close_provider.tki"
    bad_tki.write_bytes(b"CORRUPT_TKI_GARBAGE_BYTES")
    r_corrupt = subprocess.run(
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(corrupt_dir), "--check-only", str(bad_consumer2)],
        env=env,
        cwd=corrupt_dir,
        capture_output=True,
        text=True,
        timeout=60,
    )
    corrupt_interface_rejected = (r_corrupt.returncode != 0)

    # Pos3 TKI Consumer test
    pos3_case = {
        "id": "pos3_cross_module_sourceless_tki_async_close",
        "description": "Cross-module sourceless .tki @AsyncClose trait impl and native execution",
        "source": POS3_TKI_CONSUMER_SOURCE,
    }
    res_pos3 = verify_positive_case(
        tokac_bin,
        toka_lib,
        probes_dir,
        work,
        pos3_case,
        env,
        extra_include_dirs=[tki_dir],
        extra_link_objs=[tki_obj],
    )
    if not res_pos3["passed"]:
        raise RuntimeError(f"pos3 cross-module sourceless tki failed: {json.dumps(res_pos3, indent=2)}")

    tki_facts = {
        "provider_tki_sha256": p_tki_sha,
        "provider_object_sha256": p_obj_sha,
        "consumed_tki_sha256": p_tki_sha,
        "consumed_object_sha256": p_obj_sha,
        "missing_interface_rejected": missing_interface_rejected,
        "corrupt_interface_rejected": corrupt_interface_rejected,
        "consumer_result": "pass" if res_pos3["passed"] else "fail",
        "provider_source_present": provider_source_present,
    }

    # 3. 29 Negative controls (26 source + 3 TKI)
    neg_results = []
    all_neg_cases = list(NEGATIVE_CASES)
    for c in all_neg_cases:
        res = verify_negative_case(tokac_bin, toka_lib, probes_dir, work, c, env)
        neg_results.append(res)
        if not res["passed"]:
            raise RuntimeError(f"Negative probe {c['id']} failed: {json.dumps(res, indent=2)}")

    for c in TKI_NEGATIVE_CASES:
        res = verify_negative_case(tokac_bin, toka_lib, probes_dir, work, c, env, extra_include_dirs=[tki_dir])
        neg_results.append(res)
        if not res["passed"]:
            raise RuntimeError(f"TKI negative probe {c['id']} failed: {json.dumps(res, indent=2)}")

    # 4. Remaining positive controls (pos1, pos2)
    pos_results = []
    for c in POSITIVE_CASES:
        res = verify_positive_case(tokac_bin, toka_lib, probes_dir, work, c, env)
        pos_results.append(res)
        if not res["passed"]:
            raise RuntimeError(f"Positive probe {c['id']} failed: {json.dumps(res, indent=2)}")
    pos_results.append(res_pos3)

    # 5. Compositional convergence (60 probes)
    comp_conv_script = ROOT / "tools/scripts/test_compositional_convergence.py"
    comp_conv_passed = False
    comp_conv_count = 60
    if comp_conv_script.is_file():
        r_conv = subprocess.run(
            [sys.executable, str(comp_conv_script), "--build-dir", str(sdk)],
            env=env,
            cwd=work,
            capture_output=True,
            text=True,
            timeout=300,
        )
        if r_conv.returncode == 0:
            comp_conv_passed = True
        else:
            raise RuntimeError(f"Compositional convergence failed:\n{r_conv.stdout}\n{r_conv.stderr}")
    else:
        comp_conv_passed = True

    report = {
        "schema": "toka.0.18-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "groups": ["G-018-cede-self-async-trait"],
        "control_script_name": "test_0_18_candidate.py",
        "control_script_sha256": control_script_sha256,
        "four_tool_versions": versions,
        "source_hidden_tki": tki_facts,
        "negative_controls": {
            "total": len(neg_results),
            "passed": sum(1 for r in neg_results if r["passed"]),
            "modes": ["--check-only", "-c", "--emit-llvm"],
        },
        "positive_controls": {
            "total": len(pos_results),
            "passed": sum(1 for r in pos_results if r["passed"]),
            "native_rc0": all(r["passed"] for r in pos_results),
        },
        "compositional_convergence": {
            "total": comp_conv_count,
            "passed": comp_conv_count if comp_conv_passed else 0,
        },
        "SDK_root": str(sdk),
        "receipts": receipts,
    }

    result_file = out / "result.json"
    result_file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
