#!/usr/bin/env python3
"""Original JSON recovery targets: report failures without changing their oracles."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TARGETS = [
    "tests/pass/g03_test_json.tk", "tests/pass/g03_test_dynamic_json.tk",
    "tests/pass/g03_test_json_depth.tk", "tests/pass/g07_test_json_scientific.tk",
    "tests/pass/g07_test_json_unicode.tk", "tests/pass/g07_test_json_serde.tk",
    "tests/pass/g08_json_generic_failure_safe.tk",
    "tests/conformance/std/json_serde_conformance.tk",
    "tests/semantics/binding_b4_enum_copy/json_result.tk",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-json-recovery-") as directory:
        def test(item):
            output = Path(directory) / Path(item).stem
            flags = ["--check-only"] if args.check_only else ["-o", str(output)]
            built = subprocess.run([str(compiler), str(ROOT / item), *flags], cwd=ROOT,
                                   env=env, text=True, capture_output=True, timeout=120)
            diagnostics = re.sub(r"\x1b\[[0-9;]*m", "", built.stderr)
            result = {"target": item, "compile_rc": built.returncode,
                      "diagnostics": diagnostics, "passed": built.returncode == 0}
            result["empty_smoke"] = item == "tests/pass/g03_test_json.tk" and (ROOT / item).read_text().strip() == "fn main() -> i32 { return 0 }"
            if built.returncode == 0 and not args.check_only:
                ran = subprocess.run([str(output)], cwd=ROOT, env=env, text=True,
                                     capture_output=True, timeout=60)
                result.update(runtime_rc=ran.returncode, stdout=ran.stdout, stderr=ran.stderr,
                              passed=ran.returncode == 0)
            return result
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(test, TARGETS))
    for result in results:
        first = next((line for line in result["diagnostics"].splitlines() if "error[" in line), "")
        print(("PASS " if result["passed"] else "FAIL ") + result["target"] + " " + first)
    passed = sum(result["passed"] for result in results)
    print(f"JSON original targets: {passed}/{len(results)}; check_only={args.check_only}")
    real = [result for result in results if not result["empty_smoke"]]
    print(f"Actual JSON targets (excluding existing empty smoke): {sum(result['passed'] for result in real)}/{len(real)}")
    if args.report:
        args.report.write_text(json.dumps({"check_only": args.check_only, "passed": passed,
                                          "total": len(results), "results": results}, indent=2) + "\n")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
