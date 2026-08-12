#!/usr/bin/env python3

"""Golden gate for the public machine-facing Toka CLI JSON boundary."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "tests/tooling/json_cli_contract.golden.json"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def nested_value(document, path):
    value = document
    for segment in path.split("."):
        require(isinstance(value, dict) and segment in value,
                "golden path is missing from JSON output: " + path)
        value = value[segment]
    return value


def contains_subset(actual, expected):
    if isinstance(expected, dict):
        return (isinstance(actual, dict) and
                all(key in actual and contains_subset(actual[key], value)
                    for key, value in expected.items()))
    return actual == expected


def parse_single_document(stdout, name):
    require(stdout, name + " emitted no JSON on stdout")
    require("\x1b" not in stdout,
            name + " mixed ANSI/color data into machine stdout")
    decoder = json.JSONDecoder()
    try:
        document, end = decoder.raw_decode(stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(name + " stdout is not JSON: " + str(error)) from error
    require(not stdout[end:].strip(),
            name + " emitted more than one JSON document or non-JSON stdout")
    require(isinstance(document, dict), name + " JSON root is not an object")
    return document


def run_case(toka, name, command, spec):
    result = subprocess.run(
        [str(part) for part in [toka, *command]], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(result.returncode == spec["exit"],
            "%s exit changed: expected %d, got %d\n%s%s" % (
                name, spec["exit"], result.returncode,
                result.stdout, result.stderr))
    document = parse_single_document(result.stdout, name)
    require(document.get("schema") == spec["schema"],
            name + " schema changed")
    require(document.get("version") == spec["version"],
            name + " version changed")
    for path, expected in spec.get("equals", {}).items():
        require(nested_value(document, path) == expected,
                name + " golden value changed at " + path)
    for path, expected_records in spec.get("contains", {}).items():
        records = nested_value(document, path)
        require(isinstance(records, list), name + " golden collection is not a list: " + path)
        for expected in expected_records:
            require(any(contains_subset(record, expected) for record in records),
                    name + " omitted golden record at " + path)
    for path, minimum in spec.get("minimum_lengths", {}).items():
        value = nested_value(document, path)
        require(hasattr(value, "__len__") and len(value) >= minimum,
                name + " collection is smaller than its golden minimum at " + path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    suffix = ".exe" if sys.platform == "win32" else ""
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    require(toka.is_file(), "toka SDK manager is missing")
    golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
    require(golden["schema"] == "toka.json-cli-contract-golden" and
            golden["version"] == 1,
            "JSON CLI golden identity changed")

    workspace = ROOT / "tests/tooling/semantic_workspace/main.tk"
    cases = {
        "check_success": ["check", "--json", ROOT / "tests/pass/g03_println.tk"],
        "check_failure": ["check", "--json", ROOT / "tests/fail/borrow_move.tk"],
        "explain": ["explain", "E0438", "--json"],
        "explain_failure": ["explain", "NOT_A_CODE", "--json"],
        "index": ["index", "--json", workspace],
        "index_failure": [
            "index", "--json", ROOT / "tests/fail/borrow_move.tk",
        ],
        "context": [
            "context", workspace, "--query-file", workspace,
            "--line", "13", "--character", "12",
        ],
        "context_failure": [
            "context", ROOT / "tests/fail/borrow_move.tk",
            "--query-file", ROOT / "tests/fail/borrow_move.tk",
            "--line", "19", "--character", "15",
        ],
        "query_references": [
            "query", "references", workspace, "--query-file", workspace,
            "--line", "13", "--character", "12", "--json",
        ],
        "query_references_failure": [
            "query", "references", ROOT / "tests/fail/borrow_move.tk",
            "--query-file", ROOT / "tests/fail/borrow_move.tk",
            "--line", "19", "--character", "15", "--json",
        ],
        "evidence": [
            "evidence", "--json", "--check-only",
            ROOT / "tests/semantics/tki_replay/cases/pal_call_001_alias/fail_mut_read_alias.tk",
        ],
        "cede_obligations": [
            "cede-obligations", "--json", "--check-only",
            ROOT / "tests/semantics/tki_replay/cases/own_cede_001_signature/fail_missing_cede.tk",
        ],
        "capabilities": [
            "capabilities", "--json", "--check-only",
            ROOT / "tests/conformance/diagnostics/call_handle_only_cannot_supply_payload.tk",
        ],
        "todo_goals": [
            "todo-goals", "--json", "--check-only",
            ROOT / "tests/tooling/typed_todo/explicit_contract.tk",
        ],
        "conditional_facts": [
            "conditional-facts", "--json", "--check-only",
            ROOT / "tests/tooling/typed_todo/conditional_binding_facts.tk",
        ],
    }
    require(set(cases) == set(golden["cases"]),
            "JSON CLI golden cases and commands drifted")
    for name, command in cases.items():
        run_case(toka, name, command, golden["cases"][name])

    print(json.dumps({
        "checks": sorted(cases),
        "count": len(cases),
        "result": "pass",
        "schema": "toka.json-cli-contract-test",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
