#!/usr/bin/env python3

"""Fail-closed ABI gate for the H/P call capability pilot."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
PASS_CASE = ROOT / "tests/conformance/ownership/call_permission_capability_matrix.tk"
HANDLE_ONLY_FAIL = ROOT / "tests/conformance/diagnostics/call_handle_only_cannot_supply_payload.tk"
PAYLOAD_ONLY_FAIL = ROOT / "tests/conformance/diagnostics/call_payload_only_cannot_supply_rebind.tk"
CAPABILITY_FIELDS = {"handle_rebind", "payload_write"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, expected):
    result = subprocess.run([str(item) for item in command], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != expected:
        raise RuntimeError("expected exit %d, got %d: %s\n%s%s" %
                           (expected, result.returncode,
                            " ".join(map(str, command)),
                            result.stdout, result.stderr))
    return result


def validate_location(location, context):
    require(set(location) == {"file", "line", "column"},
            context + " location fields changed")
    require(isinstance(location["file"], str), context + " location file")
    require(isinstance(location["line"], int) and location["line"] >= 0,
            context + " location line")
    require(isinstance(location["column"], int) and location["column"] >= 0,
            context + " location column")


def validate_capability(value, context):
    require(set(value) == CAPABILITY_FIELDS, context + " capability fields changed")
    require(all(isinstance(item, bool) for item in value.values()),
            context + " capability value is not boolean")


def validate_document(payload):
    require(set(payload) == {"schema", "version", "records"},
            "capability pilot envelope fields changed")
    require(payload["schema"] == "toka.capability-pilot", "pilot schema changed")
    require(payload["version"] == 1, "pilot version changed")
    require(isinstance(payload["records"], list), "records is not an array")
    required = {"callee", "parameter", "subject", "declared", "inferred",
                "request", "required", "granted", "independent_cede",
                "location", "contract_location"}
    seen = set()
    for record in payload["records"]:
        require(set(record) == required, "capability record fields changed")
        for field in ("callee", "parameter", "subject"):
            require(isinstance(record[field], str), field + " is not a string")
        for field in ("declared", "inferred", "request", "required", "granted"):
            validate_capability(record[field], field)
        require(isinstance(record["independent_cede"], bool),
                "independent_cede is not boolean")
        validate_location(record["location"], "record")
        validate_location(record["contract_location"], "contract")
        current = json.dumps(record, sort_keys=True, separators=(",", ":"))
        require(current not in seen, "records are not deduplicated")
        seen.add(current)


def compile_pilot(tokac, source, expected):
    result = run([tokac, "--capabilities=json", "--check-only", source], expected)
    payload = json.loads(result.stdout)
    validate_document(payload)
    return result.stdout, payload


def record_for(payload, callee):
    return next((item for item in payload["records"] if item["callee"] == callee), None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    schema = ROOT / "schemas/toka.capability-pilot.v1.schema.json"
    require(tokac.is_file() and toka.is_file() and schema.is_file(),
            "capability pilot prerequisites are missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["schema"] == {"const": "toka.capability-pilot"},
            "pilot schema identity is not frozen")
    require(schema_doc["properties"]["version"] == {"const": 1},
            "pilot schema version is not frozen")

    with tempfile.TemporaryDirectory(prefix="toka-capability-pilot-") as temp:
        temp_dir = Path(temp)
        first, allowed = compile_pilot(tokac, PASS_CASE, expected=0)
        second, repeated = compile_pilot(tokac, PASS_CASE, expected=0)
        require(first == second and allowed == repeated,
                "capability pilot output is not deterministic")
        payload_grant = record_for(allowed, "overwrite")
        require(payload_grant is not None and payload_grant["granted"]["payload_write"],
                "payload capability grant is missing")
        handle_grant = record_for(allowed, "reset")
        require(handle_grant is not None and handle_grant["granted"]["handle_rebind"],
                "handle capability grant is missing")

        _, handle_only = compile_pilot(tokac, HANDLE_ONLY_FAIL, expected=1)
        payload_denial = record_for(handle_only, "overwrite")
        require(payload_denial is not None and
                payload_denial["declared"]["handle_rebind"] and
                not payload_denial["declared"]["payload_write"] and
                payload_denial["required"]["payload_write"] and
                not payload_denial["granted"]["payload_write"],
                "handle-only path did not explain payload denial")

        _, payload_only = compile_pilot(tokac, PAYLOAD_ONLY_FAIL, expected=1)
        handle_denial = record_for(payload_only, "reset")
        require(handle_denial is not None and
                not handle_denial["declared"]["handle_rebind"] and
                handle_denial["declared"]["payload_write"] and
                handle_denial["required"]["handle_rebind"] and
                not handle_denial["granted"]["handle_rebind"],
                "payload-only path did not explain handle denial")

        manager = run([toka, "capabilities", "--json", PASS_CASE,
                       "-o", temp_dir / ("manager" + suffix)], expected=0)
        require(manager.stdout == first,
                "toka capabilities does not preserve compiler output")

        mixed = run([tokac, "--capabilities=json", "--semantic-evidence=json",
                     "--check-only", HANDLE_ONLY_FAIL], expected=1)
        require(not mixed.stdout,
                "incompatible JSON output modes emitted an ambiguous document")

    print("H/P Call Capability Pilot v1 ABI gate PASSED")


if __name__ == "__main__":
    main()
