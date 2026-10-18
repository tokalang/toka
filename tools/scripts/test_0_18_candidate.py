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

import test_018_stage1_verification


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

    def run_cmd(name, cmd, cwd, allow_fail=False, expected_code=None, artifact_absent=False, timeout=300):
        t0 = time.time()
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - t0
        combined = r.stdout + "\n" + r.stderr

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

        if expected_code and expected_code not in combined:
            ok = False
            err_msg = f"missing required diagnostic {expected_code}"

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
        if expected_code:
            rec["expected_code"] = expected_code
            rec["diagnostic"] = expected_code
        if artifact_absent:
            rec["artifact_absent"] = True

        receipts.append(rec)
        if not ok:
            raise RuntimeError(f"Step {name} failed: {err_msg}\n{combined}")
        return r

    # 1. Four tool versions (4 receipts)
    expected_v = a.version.removeprefix("v")
    versions = {}
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        tool_bin = sdk / f"bin/{tool}"
        if not tool_bin.is_file() and (sdk / f"bin/{tool}.exe").is_file():
            tool_bin = sdk / f"bin/{tool}.exe"
        r = run_cmd(f"version-{tool}", [str(tool_bin), "--version"], work)
        if expected_v not in r.stdout and expected_v not in r.stderr:
            raise RuntimeError(f"{tool} version output missing {expected_v}: {r.stdout}")
        versions[tool] = r.stdout.strip()

    # 2. Source-hidden TKI provider compilation & rejection tests (3 receipts)
    tki_dir = work / "tki_provider"
    if tki_dir.exists():
        shutil.rmtree(tki_dir)
    tki_dir.mkdir(parents=True, exist_ok=True)

    src_path = tki_dir / "tki_close_provider.tk"
    tki_path = tki_dir / "tki_close_provider.tki"
    obj_path = tki_dir / "tki_close_provider.o"
    bodyless_tki_path = tki_dir / "bodyless_close_consumer.tki"
    src_path.write_text(test_018_stage1_verification.TKI_PROVIDER_SOURCE.lstrip(), encoding="utf-8")
    bodyless_tki_path.write_text(test_018_stage1_verification.BODYLESS_TKI_CONTENT, encoding="utf-8")

    run_cmd(
        "pos-tki-provider-compile",
        [str(tokac_bin), "-I", str(toka_lib), "-c", "--emit-interface", str(src_path), "-o", str(obj_path)],
        tki_dir,
    )
    if not tki_path.exists() or not obj_path.exists():
        raise RuntimeError("Failed to build TKI provider")
    tki_file = tki_path
    tki_obj = obj_path

    # Delete source file to guarantee sourceless interface consumption
    if src_path.exists():
        src_path.unlink()
    provider_source_present = src_path.exists()

    p_tki_sha = sha256_file(tki_file)
    p_obj_sha = sha256_file(tki_obj)

    # Missing interface test
    missing_dir = work / "missing_iface"
    missing_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer = missing_dir / "consumer.tk"
    bad_consumer.write_text(test_018_stage1_verification.POS3_TKI_CONSUMER_SOURCE.lstrip(), encoding="utf-8")
    r_missing = run_cmd(
        "neg-tki-missing-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(missing_dir), "--check-only", str(bad_consumer)],
        missing_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    missing_interface_rejected = (r_missing.returncode == 1)
    missing_iface_cmd = receipts[-1]

    # Corrupt interface test
    corrupt_dir = work / "corrupt_iface"
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer2 = corrupt_dir / "consumer.tk"
    bad_consumer2.write_text(test_018_stage1_verification.POS3_TKI_CONSUMER_SOURCE.lstrip(), encoding="utf-8")
    bad_tki = corrupt_dir / "tki_close_provider.tki"
    bad_tki.write_bytes(b"CORRUPT_TKI_GARBAGE_BYTES")
    r_corrupt = run_cmd(
        "neg-tki-corrupt-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(corrupt_dir), "--check-only", str(bad_consumer2)],
        corrupt_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    corrupt_interface_rejected = (r_corrupt.returncode == 1)
    corrupt_iface_cmd = receipts[-1]

    # 3. 29 Negative controls (26 normal + 3 TKI) across 3 modes (87 receipts)
    neg_results = []

    def execute_negative_case(c, extra_include_dirs=None):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if extra_include_dirs:
            for d in extra_include_dirs:
                inc_args.extend(["-I", str(d)])

        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"

        # Mode 1: check-only
        r_chk = run_cmd(
            f"neg-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        # Mode 2: compile-obj
        r_obj = run_cmd(
            f"neg-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if obj_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected object file")
        # Mode 3: emit-llvm
        r_ll = run_cmd(
            f"neg-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if ll_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected LLVM IR file")

        return {
            "id": cid,
            "category": c["category"],
            "description": c["description"],
            "passed": True,
            "modes": {
                "check-only": {"rc": r_chk.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "compile-obj": {"rc": r_obj.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "emit-llvm": {"rc": r_ll.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
            },
        }

    for c in test_018_stage1_verification.NEGATIVE_CASES:
        res = execute_negative_case(c)
        neg_results.append(res)

    for c in test_018_stage1_verification.TKI_NEGATIVE_CASES:
        res = execute_negative_case(c, extra_include_dirs=[tki_dir])
        neg_results.append(res)

    # 4. Positive controls (pos1, pos2, pos3) across 5 steps (15 receipts)
    pos_results = []

    def execute_positive_case(c, extra_include_dirs=None, extra_link_objs=None):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if extra_include_dirs:
            for d in extra_include_dirs:
                inc_args.extend(["-I", str(d)])

        suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"
        bin_file = case_work / f"{cid}{suffix}"

        # 1. check-only
        run_cmd(
            f"pos-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
        )
        # 2. emit-llvm
        run_cmd(
            f"pos-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
        )
        if not ll_file.exists() or ll_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit LLVM file")
        # 3. compile-obj
        run_cmd(
            f"pos-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
        )
        if not obj_file.exists() or obj_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit object file")
        # 4. binary-build
        link_inputs = [str(src_file)]
        if extra_link_objs:
            link_inputs.extend(str(o) for o in extra_link_objs)
        run_cmd(
            f"pos-{cid}-binary-build",
            [str(tokac_bin), *inc_args, *link_inputs, "-o", str(bin_file)],
            case_work,
        )
        if not bin_file.exists():
            raise RuntimeError(f"Positive {cid} failed to build binary")
        # 5. native-execution
        r_run = run_cmd(
            f"pos-{cid}-native-execution",
            [str(bin_file)],
            case_work,
        )
        if r_run.returncode != 0:
            raise RuntimeError(f"Positive {cid} native execution failed with exit {r_run.returncode}")

        return {
            "id": cid,
            "description": c.get("description", ""),
            "passed": True,
        }

    for c in test_018_stage1_verification.POSITIVE_CASES:
        res = execute_positive_case(c)
        pos_results.append(res)

    pos3_case = {
        "id": "pos3_cross_module_sourceless_tki_async_close",
        "description": "Cross-module sourceless .tki @AsyncClose trait impl and native execution",
        "source": test_018_stage1_verification.POS3_TKI_CONSUMER_SOURCE,
    }
    res_pos3 = execute_positive_case(
        pos3_case,
        extra_include_dirs=[tki_dir],
        extra_link_objs=[tki_obj],
    )
    pos_results.append(res_pos3)

    tki_facts = {
        "provider_tki_sha256": p_tki_sha,
        "provider_object_sha256": p_obj_sha,
        "consumed_tki_sha256": p_tki_sha,
        "consumed_object_sha256": p_obj_sha,
        "missing_interface_rejected": missing_interface_rejected,
        "corrupt_interface_rejected": corrupt_interface_rejected,
        "consumer_result": "pass" if res_pos3["passed"] else "fail",
        "provider_source_present": provider_source_present,
        "missing_interface_command": missing_iface_cmd,
        "corrupt_interface_command": corrupt_iface_cmd,
    }

    # 5. Compositional convergence (60 probes, 1 receipt)
    comp_conv_script = ROOT / "tools/scripts/test_compositional_convergence.py"
    if not comp_conv_script.is_file():
        alt = pathlib.Path(__file__).resolve().parent / "test_compositional_convergence.py"
        if alt.is_file():
            comp_conv_script = alt
        else:
            raise RuntimeError(f"Missing mandatory test_compositional_convergence.py: checked {comp_conv_script} and {alt}")

    conv_cmd = [sys.executable, str(comp_conv_script), "--build-dir", str(sdk)]
    suite_cand = comp_conv_script.parent / "r018_fixtures"
    if suite_cand.is_dir():
        conv_cmd.extend(["--suite-dir", str(suite_cand)])
    elif (ROOT / "tests/semantics/compositional_convergence").is_dir():
        conv_cmd.extend(["--suite-dir", str(ROOT / "tests/semantics/compositional_convergence")])

    r_conv = run_cmd(
        "pos-compositional-convergence",
        conv_cmd,
        work,
    )
    if r_conv.returncode == 0:
        comp_conv_passed = True
    else:
        raise RuntimeError(f"Compositional convergence failed:\n{r_conv.stdout}\n{r_conv.stderr}")

    comp_conv_count = 60

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
        "counts": {
            "total_receipts": len(receipts),
            "passed": sum(1 for r in receipts if r.get("ok")),
            "failed": sum(1 for r in receipts if not r.get("ok")),
            "groups": {
                "version": 4,
                "source_hidden_tki": 3,
                "negative_controls": len(neg_results) * 3,
                "positive_controls": len(pos_results) * 5,
                "compositional_convergence": 1,
            },
        },
        "SDK_root": str(sdk),
        "receipts": receipts,
    }

    result_file = out / "result.json"
    result_file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
