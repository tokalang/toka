#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import re
import json
import os

def main():
    def_path = "/home/zhyi/GitDP/tokalang/include/toka/DiagnosticDefs.def"
    json_path = "/home/zhyi/GitDP/tokalang/spec/diagnostic.map.json"

    if not os.path.exists(def_path):
        print(f"Error: {def_path} does not exist.")
        return

    # Load existing JSON
    if os.path.exists(json_path):
        with open(json_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
    else:
        data = {
            "spec_version": "1.0.0-rc.1",
            "compiler_compat": ">=0.9.8-01 <0.10.0",
            "diagnostics": {}
        }

    diagnostics = data.setdefault("diagnostics", {})

    # Parse DiagnosticDefs.def
    # DIAG(ID, Level, Code, Msg)
    pattern = re.compile(r'^\s*DIAG\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*"([^"]+)"\s*,\s*"(.*)"\s*\)\s*(?://.*)?$')
    
    parsed_entries = []
    with open(def_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            match = pattern.match(line)
            if match:
                diag_id, level, code, message = match.groups()
                parsed_entries.append((diag_id, level, code, message))
            else:
                # If a line starts with DIAG but didn't match the pattern, warn us
                if line.startswith("DIAG"):
                    print(f"Warning: line did not match regex: {line}")

    print(f"Successfully parsed {len(parsed_entries)} entries from .def file.")

    # Merge into JSON, keeping order
    new_diagnostics = {}
    
    # We want to add all parsed entries in order
    for diag_id, level, code, message in parsed_entries:
        if code in diagnostics:
            # Update existing
            entry = diagnostics[code]
            entry["id"] = diag_id
            entry["level"] = level
            entry["message"] = message
            if "description" not in entry:
                entry["description"] = message
        else:
            # Create new
            entry = {
                "id": diag_id,
                "level": level,
                "message": message,
                "description": message
            }
        new_diagnostics[code] = entry

    # Add any existing diagnostics that were not in the parsed list (just in case)
    for code, entry in diagnostics.items():
        if code not in new_diagnostics:
            new_diagnostics[code] = entry

    data["diagnostics"] = new_diagnostics

    # Write back to JSON
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write('\n')

    print(f"Successfully merged and updated {json_path}. Total entries: {len(new_diagnostics)}")

if __name__ == "__main__":
    main()
