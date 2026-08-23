#!/usr/bin/env python3
# Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
# Licensed under the Apache License, Version 2.0.
"""
Handle Grammar Morphic Audit Tool & Receipt Aggregator
Schema Version: 1.0.0
"""

import os
import sys
import shutil
import subprocess
import json
import time
import argparse
import datetime
from collections import defaultdict

SCHEMA_VERSION = "1.0.0"

def run_full_scan(tokac_bin, run_dir, scratch_dir):
    env = os.environ.copy()
    env["TOKAC"] = tokac_bin
    env["TOKA_HANDLE_GRAMMAR_AUDIT"] = "1"
    env["TOKA_HANDLE_GRAMMAR_AUDIT_DIR"] = run_dir

    print(f"=== [Handle Grammar Audit] Starting Full Scan (Schema v{SCHEMA_VERSION}) ===", flush=True)
    print(f"Audit log directory: {run_dir}", flush=True)
    print(f"Scratch output dir : {scratch_dir}", flush=True)

    # 1. Scan all repository .tk files
    tk_files = []
    for root, dirs, files in os.walk("."):
        if any(p in root for p in ["build", "tmp", ".git"]):
            continue
        for f in files:
            if f.endswith(".tk"):
                tk_files.append(os.path.join(root, f))

    def is_expected_non_standalone(file_path):
        # 1. Negative compile-fail / warn test suites
        if "/tests/fail/" in file_path or "/tests/warn/" in file_path:
            return "Expected Compile-Fail Test"
        # 2. Conformance diagnostic compile-fail tests
        if "/tests/conformance/diagnostics/" in file_path:
            return "Conformance Diagnostic Compile-Fail Test"
        # 3. Tooling diagnostic tests
        if "/tests/tooling/" in file_path:
            return "Tooling Diagnostic Test"
        # 4. Multi-file semantic cache & replay fixtures (including fail_main fixtures)
        if "/tests/semantics/" in file_path or "/tests/fixtures/" in file_path or "/tests/import_test/" in file_path or "/tests/runtime/" in file_path or "/tests/wasm/" in file_path:
            return "Multi-File Semantic / Integration Fixture"
        # 5. Foreign OS platform implementations (Linux/Wasi/Windows on macOS)
        if any(p in file_path for p in ["lib/sys/linux/", "lib/sys/wasi/", "lib/sys/windows/"]):
            return "Foreign OS Platform Implementation"
        # 6. Multi-file tool packages with inter-file module dependencies
        if file_path.startswith("./tools/") or file_path.startswith("tools/"):
            return "Multi-File Tool Package Submodule"
        # 7. Multi-file example packages with manifests
        if file_path.startswith("./examples/") or file_path.startswith("examples/"):
            return "Multi-File Example Project Package"
        # 8. Non-standalone test submodules
        if any(p in file_path for p in ["submodules/", "mod.tk", "_submodule.tk"]):
            return "Non-Standalone Test Submodule"
        return None

    print(f"\nStep 1: Compiling all {len(tk_files)} repository .tk files with isolated scratch outputs...", flush=True)
    processed = 0
    standalone_pass = 0
    category_counts = defaultdict(int)
    unexpected_failures = []
    scan_timeout = []

    for f in tk_files:
        out_obj = os.path.join(scratch_dir, f"out_{processed}.o")
        cmd = [tokac_bin, "-c", f, "-I", "lib", "-I", ".", "-o", out_obj]
        try:
            res = subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=25)
            if res.returncode == 0:
                standalone_pass += 1
            else:
                category = is_expected_non_standalone(f)
                if category:
                    category_counts[category] += 1
                else:
                    unexpected_failures.append(f)
        except subprocess.TimeoutExpired:
            scan_timeout.append(f)
        processed += 1
        if processed % 200 == 0 or processed == len(tk_files):
            print(f"  Processed {processed}/{len(tk_files)} files...", flush=True)

    expected_total = sum(category_counts.values())
    print(f"  Source compilation scan metrics:", flush=True)
    print(f"    • Total .tk Files Scanned       : {len(tk_files)}", flush=True)
    print(f"    • Standalone Clean Compilation  : {standalone_pass}", flush=True)
    print(f"    • Expected Negative / Submodules: {expected_total}", flush=True)
    for cat, count in sorted(category_counts.items()):
        print(f"      - {cat:<36}: {count}", flush=True)
    print(f"    • Unexpected Standalone Failures: {len(unexpected_failures)}", flush=True)
    print(f"    • Timeouts                      : {len(scan_timeout)}", flush=True)

    if scan_timeout or unexpected_failures:
        if scan_timeout:
            print(f"[FATAL] Handle Grammar Audit ABORTED: compilations timed out: {scan_timeout[:5]}", flush=True)
        if unexpected_failures:
            print(f"[FATAL] Handle Grammar Audit ABORTED: unexpected failures in repository: {unexpected_failures[:10]}", flush=True)
        shutil.rmtree(scratch_dir)
        sys.exit(1)

    # 2. Run All Test Suites with Strict Returncode Checking
    print("\nStep 2: Running verification and conformance suites with strict exit code validation...", flush=True)
    suites_dir = os.path.join(audit_dir, "suites")
    os.makedirs(suites_dir, exist_ok=True)

    suites = [
        ("Pass Suite (test_pass.py)", ["python3", "tools/scripts/test_pass.py"], "pass_suite.log"),
        ("Fail Suite (test_verify_fail.py)", ["python3", "tools/scripts/test_verify_fail.py"], "fail_suite.log"),
        ("Conformance Suite (run_conformance.py)", ["python3", "tools/run_conformance.py"], "conformance_suite.log"),
        ("Semantic Replay Suite (test_semantic_replay.sh)", ["bash", "tools/scripts/test_semantic_replay.sh"], "semantic_replay.log"),
        ("Verify Warn Suite (test_verify_warn.py)", ["python3", "tools/scripts/test_verify_warn.py"], "verify_warn.log"),
        ("TKI Cache Validation (test_tki_cache_validation.sh)", ["bash", "tools/scripts/test_tki_cache_validation.sh"], "tki_cache_validation.log"),
        ("Cache Invalidation (test_semantic_cache_invalidation.sh)", ["bash", "tools/scripts/test_semantic_cache_invalidation.sh"], "cache_invalidation.log"),
        ("Mixed Core Cache (test_mixed_core_cache.sh)", ["bash", "tools/scripts/test_mixed_core_cache.sh"], "mixed_core_cache.log"),
        ("CTest (build-debug)", ["ctest", "--test-dir", "build-debug", "--output-on-failure"], "ctest.log"),
    ]

    import hashlib
    def get_cmd_output(c):
        try:
            return subprocess.check_output(c, stderr=subprocess.STDOUT).decode("utf-8", errors="ignore").strip()
        except Exception:
            return ""

    git_commit = get_cmd_output(["git", "rev-parse", "HEAD"])
    git_status = get_cmd_output(["git", "status", "--porcelain"])
    git_diff = get_cmd_output(["git", "diff", "HEAD"])
    git_diff_hash = hashlib.sha256(git_diff.encode("utf-8")).hexdigest()

    def file_sha256(path):
        if not os.path.isfile(path):
            return "missing"
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()

    controlled_files_hashes = {}
    key_files = [
        "CMakeLists.txt",
        "include/toka/HandleGrammarAudit.h",
        "include/toka/Type.h",
        "src/Type.cpp",
        "src/main.cpp",
        "src/Sema/Sema.cpp",
        "src/Sema/Sema_Stmt.cpp",
        "src/Sema/Sema_Template.cpp",
        "tests/HandleGrammarClassifierTest.cpp",
        "tests/pass/g03_nullable_raw_pointer_matrix.tk",
        "tests/pass/g08_handle_grammar_valid_matrix.tk",
        "tools/scripts/audit_handle_grammar.py",
    ]
    for kf in key_files:
        controlled_files_hashes[kf] = file_sha256(kf)

    hasher = hashlib.sha256()
    hasher.update(git_diff.encode("utf-8"))
    for kf in sorted(controlled_files_hashes.keys()):
        hasher.update(f"{kf}:{controlled_files_hashes[kf]}\n".encode("utf-8"))
    workspace_integrity_digest = hasher.hexdigest()

    suite_manifest = {
        "timestamp": datetime.datetime.now().isoformat(),
        "git_commit": git_commit,
        "git_diff_hash": git_diff_hash,
        "workspace_integrity_digest": workspace_integrity_digest,
        "controlled_files_hashes": controlled_files_hashes,
        "git_status_summary": git_status,
        "step1_source_scan": {
            "total_tk_files": len(tk_files),
            "standalone_pass": standalone_pass,
            "expected_negative_total": expected_total,
            "unexpected_failures": unexpected_failures,
            "timeouts": scan_timeout,
            "category_counts": dict(category_counts),
        },
        "step2_suites": []
    }

    suite_results = []
    for name, cmd, log_name in suites:
        print(f"  Executing {name}...", flush=True)
        t0 = time.time()
        res = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        duration = time.time() - t0
        log_path = os.path.join(suites_dir, log_name)
        with open(log_path, "w", encoding="utf-8") as lf:
            lf.write(res.stdout)

        # Extract test counts if present
        count_summary = ""
        for line in res.stdout.splitlines():
            if "Passed:" in line or "Total Passed:" in line or "100% tests passed" in line:
                count_summary = line.strip()

        suite_entry = {
            "name": name,
            "cmd": cmd,
            "returncode": res.returncode,
            "status": "PASSED" if res.returncode == 0 else "FAILED",
            "duration_seconds": round(duration, 2),
            "log_file": log_name,
            "test_summary": count_summary,
        }
        suite_manifest["step2_suites"].append(suite_entry)

        if res.returncode == 0:
            suite_results.append((name, "PASSED", res.returncode))
            print(f"  [PASS] {name} succeeded ({duration:.1f}s) {count_summary}", flush=True)
        else:
            suite_results.append((name, "FAILED", res.returncode))
            print(f"  [FAIL] {name} exited with code {res.returncode}", flush=True)

    manifest_path = os.path.join(audit_dir, "suite_execution_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as mf:
        json.dump(suite_manifest, mf, indent=2)

    # 3. Clean scratch
    shutil.rmtree(scratch_dir)
    print(f"\nScratch directory cleaned. Manifest saved to {manifest_path}.", flush=True)

    # 4. Strict fail-closed gate: if any suite failed, abort immediately with non-zero exit code
    failed_suites = [(name, rc) for name, status, rc in suite_results if status != "PASSED"]
    if failed_suites:
        print(f"\n[FATAL] Handle Grammar Audit ABORTED: {len(failed_suites)} suite(s) failed:", flush=True)
        for name, rc in failed_suites:
            print(f"  • {name} (exit code {rc})", flush=True)
        sys.exit(1)

def aggregate_receipts(audit_dir):
    audit_files = [os.path.join(audit_dir, f) for f in os.listdir(audit_dir) if f.endswith(".jsonl")]
    raw_events = []
    for af in audit_files:
        with open(af, "r") as fp:
            for line in fp:
                line = line.strip()
                if line:
                    try:
                        raw_events.append(json.loads(line))
                    except Exception:
                        pass

    raw_event_count = len(raw_events)

    # Canonical deduplication based strictly on record["key"]
    unique_canonical_entries = {}
    for r in raw_events:
        key = r.get("key")
        if not key:
            continue
        if key not in unique_canonical_entries:
            entry = dict(r)
            entry["phases"] = set(r.get("phases", []))
            unique_canonical_entries[key] = entry
        else:
            existing = unique_canonical_entries[key]
            for ph in r.get("phases", []):
                existing["phases"].add(ph)

            if r.get("reachability") == "Reachable":
                existing["reachability"] = "Reachable"
            elif existing.get("reachability") == "Unknown" and r.get("reachability") != "Unknown":
                existing["reachability"] = r.get("reachability")

            if r.get("enclosing_fn_codegen") == "Lowered":
                existing["enclosing_fn_codegen"] = "Lowered"
            elif existing.get("enclosing_fn_codegen") == "Unknown" and r.get("enclosing_fn_codegen") != "Unknown":
                existing["enclosing_fn_codegen"] = r.get("enclosing_fn_codegen")

            if r.get("llvm_type_lowered"):
                existing["llvm_type_lowered"] = True

            if r.get("instantiated"):
                existing["instantiated"] = True

    entries = list(unique_canonical_entries.values())
    N = len(entries)

    # All metrics computed strictly from `entries`
    by_origin = defaultdict(list)
    phase_combinations = defaultdict(list)
    phase_counts = defaultdict(int)
    by_reachability = defaultdict(list)
    by_fn_codegen = defaultdict(list)
    by_llvm_type = defaultdict(list)
    by_violation = defaultdict(list)
    instantiated_count = 0

    for e in entries:
        orig = e.get("syntax_origin", "Unknown")
        reach = e.get("reachability", "Unknown")
        cg = e.get("enclosing_fn_codegen", "Unknown")
        llvm_ty = e.get("llvm_type_lowered", False)
        viol = e.get("violation", "Unknown")
        if e.get("instantiated"):
            instantiated_count += 1

        by_origin[orig].append(e)
        combo = tuple(sorted(list(e["phases"])))
        phase_combinations[combo].append(e)
        for ph in e["phases"]:
            phase_counts[ph] += 1
        by_reachability[reach].append(e)
        by_fn_codegen[cg].append(e)
        by_llvm_type[llvm_ty].append(e)
        by_violation[viol].append(e)

    reach_cnt = len(by_reachability["Reachable"])
    unreach_cnt = len(by_reachability["Unreachable"])
    unk_cnt = len(by_reachability["Unknown"])

    cg_lowered = len(by_fn_codegen["Lowered"])
    cg_not_lowered = len(by_fn_codegen["NotLowered"])
    cg_unk = len(by_fn_codegen["Unknown"])

    llvm_true = len(by_llvm_type[True])
    llvm_false = len(by_llvm_type[False])

    print("\n" + "="*80, flush=True)
    print(f"   HANDLE GRAMMAR CANONICAL AUDIT REPORT (SCHEMA v{SCHEMA_VERSION})", flush=True)
    print("="*80, flush=True)
    print(f"Audit Log Directory               : {audit_dir}", flush=True)
    print(f"Process-Isolated Log Files Emitted : {len(audit_files)}", flush=True)
    print(f"Raw Events Emitted Across All Runs : {raw_event_count}", flush=True)
    print(f"Unique Canonical Entries (N)       : {N}", flush=True)

    print(f"\n1. SyntaxOrigin Breakdown (from unique entries, N={N}):", flush=True)
    for k, v in sorted(by_origin.items()):
        print(f"   • {k:<30}: {len(v)}", flush=True)
    print(f"   [Sum = {sum(len(v) for v in by_origin.values())} == {N}]", flush=True)

    print(f"\n2. FormationPhase Combinations (from unique entries, N={N}):", flush=True)
    for k, v in sorted(phase_combinations.items()):
        print(f"   • {str(list(k)):<45}: {len(v)}", flush=True)
    print(f"   [Sum = {sum(len(v) for v in phase_combinations.values())} == {N}]", flush=True)

    print(f"\n3. Individual Formation Phase Coverage (from unique entries, N={N}):", flush=True)
    for k, v in sorted(phase_counts.items()):
        print(f"   • {k:<30}: {v}", flush=True)

    print(f"\n4. Reachability Status (from unique entries, N={N}):", flush=True)
    print(f"   • Reachable                     : {reach_cnt}", flush=True)
    print(f"   • Unreachable                   : {unreach_cnt}", flush=True)
    print(f"   • Unknown                       : {unk_cnt}", flush=True)
    print(f"   [Sum = {reach_cnt + unreach_cnt + unk_cnt} == {N}]", flush=True)

    print(f"\n5. Enclosing Function CodeGen Status (from unique entries, N={N}):", flush=True)
    print(f"   • Lowered                       : {cg_lowered}", flush=True)
    print(f"   • NotLowered                    : {cg_not_lowered}", flush=True)
    print(f"   • Unknown                       : {cg_unk}", flush=True)
    print(f"   [Sum = {cg_lowered + cg_not_lowered + cg_unk} == {N}]", flush=True)

    print(f"\n6. Actual LLVM Type Lowered (Canonical getLLVMType Receipt, N={N}):", flush=True)
    print(f"   • True (Lowered to LLVM Type)   : {llvm_true}", flush=True)
    print(f"   • False (Never entered getLLVMType): {llvm_false}", flush=True)
    print(f"   [Sum = {llvm_true + llvm_false} == {N}]", flush=True)

    print(f"\n7. Monomorphized Instantiated Status (from unique entries, N={N}):", flush=True)
    print(f"   • Instantiated == True          : {instantiated_count}", flush=True)
    print(f"   • Instantiated == False         : {N - instantiated_count}", flush=True)
    print(f"   [Sum = {instantiated_count + (N - instantiated_count)} == {N}]", flush=True)

    print(f"\n8. Violation Category Breakdown (from unique entries, N={N}):", flush=True)
    for k, v in sorted(by_violation.items()):
        print(f"   • {k:<30}: {len(v)}", flush=True)
    print(f"   [Sum = {sum(len(v) for v in by_violation.values())} == {N}]", flush=True)

    print("\n" + "-"*80, flush=True)
    print("                 DETAILED ENTRY-BY-ENTRY AUDIT MATRIX                  ", flush=True)
    print("-"*80, flush=True)
    for e in sorted(entries, key=lambda x: (x.get("syntax_origin",""), sorted(list(x.get("phases",[]))), x.get("loc",""))):
        print(f"• Key : {e['key']}", flush=True)
        print(f"  Type: {e['type']:<26} (ID: {e.get('type_id')}) Violation: {e['violation']}", flush=True)
        print(f"  Origin: {e.get('syntax_origin')} | Phases: {sorted(list(e['phases']))} | Loc: {e.get('loc')}", flush=True)
        print(f"  FnId: {e.get('fn_id')} | Member: {e.get('member')} | Template: {e.get('template')}", flush=True)
        print(f"  Instantiated: {e.get('instantiated')} | Reachability: {e.get('reachability')} | EnclosingFnCodeGen: {e.get('enclosing_fn_codegen')} | LLVMTypeLowered: {e.get('llvm_type_lowered')}\n", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Handle Grammar Morphic Audit Tool")
    parser.add_argument("--scan", action="store_true", help="Run full scan across all 1604 .tk files and test suites")
    parser.add_argument("--audit-dir", type=str, default="", help="Directory containing process-isolated audit JSONL files")
    parser.add_argument("--tokac", type=str, default="build-debug/bin/tokac", help="Path to tokac binary")
    args = parser.parse_args()

    tokac_bin = os.path.abspath(args.tokac)
    if args.scan or not args.audit_dir:
        run_id = int(time.time())
        audit_dir = f"/tmp/toka_audit_run_{run_id}"
        scratch_dir = f"/tmp/toka_scan_scratch_{run_id}"
        os.makedirs(audit_dir, exist_ok=True)
        os.makedirs(scratch_dir, exist_ok=True)
        run_full_scan(tokac_bin, audit_dir, scratch_dir)
        aggregate_receipts(audit_dir)
    else:
        aggregate_receipts(args.audit_dir)
