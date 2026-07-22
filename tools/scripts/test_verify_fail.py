#!/usr/bin/env python3
import sys
import os
import subprocess
import glob
import re
import difflib

# Configuration
TOKAC = "./build/bin/tokac"
FAIL_TEST_DIR = "tests/fail"

GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[0;33m'
BLUE = '\033[0;34m'
NC = '\033[0m' # No Color

def main():
    bless_mode = os.environ.get("BLESS") == "1"
    args = list(sys.argv[1:])
    exclude_file = None
    if "--exclude-file" in args:
        index = args.index("--exclude-file")
        if index + 1 >= len(args):
            print(f"{RED}--exclude-file requires a path{NC}")
            sys.exit(2)
        exclude_file = args[index + 1]
        del args[index:index + 2]
    
    if not os.path.exists(TOKAC):
        print(f"{RED}Error: Compiler not found at {TOKAC}{NC}")
        print(f"Please build it first: make -C build -j8")
        sys.exit(1)

    # Determine which files to test
    if args:
        files = [f for f in args if f.endswith(".tk")]
    else:
        files = glob.glob(os.path.join(FAIL_TEST_DIR, "*.tk"))
        if not files:
            print(f"{YELLOW}No .tk files found in {FAIL_TEST_DIR}{NC}")
            sys.exit(0)

    if exclude_file:
        with open(exclude_file, "r", encoding="utf-8") as stream:
            excluded = {
                line.strip().replace("\\", "/")
                for line in stream
                if line.strip() and not line.lstrip().startswith("#")
            }
        normalized_files = {path.replace("\\", "/") for path in files}
        missing = sorted(excluded.difference(normalized_files))
        if missing:
            print(f"{RED}Unknown quarantined fail fixture(s): " +
                  ", ".join(missing) + NC)
            sys.exit(2)
        files = [path for path in files
                 if path.replace("\\", "/") not in excluded]
        print("Quarantined fail fixtures: %d" % len(excluded))

    files.sort()
    
    total_run = 0
    total_passed = 0
    total_failed = 0

    if bless_mode:
        print(f"{BLUE}Starting Toka 'VERIFY FAIL' Test Suite in BLESS mode...{NC}")
    else:
        print("Starting Toka 'VERIFY FAIL' Test Suite...")
    print("---------------------------------------")

    for test_file in files:
        if not os.path.exists(test_file):
            print(f"{YELLOW}Skipping missing file: {test_file}{NC}")
            continue
            
        total_run += 1
        test_name = os.path.basename(test_file)
        stderr_path = test_file.replace(".tk", ".stderr")
        
        # 1. Run Compiler
        try:
            result = subprocess.run(
                [TOKAC, test_file],
                capture_output=True,
                text=True
            )
        except Exception as e:
            print(f"Testing {test_name:<35} {RED}ERROR (Execution Failed){NC}: {e}")
            total_failed += 1
            continue

        raw_output = result.stderr + result.stdout
        exit_code = result.returncode

        # --- Filter output to keep diagnostic relevance ---
        def strip_ansi(text):
            ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
            return ansi_escape.sub('', text)

        ui_lines = []
        for line in raw_output.splitlines():
            clean_line = line.strip()
            if not clean_line: continue
            if clean_line.startswith("[DEBUG]") or clean_line.startswith("DEBUG:") or clean_line.startswith("DEBUG-PAT:"):
                continue
            
            line = strip_ansi(line)
            # Logic: keep lines that look like diagnostics or related source snippets
            if "error[" in line or "note:" in line or "warning:" in line or ".tk:" in line:
                ui_lines.append(line)
            elif line.strip().startswith("|") or line.strip().startswith("^") or re.search(r'^\d+ \|', line.strip()):
                ui_lines.append(line)
        
        filtered_output = "\n".join(ui_lines).strip()

        # 2. Verification Logic
        
        # Case A: Unexpected Pass (Exit Code 0)
        if exit_code == 0:
            print(f"Testing {test_name:<35} {RED}FAIL (Unexpectedly Passed){NC}")
            total_failed += 1
            continue
            
        # Case B: Blessing Mode
        if bless_mode:
            with open(stderr_path, 'w') as f:
                f.write(filtered_output + "\n")
            print(f"Testing {test_name:<35} {GREEN}BLESSED{NC}")
            total_passed += 1
            continue

        # Case C: Snapshot Comparison
        if os.path.exists(stderr_path):
            with open(stderr_path, 'r') as f:
                expected_output = f.read().strip()
            
            if filtered_output == expected_output:
                # Success
                total_passed += 1
            else:
                print(f"Testing {test_name:<35} {RED}FAIL (Snapshot Mismatch){NC}")
                diff = difflib.unified_diff(
                    expected_output.splitlines(),
                    filtered_output.splitlines(),
                    fromfile="Expected (.stderr)",
                    tofile="Actual (Compiler Output)",
                    lineterm=""
                )
                print(f"{YELLOW}Diff:{NC}")
                for line in diff:
                    if line.startswith('+'):
                        print(f"{GREEN}{line}{NC}")
                    elif line.startswith('-'):
                        print(f"{RED}{line}{NC}")
                    else:
                        print(line)
                print("---------------------------------------")
                total_failed += 1
            continue

        # Case D: Fallback to Legacy Comments (if no snapshot exists)
        expected_codes = []
        with open(test_file, 'r') as f:
            for line in f:
                if '// EXPECT:' in line or '// EXPECT_ERROR:' in line:
                    found_codes = re.findall(r'E\d+', line)
                    expected_codes.extend(found_codes)
        
        if expected_codes:
            actual_codes = re.findall(r'E\d+', filtered_output)
            missing = [c for c in expected_codes if c not in actual_codes]
            if not missing:
                total_passed += 1
            else:
                print(f"Testing {test_name:<35} {RED}FAIL (Legacy Match Failed){NC}")
                print(f"  {YELLOW}Expected codes not found:{NC} {missing}")
                print(f"  {YELLOW}Actual Filtered Output:{NC}\n{filtered_output}")
                total_failed += 1
        else:
            print(f"Testing {test_name:<35} {RED}FAIL (Missing Snapshot & Expectations){NC}")
            print(f"  {YELLOW}Actual Filtered Output:{NC}\n{filtered_output}")
            print(f"  {YELLOW}Hint: Run with BLESS=1 to generate a snapshot.{NC}")
            total_failed += 1

    print("---------------------------------------")
    print("Summary:")
    print(f"  Passed: {GREEN}{total_passed}{NC}")
    print(f"  Failed: {RED}{total_failed}{NC}")
    
    if total_failed > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
