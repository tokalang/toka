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

SCHEMA_VERSION = "2.0.0"

import concurrent.futures

def run_quick_scan(tokac_bin, run_dir, scratch_dir):
    env = os.environ.copy()
    env["TOKAC"] = tokac_bin
    env["TOKA_HANDLE_GRAMMAR_AUDIT"] = "1"
    env["TOKA_HANDLE_GRAMMAR_AUDIT_DIR"] = run_dir

    print(f"=== [Handle Grammar Audit] Starting Quick Scan (Schema v{SCHEMA_VERSION}) ===", flush=True)
    print(f"Audit log directory: {run_dir}", flush=True)

    ctest_dir = "build-debug" if os.path.exists("build-debug/CTestTestfile.cmake") else "build"

    quick_steps = [
        ("Handle Grammar Pass Suite", [
            "python3", "tools/scripts/test_pass.py",
            "tests/pass/g08_level2_param_signatures.tk",
            "tests/pass/g08_smart_ptr_borrow.tk",
            "tests/pass/g08_handle_grammar_parser_matrix.tk",
            "tests/pass/g08_handle_grammar_valid_matrix.tk"
        ]),
        ("Handle Grammar Fail Suite", [
            "python3", "tools/scripts/test_verify_fail.py",
            "tests/fail/handle_grammar_alias_exceeded_depth.tk",
            "tests/fail/handle_grammar_alias_illegal_order.tk",
            "tests/fail/handle_grammar_alias_mixed_raw.tk",
            "tests/fail/handle_grammar_array_element_illegal.tk",
            "tests/fail/handle_grammar_cast_illegal.tk",
            "tests/fail/handle_grammar_extern_param_illegal.tk",
            "tests/fail/handle_grammar_extern_return_illegal.tk",
            "tests/fail/handle_grammar_fn_param_illegal.tk",
            "tests/fail/handle_grammar_fn_return_illegal.tk",
            "tests/fail/handle_grammar_function_type_illegal.tk",
            "tests/fail/handle_grammar_generic_arg_illegal.tk",
            "tests/fail/handle_grammar_param_level1_redundant.tk",
            "tests/fail/handle_grammar_trait_method_param_illegal.tk",
            "tests/fail/handle_grammar_trait_method_return_illegal.tk",
            "tests/fail/handle_grammar_unsafe_block_illegal.tk"
        ]),
        ("Handle Grammar TKI Replay", [
            "bash", "tools/scripts/test_semantic_replay.sh"
        ], {"TOKAC": tokac_bin, "CASE_ROOT": "tests/semantics/tki_replay/cases/handle_001_level2_param_signatures"}),
        ("Handle Grammar Classifier CTest", [
            "ctest", "--test-dir", ctest_dir, "-R", "toka_handle_grammar_classifier", "--output-on-failure"
        ])
    ]

    for step in quick_steps:
        name = step[0]
        cmd = step[1]
        step_env = env.copy()
        if len(step) > 2:
            step_env.update(step[2])
        print(f"  Executing {name}...", flush=True)
        t0 = time.time()
        res = subprocess.run(cmd, env=step_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        dur = time.time() - t0
        if res.returncode == 0:
            print(f"  [PASS] {name} succeeded ({dur:.1f}s)", flush=True)
        else:
            print(f"  [FAIL] {name} failed with code {res.returncode}:\n{res.stdout}", flush=True)
            sys.exit(1)

def run_full_scan(tokac_bin, run_dir, scratch_dir, jobs=8, check_only=True):
    env = os.environ.copy()
    env["TOKAC"] = tokac_bin
    env["TOKA_HANDLE_GRAMMAR_AUDIT"] = "1"
    env["TOKA_HANDLE_GRAMMAR_AUDIT_DIR"] = run_dir

    print(f"=== [Handle Grammar Audit] Starting Full Scan (Schema v{SCHEMA_VERSION}) ===", flush=True)
    print(f"Audit log directory: {run_dir}", flush=True)
    print(f"Scratch output dir : {scratch_dir}", flush=True)
    print(f"Parallel workers   : {jobs}", flush=True)
    print(f"Scan check-only    : {check_only}", flush=True)

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
    standalone_pass = 0
    category_counts = defaultdict(int)
    unexpected_failures = []
    scan_timeout = []

    def compile_one_file(idx_and_f):
        idx, f = idx_and_f
        out_obj = os.path.join(scratch_dir, f"out_{idx}.o")
        if check_only:
            cmd = [tokac_bin, "--check-only", f, "-I", "lib", "-I", "."]
        else:
            cmd = [tokac_bin, "-c", f, "-I", "lib", "-I", ".", "-o", out_obj]
        try:
            res = subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=25)
            return (f, res.returncode, None)
        except subprocess.TimeoutExpired:
            return (f, -1, "timeout")

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [executor.submit(compile_one_file, item) for item in enumerate(tk_files)]
        done_count = 0
        for fut in concurrent.futures.as_completed(futures):
            f, rc, timeout = fut.result()
            done_count += 1
            if timeout:
                scan_timeout.append(f)
            elif rc == 0:
                standalone_pass += 1
            else:
                cat = is_expected_non_standalone(f)
                if cat:
                    category_counts[cat] += 1
                else:
                    unexpected_failures.append(f)
            if done_count % 200 == 0 or done_count == len(tk_files):
                print(f"  Processed {done_count}/{len(tk_files)} files...", flush=True)

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
    suites_dir = os.path.join(run_dir, "suites")
    os.makedirs(suites_dir, exist_ok=True)

    ctest_dir = "build-debug" if os.path.exists("build-debug/CTestTestfile.cmake") else "build"
    suites = [
        ("Pass Suite (test_pass.py)", ["python3", "tools/scripts/test_pass.py"], "pass_suite.log"),
        ("Fail Suite (test_verify_fail.py)", ["python3", "tools/scripts/test_verify_fail.py"], "fail_suite.log"),
        ("Conformance Suite (run_conformance.py)", ["python3", "tools/run_conformance.py"], "conformance_suite.log"),
        ("Semantic Replay Suite (test_semantic_replay.sh)", ["bash", "tools/scripts/test_semantic_replay.sh"], "semantic_replay.log"),
        ("Verify Warn Suite (test_verify_warn.py)", ["python3", "tools/scripts/test_verify_warn.py"], "verify_warn.log"),
        ("TKI Cache Validation (test_tki_cache_validation.sh)", ["bash", "tools/scripts/test_tki_cache_validation.sh"], "tki_cache_validation.log"),
        ("Cache Invalidation (test_semantic_cache_invalidation.sh)", ["bash", "tools/scripts/test_semantic_cache_invalidation.sh"], "cache_invalidation.log"),
        ("Mixed Core Cache (test_mixed_core_cache.sh)", ["bash", "tools/scripts/test_mixed_core_cache.sh"], "mixed_core_cache.log"),
        (f"CTest ({ctest_dir})", ["ctest", "--test-dir", ctest_dir, "--output-on-failure"], "ctest.log"),
    ]

    import hashlib
    def get_cmd_output(c):
        try:
            return subprocess.check_output(c, stderr=subprocess.STDOUT).decode("utf-8", errors="ignore").strip()
        except Exception:
            return ""

    git_commit = get_cmd_output(["git", "rev-parse", "HEAD"])
    git_status = get_cmd_output(["git", "status", "--porcelain"])

    suite_manifest = {
        "schema_version": SCHEMA_VERSION,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "git_commit": git_commit,
        "git_status_summary": git_status,
        "step1_scan": {
            "total_tk_files": len(tk_files),
            "standalone_pass": standalone_pass,
            "expected_non_standalone": expected_total,
            "unexpected_failures": len(unexpected_failures),
            "category_breakdown": dict(category_counts),
        },
        "step2_suites": []
    }

    suite_results = []
    for name, cmd, log_name in suites:
        print(f"  Executing {name}...", flush=True)
        log_path = os.path.join(suites_dir, log_name)
        start_t = time.time()
        with open(log_path, "w", encoding="utf-8") as lf:
            res = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            lf.write(res.stdout)
        duration = time.time() - start_t

        count_summary = "N/A"
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

    manifest_path = os.path.join(run_dir, "suite_execution_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as mf:
        json.dump(suite_manifest, mf, indent=2)

    shutil.rmtree(scratch_dir)
    print(f"\nScratch directory cleaned. Manifest saved to {manifest_path}.", flush=True)

    failed_suites = [(name, rc) for name, status, rc in suite_results if status != "PASSED"]
    if failed_suites:
        print(f"\n[FATAL] Handle Grammar Audit ABORTED: {len(failed_suites)} suite(s) failed:", flush=True)
        for name, rc in failed_suites:
            print(f"  • {name} (exit code {rc})", flush=True)
        sys.exit(1)

def aggregate_receipts(audit_dir, tokac_bin=None):
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

            if r.get("decision"):
                existing["decision"] = r.get("decision")
            if r.get("is_transient"):
                existing["is_transient"] = True
            if r.get("is_admitted"):
                existing["is_admitted"] = True

    entries = list(unique_canonical_entries.values())

    # Link Generated transients to RejectedSource if the type was rejected at source
    rejected_type_ids = set()
    for e in entries:
        if e.get("decision") == "RejectedSource":
            if e.get("type_id"):
                rejected_type_ids.add(e.get("type_id"))
            if e.get("type"):
                rejected_type_ids.add(e.get("type"))

    for e in entries:
        tid = e.get("type_id")
        tstr = e.get("type")
        if (tid in rejected_type_ids or tstr in rejected_type_ids) and e.get("decision") == "Observed":
            e["decision"] = "RejectedSource"
            e["is_transient"] = False

    N = len(entries)

    # Taxonomy calculations
    by_origin = defaultdict(list)
    by_decision = defaultdict(list)
    phase_combinations = defaultdict(list)
    phase_counts = defaultdict(int)
    by_reachability = defaultdict(list)
    by_fn_codegen = defaultdict(list)
    by_llvm_type = defaultdict(list)
    by_violation = defaultdict(list)

    admitted_sourcesurface = 0
    admitted_tkiimport = 0
    non_sfinae_transients = 0
    rejected_sfinae = 0
    rejected_source = 0
    instantiated_violations = 0
    llvm_lowered_violations = 0

    for e in entries:
        orig = e.get("syntax_origin", "Unknown")
        dec = e.get("decision", "Observed")
        is_transient = e.get("is_transient", False)
        is_admitted = e.get("is_admitted", False)
        is_inst = e.get("instantiated", False)
        llvm_ty = e.get("llvm_type_lowered", False)
        viol = e.get("violation", "Unknown")
        reach = e.get("reachability", "Unknown")
        cg = e.get("enclosing_fn_codegen", "Unknown")

        by_origin[orig].append(e)
        by_decision[dec].append(e)
        combo = tuple(sorted(list(e["phases"])))
        phase_combinations[combo].append(e)
        for ph in e["phases"]:
            phase_counts[ph] += 1
        by_reachability[reach].append(e)
        by_fn_codegen[cg].append(e)
        by_llvm_type[llvm_ty].append(e)
        by_violation[viol].append(e)

        if is_admitted and orig == "SourceSurface":
            admitted_sourcesurface += 1
        if is_admitted and orig == "TKIImport":
            admitted_tkiimport += 1
        if is_transient and dec != "RejectedSFINAE" and dec != "RejectedSource":
            non_sfinae_transients += 1
        if dec == "RejectedSFINAE":
            rejected_sfinae += 1
        if dec == "RejectedSource":
            rejected_source += 1
        if is_inst:
            instantiated_violations += 1
        if llvm_ty:
            llvm_lowered_violations += 1

    import hashlib

    def get_git_output(args):
        try:
            res = subprocess.run(["git"] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode != 0:
                print(f"[WARN] git {' '.join(args)} returned code {res.returncode}: {res.stderr.strip()}", file=sys.stderr)
                return ""
            return res.stdout.strip()
        except Exception as ex:
            print(f"[WARN] Git error: {ex}", file=sys.stderr)
            return ""

    git_commit = get_git_output(["rev-parse", "HEAD"])
    git_diff = get_git_output(["diff", "HEAD"])
    git_diff_hash = hashlib.sha256(git_diff.encode("utf-8")).hexdigest()

    # Dynamic compute controlled file hashes over all git tracked files
    tracked_files_raw = get_git_output(["ls-files"])
    tracked_files = [line.strip() for line in tracked_files_raw.splitlines() if line.strip()]
    controlled_file_hashes = {}
    for tf in sorted(tracked_files):
        if not os.path.isfile(tf):
            continue
        try:
            with open(tf, "rb") as f:
                controlled_file_hashes[tf] = hashlib.sha256(f.read()).hexdigest()
        except Exception:
            pass

    tokac_provenance = {}
    if tokac_bin and os.path.exists(tokac_bin):
        tokac_real = os.path.realpath(tokac_bin)
        try:
            with open(tokac_real, "rb") as f:
                tokac_hash = hashlib.sha256(f.read()).hexdigest()
            tokac_provenance = {
                "binary_path": tokac_real,
                "binary_sha256": tokac_hash
            }
        except Exception:
            tokac_provenance = {"binary_path": tokac_real}

    print("\n" + "="*80, flush=True)
    print(f"   HANDLE GRAMMAR AUTHORITATIVE RECEIPT & TAXONOMY (SCHEMA v{SCHEMA_VERSION})", flush=True)
    print("="*80, flush=True)
    print(f"Audit Log Directory               : {audit_dir}", flush=True)
    print(f"Process-Isolated Log Files Emitted : {len(audit_files)}", flush=True)
    print(f"Raw Events Emitted Across All Runs : {raw_event_count}", flush=True)
    print(f"Unique Canonical Entries (N)       : {N}", flush=True)
    print(f"Compiler Binary Path               : {tokac_provenance.get('binary_path', 'unknown')}", flush=True)
    print(f"Compiler Binary SHA-256            : {tokac_provenance.get('binary_sha256', 'unknown')}", flush=True)
    print(f"Git Commit Hash                    : {git_commit}", flush=True)
    print(f"Git Diff Hash (SHA-256)            : {git_diff_hash}", flush=True)
    print(f"Tracked Files Measured             : {len(controlled_file_hashes)}", flush=True)

    print(f"\n--- AUTHORITATIVE ADMISSION & LOWERING GATES (M0 THRESHOLDS) ---", flush=True)
    print(f"   • Admitted SourceSurface Violations : {admitted_sourcesurface:<4} [TARGET: 0]", flush=True)
    print(f"   • Admitted TKIImport Violations     : {admitted_tkiimport:<4} [TARGET: 0]", flush=True)
    print(f"   • Non-SFINAE Transients             : {non_sfinae_transients:<4} [TARGET: 0]", flush=True)
    print(f"   • Instantiated Violations           : {instantiated_violations:<4} [TARGET: 0]", flush=True)
    print(f"   • LLVM Lowered Violations           : {llvm_lowered_violations:<4} [TARGET: 0]", flush=True)
    print(f"   • Rejected SFINAE Evidence          : {rejected_sfinae:<4} [TARGET: 4]", flush=True)
    print(f"   • Rejected Compile-Fail Evidence    : {rejected_source:<4} [TARGET: 14]", flush=True)

    print(f"\n1. Decision Taxonomy Breakdown (N={N}):", flush=True)
    for k, v in sorted(by_decision.items()):
        print(f"   • {k:<30}: {len(v)}", flush=True)

    print(f"\n2. SyntaxOrigin Breakdown (N={N}):", flush=True)
    for k, v in sorted(by_origin.items()):
        print(f"   • {k:<30}: {len(v)}", flush=True)

    print(f"\n3. FormationPhase Combinations (N={N}):", flush=True)
    for k, v in sorted(phase_combinations.items()):
        print(f"   • {str(list(k)):<45}: {len(v)}", flush=True)

    print(f"\n4. Violation Category Breakdown (N={N}):", flush=True)
    for k, v in sorted(by_violation.items()):
        print(f"   • {k:<30}: {len(v)}", flush=True)

    print("\n" + "-"*80, flush=True)
    print("                 DETAILED ENTRY-BY-ENTRY AUDIT MATRIX                  ", flush=True)
    print("-"*80, flush=True)
    for e in sorted(entries, key=lambda x: (x.get("decision",""), x.get("syntax_origin",""), sorted(list(x.get("phases",[]))), x.get("loc",""))):
        print(f"• Key : {e['key']}", flush=True)
        print(f"  Type: {e['type']:<26} (ID: {e.get('type_id')}) Violation: {e['violation']}", flush=True)
        print(f"  Decision: {e.get('decision')} | Admitted: {e.get('is_admitted')} | Transient: {e.get('is_transient')}", flush=True)
        print(f"  Origin: {e.get('syntax_origin')} | Phases: {sorted(list(e['phases']))} | Loc: {e.get('loc')}", flush=True)
        print(f"  FnId: {e.get('fn_id')} | Member: {e.get('member')} | Template: {e.get('template')}", flush=True)
        print(f"  Instantiated: {e.get('instantiated')} | Reachability: {e.get('reachability')} | EnclosingFnCodeGen: {e.get('enclosing_fn_codegen')} | LLVMTypeLowered: {e.get('llvm_type_lowered')}\n", flush=True)

    # Save receipt manifest
    serializable_entries = []
    for e in entries:
        ed = dict(e)
        if isinstance(ed.get("phases"), (set, list)):
            ed["phases"] = sorted(list(ed["phases"]))
        serializable_entries.append(ed)

    receipt_data = {
        "schema_version": SCHEMA_VERSION,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "integrity": {
            "git_commit": git_commit,
            "git_diff_hash": git_diff_hash,
            "compiler": tokac_provenance,
            "controlled_file_count": len(controlled_file_hashes),
            "controlled_file_hashes": controlled_file_hashes
        },
        "total_canonical_entries": N,
        "metrics": {
            "admitted_sourcesurface_violations": admitted_sourcesurface,
            "admitted_tkiimport_violations": admitted_tkiimport,
            "non_sfinae_transients": non_sfinae_transients,
            "instantiated_violations": instantiated_violations,
            "llvm_lowered_violations": llvm_lowered_violations,
            "rejected_sfinae_evidence": rejected_sfinae,
            "rejected_compile_fail_evidence": rejected_source
        },
        "entries": serializable_entries
    }
    receipt_file = os.path.join(audit_dir, "authoritative_receipt_manifest.json")
    with open(receipt_file, "w", encoding="utf-8") as rf:
        json.dump(receipt_data, rf, indent=2)
    print(f"Authoritative receipt manifest saved to {receipt_file}.", flush=True)

    gate_failures = []
    if admitted_sourcesurface != 0:
        gate_failures.append(f"Admitted SourceSurface Violations: {admitted_sourcesurface} (expected 0)")
    if admitted_tkiimport != 0:
        gate_failures.append(f"Admitted TKIImport Violations: {admitted_tkiimport} (expected 0)")
    if non_sfinae_transients != 0:
        gate_failures.append(f"Non-SFINAE Transients: {non_sfinae_transients} (expected 0)")
    if instantiated_violations != 0:
        gate_failures.append(f"Instantiated Violations: {instantiated_violations} (expected 0)")
    if llvm_lowered_violations != 0:
        gate_failures.append(f"LLVM Lowered Violations: {llvm_lowered_violations} (expected 0)")
    if rejected_sfinae < 4:
        gate_failures.append(f"Rejected SFINAE Evidence: {rejected_sfinae} (expected >= 4)")
    if rejected_source < 14:
        gate_failures.append(f"Rejected Compile-Fail Evidence: {rejected_source} (expected >= 14)")

    if gate_failures:
        print(f"\n[FATAL] Handle Grammar Audit Gate Validation FAILED:", flush=True)
        for gf in gate_failures:
            print(f"  • {gf}", flush=True)
        sys.exit(1)
    else:
        print(f"\n[PASS] All Authoritative Handle Grammar Gates Passed Cleanly!", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Handle Grammar Morphic Audit Tool")
    parser.add_argument("--quick", action="store_true", help="Run quick pre-push verification of Handle Grammar matrices, classifier, and TKI replay")
    parser.add_argument("--full", action="store_true", help="Run full scan across all repository .tk files and 9 test suites")
    parser.add_argument("--scan", action="store_true", help="Alias for --full")
    parser.add_argument("--jobs", "-j", type=int, default=8, help="Number of parallel worker threads for file scanning (default: 8)")
    parser.add_argument("--check-only", action="store_true", default=True, help="Use --check-only in Step 1 file scanning (default: True)")
    parser.add_argument("--no-check-only", dest="check_only", action="store_false", help="Generate object files in Step 1 file scanning")
    parser.add_argument("--audit-dir", type=str, default="", help="Directory containing process-isolated audit JSONL files")
    default_tokac = "build/bin/tokac" if os.path.exists("build/bin/tokac") else "build-debug/bin/tokac"
    parser.add_argument("--tokac", type=str, default=default_tokac, help=f"Path to tokac binary (default: {default_tokac})")
    args = parser.parse_args()

    tokac_bin = os.path.abspath(args.tokac)
    if args.quick:
        run_id = int(time.time())
        audit_dir = f"/tmp/toka_audit_run_{run_id}"
        scratch_dir = f"/tmp/toka_scan_scratch_{run_id}"
        os.makedirs(audit_dir, exist_ok=True)
        os.makedirs(scratch_dir, exist_ok=True)
        run_quick_scan(tokac_bin, audit_dir, scratch_dir)
        aggregate_receipts(audit_dir, tokac_bin=tokac_bin)
    elif args.full or args.scan or not args.audit_dir:
        run_id = int(time.time())
        audit_dir = f"/tmp/toka_audit_run_{run_id}"
        scratch_dir = f"/tmp/toka_scan_scratch_{run_id}"
        os.makedirs(audit_dir, exist_ok=True)
        os.makedirs(scratch_dir, exist_ok=True)
        run_full_scan(tokac_bin, audit_dir, scratch_dir, jobs=args.jobs, check_only=args.check_only)
        aggregate_receipts(audit_dir, tokac_bin=tokac_bin)
    else:
        aggregate_receipts(args.audit_dir, tokac_bin=tokac_bin)
