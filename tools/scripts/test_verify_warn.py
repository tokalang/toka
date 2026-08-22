#!/usr/bin/env python3
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

TOKAC = os.environ.get("TOKAC", "./build/bin/tokac")
WARN_TEST_DIR = "tests/warn"
TMP_TEST_DIR = Path("tmp/warn_cases")

GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[0;33m"
NC = "\033[0m"


def strip_ansi(text):
    ansi_escape = re.compile(r"\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")
    return ansi_escape.sub("", text)


def expected_codes_for(test_file):
    expected = []
    with open(test_file, "r") as f:
        for line in f:
            if "// EXPECT:" in line or "// EXPECT_WARNING:" in line:
                expected.extend(re.findall(r"W\d+", line))
    return expected


def expected_counts_for(test_file):
    expected = {}
    with open(test_file, "r") as f:
        for line in f:
            if "// EXPECT_COUNT:" not in line:
                continue
            match = re.search(r"(W\d+)\s+(\d+)", line)
            if match:
                expected[match.group(1)] = int(match.group(2))
    return expected


def filtered_diagnostics(raw_output):
    lines = []
    for line in raw_output.splitlines():
        clean = strip_ansi(line)
        stripped = clean.strip()
        if not stripped:
            continue
        if "warning[" in clean or "error[" in clean or ".tk:" in clean:
            lines.append(clean)
        elif stripped.startswith("|") or stripped.startswith("^") or re.search(r"^\d+ \|", stripped):
            lines.append(clean)
    return "\n".join(lines).strip()


def main():
    if not os.path.exists(TOKAC):
        print(f"{RED}Error: Compiler not found at {TOKAC}{NC}")
        print("Please build it first: make -C build -j8")
        sys.exit(1)

    if len(sys.argv) > 1:
        files = [f for f in sys.argv[1:] if f.endswith(".tk")]
    else:
        files = glob.glob(os.path.join(WARN_TEST_DIR, "*.tk"))

    files.sort()
    if not files:
        print(f"{YELLOW}No .tk files found in {WARN_TEST_DIR}{NC}")
        return

    TMP_TEST_DIR.mkdir(parents=True, exist_ok=True)

    total_passed = 0
    total_failed = 0

    print("Starting Toka 'VERIFY WARN' Test Suite...")
    print("---------------------------------------")

    for test_file in files:
        test_name = os.path.basename(test_file)
        temp_file = TMP_TEST_DIR / test_name
        shutil.copyfile(test_file, temp_file)

        expected_codes = expected_codes_for(test_file)
        expected_counts = expected_counts_for(test_file)
        if not expected_codes:
            print(f"Testing {test_name:<35} {RED}FAIL (Missing Expectations){NC}")
            total_failed += 1
            continue

        result = subprocess.run([TOKAC, str(temp_file)], capture_output=True, text=True)
        raw_output = result.stderr + result.stdout
        output = filtered_diagnostics(raw_output)

        if result.returncode != 0:
            print(f"Testing {test_name:<35} {RED}FAIL (Compilation Failed){NC}")
            print(output)
            total_failed += 1
            continue

        actual_codes = re.findall(r"W\d+", output)
        missing = [code for code in expected_codes if code not in actual_codes]
        if missing:
            print(f"Testing {test_name:<35} {RED}FAIL (Warning Match Failed){NC}")
            print(f"  {YELLOW}Expected warning codes not found:{NC} {missing}")
            print(f"  {YELLOW}Actual Filtered Output:{NC}\n{output}")
            total_failed += 1
            continue

        count_mismatches = []
        for code, expected_count in expected_counts.items():
            actual_count = actual_codes.count(code)
            if actual_count != expected_count:
                count_mismatches.append((code, expected_count, actual_count))
        if count_mismatches:
            print(f"Testing {test_name:<35} {RED}FAIL (Warning Count Mismatch){NC}")
            for code, expected_count, actual_count in count_mismatches:
                print(f"  {YELLOW}{code}:{NC} expected {expected_count}, got {actual_count}")
            print(f"  {YELLOW}Actual Filtered Output:{NC}\n{output}")
            total_failed += 1
            continue

        print(f"Testing {test_name:<35} {GREEN}PASS{NC}")
        total_passed += 1

    print("---------------------------------------")
    print("Summary:")
    print(f"  Passed: {GREEN}{total_passed}{NC}")
    print(f"  Failed: {RED}{total_failed}{NC}")

    if total_failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
