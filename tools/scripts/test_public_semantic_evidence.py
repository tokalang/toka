#!/usr/bin/env python3

"""Fail-closed ABI gate for Public Semantic Evidence v1."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
PASS_CASE = ROOT / "tests/semantics/tki_replay/cases/pal_call_001_alias/pass_read_read.tk"
FAIL_CASE = ROOT / "tests/semantics/tki_replay/cases/pal_call_001_alias/fail_mut_read_alias.tk"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, expected):
    result = subprocess.run(
        [str(item) for item in command], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != expected:
        raise RuntimeError(
            "expected exit %d, got %d: %s\n%s%s" %
            (expected, result.returncode, " ".join(map(str, command)),
             result.stdout, result.stderr)
        )
    return result


def validate_location(location, context):
    require(set(location) == {"file", "line", "column"},
            context + " location fields changed")
    require(isinstance(location["file"], str), context + " location file")
    require(isinstance(location["line"], int) and location["line"] >= 0,
            context + " location line")
    require(isinstance(location["column"], int) and location["column"] >= 0,
            context + " location column")


def validate_document(payload):
    require(set(payload) == {"schema", "version", "records"},
            "public evidence envelope fields changed")
    require(payload["schema"] == "toka.semantic-evidence",
            "public evidence schema changed")
    require(payload["version"] == 1, "public evidence version changed")
    require(isinstance(payload["records"], list), "records is not an array")
    required = {
        "rule", "operation", "decision", "reason", "subject", "origin",
        "primary_location", "origin_location",
    }
    allowed_decisions = {"Allow", "Reject", "ConservativeReject"}
    seen = set()
    for record in payload["records"]:
        require(set(record) == required, "public evidence record fields changed")
        require(record["decision"] in allowed_decisions,
                "unknown public evidence decision")
        for key in ("rule", "operation", "reason", "subject", "origin"):
            require(isinstance(record[key], str), "record %s is not a string" % key)
        validate_location(record["primary_location"], "primary")
        validate_location(record["origin_location"], "origin")
        current = json.dumps(record, sort_keys=True, separators=(",", ":"))
        require(current not in seen, "records are not deduplicated")
        seen.add(current)


def main():
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / "build/bin" / ("tokac" + suffix)
    toka = ROOT / "build/bin" / ("toka" + suffix)
    schema = ROOT / "schemas/toka.semantic-evidence.v1.schema.json"
    require(tokac.is_file() and toka.is_file(), "SDK binaries are missing")
    require(schema.is_file(), "public evidence schema is missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["version"] == {"const": 1},
            "schema does not freeze v1")

    with tempfile.TemporaryDirectory(prefix="toka-public-evidence-") as temp:
        temp_dir = Path(temp)
        first = run([
            tokac, "--semantic-evidence=json", PASS_CASE,
            "-o", temp_dir / ("first" + suffix),
        ], expected=0)
        second = run([
            tokac, "--semantic-evidence=json", PASS_CASE,
            "-o", temp_dir / ("second" + suffix),
        ], expected=0)
        legacy = run([
            tokac, "--dump-semantic-evidence=json", PASS_CASE,
            "-o", temp_dir / ("legacy" + suffix),
        ], expected=0)
        require(first.stdout == second.stdout == legacy.stdout,
                "public evidence or compatibility alias is not deterministic")
        pass_document = json.loads(first.stdout)
        validate_document(pass_document)
        require(any(record["rule"] == "PAL-CALL-001" and
                    record["decision"] == "Allow"
                    for record in pass_document["records"]),
                "public evidence omitted the PAL allow decision")

        failure = run([
            tokac, "--semantic-evidence=json", "-c", FAIL_CASE,
            "-o", temp_dir / "failure.o",
        ], expected=1)
        fail_document = json.loads(failure.stdout)
        validate_document(fail_document)
        reject = next((record for record in fail_document["records"]
                       if record["rule"] == "PAL-CALL-001" and
                       record["decision"] == "Reject"), None)
        require(reject is not None and reject["reason"] == "OverlappingExclusiveAccess",
                "public evidence omitted the PAL rejection cause")
        require(reject["origin_location"]["file"],
                "public evidence omitted rejection origin")

        manager = run([
            toka, "evidence", "--json", PASS_CASE,
            "-o", temp_dir / ("manager" + suffix),
        ], expected=0)
        require(manager.stdout == first.stdout,
                "toka evidence does not preserve compiler evidence output")

        mixed = run([
            tokac, "--semantic-evidence=json", "--diagnostics-json",
            "--check-only", FAIL_CASE,
        ], expected=1)
        require(not mixed.stdout,
                "incompatible JSON output modes emitted an ambiguous document")

    print("Public Semantic Evidence v1 ABI gate PASSED")


if __name__ == "__main__":
    main()
