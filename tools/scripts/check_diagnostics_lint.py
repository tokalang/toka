#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import sys
import json

# Configuration
ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC_DIR = os.path.join(ROOT_DIR, "src")
BASELINE_PATH = os.path.join(ROOT_DIR, "spec", "diagnostic.baseline.json")

# Regex patterns
# 1. error(something, "literal...")
#    Allowing spaces, multiline, etc. We will search line-by-line first for simplicity and reliability.
pattern_error_literal = re.compile(r'\berror\s*\(\s*[^,\)]+\s*,\s*"')

# 2. Use of generic/raw-string error IDs
pattern_generic_ids = re.compile(r'\bDiagID::ERR_GENERIC_(?:SEMA|PARSE)\b')

def scan_files():
    violations = []
    
    # Traverse src directory recursively
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            if not (file.endswith(".cpp") or file.endswith(".h")):
                continue
                
            file_path = os.path.join(root, file)
            # Make path relative to workspace root for portability in baseline
            rel_path = os.path.relpath(file_path, ROOT_DIR)
            
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                for line_idx, line in enumerate(f, 1):
                    # Ignore comment lines
                    stripped = line.strip()
                    if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
                        continue
                        
                    # Ignore the helper function definitions themselves
                    if "void Parser::error" in line or "void CodeGen::error" in line:
                        continue
                        
                    # Check for error(..., "literal")
                    if pattern_error_literal.search(line):
                        violations.append({
                            "file": rel_path,
                            "line": line_idx,
                            "content": stripped,
                            "type": "error_with_string_literal"
                        })
                        
                    # Check for generic IDs usage
                    elif pattern_generic_ids.search(line):
                        violations.append({
                            "file": rel_path,
                            "line": line_idx,
                            "content": stripped,
                            "type": "generic_diagnostic_id"
                        })
                        
    return violations

def main():
    generate_mode = "--generate-baseline" in sys.argv
    
    print("Running Toka Diagnostic Architecture Lint Checker...")
    
    current_violations = scan_files()
    
    if generate_mode:
        # Save all current violations as baseline
        baseline_data = {
            "description": "Baseline of legacy ad-hoc raw-string compiler errors. DO NOT ADD NEW ENTRIES HERE!",
            "violations": current_violations
        }
        with open(BASELINE_PATH, 'w', encoding='utf-8') as f:
            json.dump(baseline_data, f, indent=2, ensure_ascii=False)
            f.write('\n')
        print(f"Successfully generated baseline at {BASELINE_PATH} with {len(current_violations)} legacy entries.")
        sys.exit(0)
        
    # Standard check mode: Load baseline
    if not os.path.exists(BASELINE_PATH):
        print(f"Warning: Baseline file not found at {BASELINE_PATH}. Running in strict mode.")
        baseline_violations = []
    else:
        with open(BASELINE_PATH, 'r', encoding='utf-8') as f:
            baseline_data = json.load(f)
            baseline_violations = baseline_data.get("violations", [])
            
    # Create a set of baseline signatures for fast lookup
    # We identify a violation in the baseline by its relative file and the cleaned content (to be robust against line shifting!)
    baseline_signatures = set()
    for v in baseline_violations:
        # Signature: (file, content)
        # We strip whitespaces to be extra robust against formatting changes
        clean_content = "".join(v["content"].split())
        baseline_signatures.add((v["file"], clean_content))
        
    # Compare current violations against baseline
    new_violations = []
    for v in current_violations:
        clean_content = "".join(v["content"].split())
        if (v["file"], clean_content) not in baseline_signatures:
            new_violations.append(v)
            
    if new_violations:
        print("\n\033[1;31m❌ TOKA ARCHITECTURE VIOLATION: New raw-string error(s) detected!\033[0m")
        print("All new compiler errors must be formally declared in 'spec/diagnostic.map.json' and use strong-typed DiagID.")
        print("Do not write ad-hoc string-based errors like error(node, \"...\") or ERR_GENERIC_SEMA.")
        print("--------------------------------------------------------------------------------")
        for nv in new_violations:
            print(f"\033[1;33mFile:\033[0m {nv['file']}:{nv['line']}")
            print(f"\033[1;33mCode:\033[0m {nv['content']}")
            print(f"\033[1;33mType:\033[0m {nv['type']}")
            print("--------------------------------------------------------------------------------")
        sys.exit(1)
        
    print(f"Lint Pass: 0 new ad-hoc string errors detected (Legacy baseline: {len(baseline_signatures)} allowed).")
    sys.exit(0)

if __name__ == "__main__":
    main()
