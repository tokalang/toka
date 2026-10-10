#!/usr/bin/env python3
"""Maintained repository-level directed gate for G2 Framed Transport & TaskHandle lifecycle."""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/g2_framed_transport"
PASS_TESTS = ROOT / "tests/pass"


def resolve_build_paths(build_dir_arg=None):
    candidates = []
    if build_dir_arg:
        candidates.append(Path(build_dir_arg).resolve())
    candidates.extend([
        ROOT.parent / "builds/fix-r3-task-escape",
        ROOT / "build",
        ROOT / "build/ninja",
    ])
    for b in candidates:
        tokac = b / "bin/tokac"
        rt_o = b / "lib/sys/toka_rt.o"
        if tokac.is_file() and rt_o.is_file():
            return b, tokac, rt_o
    raise RuntimeError(f"Could not locate valid tokac and toka_rt.o in: {[str(c) for c in candidates]}")


def run_case(name, tk_file, tokac, rt_o, work_dir, env, timeout=30):
    bin_file = work_dir / f"{name}.bin"
    comp_cmd = [str(tokac), str(tk_file), str(rt_o), "-o", str(bin_file)]
    t0 = time.monotonic()
    comp = subprocess.run(comp_cmd, cwd=work_dir, env=env, capture_output=True, text=True, timeout=timeout)
    comp_ms = (time.monotonic() - t0) * 1000

    record = {
        "name": name,
        "source": str(tk_file.relative_to(ROOT)),
        "compile": {
            "argv": comp_cmd,
            "cwd": str(work_dir),
            "exit_code": comp.returncode,
            "stdout": comp.stdout,
            "stderr": comp.stderr,
            "duration_ms": comp_ms
        }
    }

    if comp.returncode != 0:
        record["run"] = None
        record["passed"] = False
        return record, None

    t1 = time.monotonic()
    run = subprocess.run([str(bin_file)], cwd=work_dir, env=env, capture_output=True, text=True, timeout=timeout)
    run_ms = (time.monotonic() - t1) * 1000

    record["run"] = {
        "argv": [str(bin_file)],
        "cwd": str(work_dir),
        "exit_code": run.returncode,
        "stdout": run.stdout,
        "stderr": run.stderr,
        "duration_ms": run_ms
    }
    record["passed"] = (run.returncode == 0)
    bin_file.unlink(missing_ok=True)
    return record, run


def main():
    parser = argparse.ArgumentParser(description="G2 Framed Transport Directed Controls Test Runner")
    parser.add_argument("--build-dir", help="Path to cmake/ninja build directory")
    parser.add_argument("--output-dir", help="Directory to store detailed JSON receipts")
    args = parser.parse_args()

    build_dir, tokac, rt_o = resolve_build_paths(args.build_dir)
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    out_dir = Path(args.output_dir).resolve() if args.output_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    print(f"[g2-gate] Using compiler: {tokac}")
    print(f"[g2-gate] Using runtime: {rt_o}")

    total_start = time.monotonic()
    all_records = []
    failures = []

    with tempfile.TemporaryDirectory(prefix="toka-g2-gate-") as temp_str:
        work_dir = Path(temp_str)

        # 1. Core 11 Branches
        print("[1/4] Running G2 Framed Transport Core (11 branches)...", end="", flush=True)
        rec, run_res = run_case("given_11_branch", PASS_TESTS / "g14_stdx_framed_transport_test.tk", tokac, rt_o, work_dir, env, timeout=45)
        all_records.append(rec)
        if rec["passed"] and "All 11 G2 framed transport verification branches passed successfully!" in run_res.stdout:
            print(" PASS")
        else:
            print(" FAIL")
            failures.append("given_11_branch")

        # 2. TaskHandle Lifecycle & .start Lowering Pass Suite
        print("[2/4] Running TaskHandle Lifecycle & .start Lowering...", end="", flush=True)
        rec, run_res = run_case("lifecycle_pass_suite", PASS_TESTS / "g14_task_start_temporary_lifecycle_test.tk", tokac, rt_o, work_dir, env, timeout=30)
        all_records.append(rec)
        if rec["passed"] and "All 10 lifecycle and cancellation controls passed with zero TCB residue and exact-once drop!" in run_res.stdout:
            print(" PASS")
        else:
            print(" FAIL")
            failures.append("lifecycle_pass_suite")

        # 3. Protocol Controls (12 cases)
        proto_cases = [
            ("closed_empty", "closed_empty.tk", lambda out: "closed-empty write ok=false" in out),
            ("read_cancel", "read_cancel.tk", lambda out: "None=true, state=5, closed=true" in out),
            ("stream_partial", "framed_stream_partial.tk", lambda out: "wire=7, calls=7, frames=1" in out),
            ("stream_zero", "framed_stream_zero.tk", lambda out: "wire=0, calls=16, frames=0, state=5, closed=true" in out),
            ("stream_ioerror", "framed_stream_ioerror.tk", lambda out: "state=5, closed=true" in out),
            ("stream_overreported", "framed_stream_overreported_count.tk", lambda out: "state=5, closed=true" in out),
            ("stream_cancel", "framed_stream_cancel.tk", lambda out: "state=5, closed=true" in out),
            ("writer_partial", "framed_writer_partial.tk", lambda out: "wire=7, calls=7, frames=1" in out),
            ("writer_zero", "framed_writer_zero.tk", lambda out: "wire=0, calls=16, frames=0, state=5, closed=true" in out),
            ("writer_ioerror", "framed_writer_ioerror.tk", lambda out: "state=5, closed=true" in out),
            ("writer_overreported", "framed_writer_overreported_count.tk", lambda out: "state=5, closed=true" in out),
            ("writer_cancel", "framed_writer_cancel.tk", lambda out: "state=5, closed=true" in out),
        ]

        print(f"[3/4] Running G2 Transport Protocol Controls ({len(proto_cases)} cases)...", end="", flush=True)
        proto_failed = False
        for name, fname, validator in proto_cases:
            rec, run_res = run_case(name, FIXTURES / fname, tokac, rt_o, work_dir, env, timeout=20)
            all_records.append(rec)
            if not rec["passed"] or not validator(run_res.stdout):
                proto_failed = True
                failures.append(name)
        if not proto_failed:
            print(" PASS")
        else:
            print(" FAIL")

        # 4. Short Buffer Read Safety Probes (4 cases)
        probe_cases = [
            ("probe_stream_1", "framed_stream_read_short_buffer_1.tk"),
            ("probe_stream_2", "framed_stream_read_short_buffer_2.tk"),
            ("probe_reader_1", "framed_reader_read_short_buffer_1.tk"),
            ("probe_reader_2", "framed_reader_read_short_buffer_2.tk"),
        ]

        print(f"[4/4] Running Short Buffer Read Safety Probes ({len(probe_cases)} cases)...", end="", flush=True)
        probe_failed = False
        for name, fname in probe_cases:
            rec, run_res = run_case(name, FIXTURES / fname, tokac, rt_o, work_dir, env, timeout=20)
            all_records.append(rec)
            if not rec["passed"] or "returned Err=true,state=5" not in run_res.stdout or "TRANSPORT CLOSED" not in run_res.stdout:
                probe_failed = True
                failures.append(name)
        if not probe_failed:
            print(" PASS")
        else:
            print(" FAIL")

    total_ms = (time.monotonic() - total_start) * 1000

    report = {
        "schema": "toka.g2-directed-controls-report.v1",
        "total_cases": len(all_records),
        "passed_cases": len(all_records) - len(failures),
        "failed_cases": len(failures),
        "failures": failures,
        "total_duration_ms": total_ms,
        "records": all_records
    }

    if out_dir:
        (out_dir / "g2_directed_controls_report.json").write_text(json.dumps(report, indent=2))
        print(f"[g2-gate] Detailed report written to: {out_dir / 'g2_directed_controls_report.json'}")

    if failures:
        print(f"\n[FAILED] {len(failures)}/{len(all_records)} cases failed: {failures}")
        sys.exit(1)
    else:
        print(f"\n[SUCCESS] All {len(all_records)} G2 directed controls passed in {total_ms:.1f}ms")
        sys.exit(0)


if __name__ == "__main__":
    main()
