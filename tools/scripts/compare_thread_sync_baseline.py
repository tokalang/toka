#!/usr/bin/env python3
"""Read-only comparison of one thread/sync full run with the retained baseline.

Does not bless diagnostics, retry suites, or modify test inputs. Optional
frontend probes classify only the remaining failed PASS cases.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def clean(text):
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def suite(directory, name):
    text = clean((directory / (name + ".log")).read_text())
    if name == "ctest":
        summary = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
        if not summary:
            raise RuntimeError("CTest has not finished: " + str(directory))
        failures = set(re.findall(r"^\s*\d+ - (\S+) \((?:Failed|Timeout|SEGFAULT|Subprocess aborted)\)", text, re.M))
        return dict(passed=int(summary[3]) - int(summary[2]), failed=int(summary[2]),
                    failures=sorted(failures), abnormal=[])
    summary = re.search(r"Summary:\s+Passed:\s+(\d+)\s+Failed:\s+(\d+)", text)
    if not summary:
        raise RuntimeError(name + " has not finished: " + str(directory))
    pattern = r"^\[FAIL\] (\S+\.tk)" if name == "pass" else r"^Testing (\S+\.tk)\s+(?:FAIL|ERROR)"
    return dict(passed=int(summary[1]), failed=int(summary[2]),
                failures=sorted(set(re.findall(pattern, text, re.M))),
                abnormal=re.findall(r"^Testing .*?(?:Unexpectedly Passed|Abnormal Exit|Execution Failed).*?$", text, re.M))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--suites", nargs="+", choices=("pass", "fail", "ctest"), default=("pass", "fail", "ctest"))
    args = parser.parse_args()
    result = {}
    for name in args.suites:
        before, after = suite(args.baseline, name), suite(args.current, name)
        old, new = set(before["failures"]), set(after["failures"])
        result[name] = dict(baseline=before, current=after, added=sorted(new - old),
                            recovered=sorted(old - new), remaining=sorted(old & new))
    if args.compiler and "pass" in result:
        baseline = {item["file"]: item for item in
                    map(json.loads, (args.baseline / "pass-first-errors.jsonl").read_text().splitlines())}
        def inspect(name):
            process = subprocess.run([str(args.compiler.resolve()), "tests/pass/" + name, "--check-only"],
                                     cwd=ROOT, env=dict(os.environ, TOKA_LIB=str(ROOT / "lib")),
                                     capture_output=True, text=True, timeout=60)
            text = clean(process.stderr)
            diagnostic = re.search(r"error\[([^]]+)\]: ([^\n]+)", text)
            site = re.search(r" --> ([^\n]+)", text)
            current = dict(file=name, rc=process.returncode, first_error=diagnostic[1] if diagnostic else None,
                           reason=diagnostic[2] if diagnostic else None, site=site[1] if site else None)
            previous = baseline.get(name)
            def signature(item):
                return (item.get("first_error"), item.get("reason"),
                        re.sub(r":\d+:\d+$", "", item.get("site") or ""))
            current["baseline_first_error"] = previous
            current["same_first_error"] = bool(previous and signature(previous) == signature(current))
            return current
        with ThreadPoolExecutor(max_workers=4) as pool:
            result["remaining_pass_frontend"] = list(pool.map(inspect, result["pass"]["current"]["failures"]))
        result["changed_first_errors"] = [item["file"] for item in result["remaining_pass_frontend"]
                                          if not item["same_first_error"]]
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
