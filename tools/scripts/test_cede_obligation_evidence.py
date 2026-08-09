#!/usr/bin/env python3

"""Fail-closed ABI gate for Cede Obligation Evidence v1."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
PASS_CALLER = ROOT / "tests/semantics/tki_replay/cases/own_cede_001_signature/pass_explicit_cede.tk"
FAIL_CALLER = ROOT / "tests/semantics/tki_replay/cases/own_cede_001_signature/fail_missing_cede.tk"
FAIL_CALLEE = ROOT / "tests/fail/cede_param_unconsumed.tk"
PASS_RETURN = ROOT / "tests/semantics/tki_replay/cases/own_cede_002_return/pass_cede_return.tk"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, expected, env=None):
    result = subprocess.run(
        [str(item) for item in command], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
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
            "cede obligation envelope fields changed")
    require(payload["schema"] == "toka.cede-obligation-evidence",
            "cede obligation schema changed")
    require(payload["version"] == 1, "cede obligation version changed")
    require(isinstance(payload["records"], list), "records is not an array")
    required = {"stage", "status", "reason", "subject", "origin",
                "location", "contract_location"}
    stages = {"caller-transfer", "callee-consumption", "return-transfer"}
    statuses = {"fulfilled", "violated"}
    reasons = {"MissingExplicitCede", "UnconsumedCede", "CedeConsumed",
               "MissingCedeReturn"}
    seen = set()
    for record in payload["records"]:
        require(set(record) == required, "cede obligation record fields changed")
        require(record["stage"] in stages, "unknown cede obligation stage")
        require(record["status"] in statuses, "unknown cede obligation status")
        require(record["reason"] in reasons, "unknown cede obligation reason")
        require(isinstance(record["subject"], str), "subject is not a string")
        require(isinstance(record["origin"], str), "origin is not a string")
        validate_location(record["location"], "record")
        validate_location(record["contract_location"], "contract")
        current = json.dumps(record, sort_keys=True, separators=(",", ":"))
        require(current not in seen, "records are not deduplicated")
        seen.add(current)


def find_record(payload, stage, status, reason, source):
    return next((record for record in payload["records"]
                 if record["stage"] == stage and record["status"] == status and
                 record["reason"] == reason and
                 record["location"]["file"].endswith(source)), None)


def compile_evidence(tokac, source, expected):
    result = run([tokac, "--cede-obligations=json", "--check-only", source], expected)
    payload = json.loads(result.stdout)
    validate_document(payload)
    return result.stdout, payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    tool_env = os.environ.copy()
    tool_env["TOKA_LIB"] = str((ROOT / args.build_dir / "lib").resolve())
    schema = ROOT / "schemas/toka.cede-obligation-evidence.v1.schema.json"
    require(tokac.is_file() and toka.is_file(), "SDK binaries are missing")
    require(schema.is_file(), "cede obligation schema is missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["schema"] ==
            {"const": "toka.cede-obligation-evidence"},
            "schema does not freeze its identity")
    require(schema_doc["properties"]["version"] == {"const": 1},
            "schema does not freeze v1")

    with tempfile.TemporaryDirectory(prefix="toka-cede-obligation-") as temp:
        temp_dir = Path(temp)
        first, pass_payload = compile_evidence(tokac, PASS_CALLER, expected=0)
        second, repeated = compile_evidence(tokac, PASS_CALLER, expected=0)
        require(first == second and pass_payload == repeated,
                "cede obligation output is not deterministic")
        require(find_record(pass_payload, "caller-transfer", "fulfilled",
                            "CedeConsumed", "pass_explicit_cede.tk") is not None,
                "fulfilled caller transfer is missing")

        _, missing_cede = compile_evidence(tokac, FAIL_CALLER, expected=1)
        require(find_record(missing_cede, "caller-transfer", "violated",
                            "MissingExplicitCede", "fail_missing_cede.tk") is not None,
                "missing explicit cede violation is missing")

        _, unconsumed = compile_evidence(tokac, FAIL_CALLEE, expected=1)
        require(find_record(unconsumed, "callee-consumption", "violated",
                            "UnconsumedCede", "cede_param_unconsumed.tk") is not None,
                "unconsumed callee obligation is missing")

        _, returned = compile_evidence(tokac, PASS_RETURN, expected=0)
        require(find_record(returned, "return-transfer", "fulfilled",
                            "CedeConsumed", "lib.tk") is not None,
                "fulfilled cede return is missing")

        manager = run([
            toka, "cede-obligations", "--json", PASS_CALLER,
            "-o", temp_dir / ("manager" + suffix),
        ], expected=0, env=tool_env)
        require(manager.stdout == first,
                "toka cede-obligations does not preserve compiler output")

        mixed = run([
            tokac, "--cede-obligations=json", "--semantic-evidence=json",
            "--check-only", FAIL_CALLER,
        ], expected=1)
        require(not mixed.stdout,
                "incompatible JSON output modes emitted an ambiguous document")

    print("Cede Obligation Evidence v1 ABI gate PASSED")


if __name__ == "__main__":
    main()
