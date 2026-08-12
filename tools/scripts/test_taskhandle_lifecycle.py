#!/usr/bin/env python3

"""Fail-closed v2 TaskHandle contract and revision-bound evidence gate."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
OPERATIONS = {"create", "activate", "await", "await-outcome", "cancel", "drop", "detach"}
RESULT_STATES = {"pending", "ready-live", "taken", "canceled"}
QUALIFICATION_FIELDS = {
    "status", "qualified_scope", "known_behavior", "unqualified_targets",
    "native_evidence",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def canonical_sha256(document):
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def revision():
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
    ).strip()


def write_conformance(path, contract, evidence):
    document = {
        "schema": "toka.taskhandle-lifecycle-conformance",
        "version": 1,
        "candidate_revision": revision(),
        "contract": {
            "schema": contract["schema"],
            "version": contract["version"],
            "path": "spec/taskhandle_lifecycle.v2.json",
            "canonical_sha256": canonical_sha256(contract),
        },
        "evidence": evidence,
        "result": "pass",
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--timeout", type=int, default=10)
    parser.add_argument("--conformance-output", type=Path)
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    contract_path = ROOT / "spec/taskhandle_lifecycle.v2.json"
    schema_path = ROOT / "schemas/toka.taskhandle-lifecycle.v2.schema.json"
    historical_contract_path = ROOT / "spec/taskhandle_lifecycle.v1.json"
    historical_schema_path = ROOT / "schemas/toka.taskhandle-lifecycle.v1.schema.json"
    require(tokac.is_file(), "tokac is missing")
    require(contract_path.is_file() and schema_path.is_file(), "TaskHandle v2 contract files are missing")
    require(historical_contract_path.is_file() and historical_schema_path.is_file(),
            "historical TaskHandle v1 files are missing")

    historical_schema = json.loads(historical_schema_path.read_text(encoding="utf-8"))
    historical_contract = json.loads(historical_contract_path.read_text(encoding="utf-8"))
    require(historical_schema["properties"]["version"] == {"const": 1},
            "historical lifecycle schema version changed")
    require(set(historical_contract) == {"schema", "version", "operations", "result_states", "redline_tests"},
            "historical lifecycle v1 fields changed")

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    require(schema["properties"]["schema"] == {"const": "toka.taskhandle-lifecycle"},
            "lifecycle schema identity is not frozen")
    require(schema["properties"]["version"] == {"const": 2},
            "lifecycle schema version is not frozen")
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    require(set(contract) == {"schema", "version", "qualification", "qualified_guarantees", "operations", "result_states", "redline_tests"},
            "lifecycle v2 contract fields changed")
    require(contract["schema"] == "toka.taskhandle-lifecycle" and contract["version"] == 2,
            "lifecycle v2 contract identity changed")
    guarantees = {item["id"]: item["statement"] for item in contract["qualified_guarantees"]}
    require(set(guarantees) == {"TH-G1", "TH-G2", "TH-G3", "TH-G4", "TH-G5", "TH-G6", "TH-G7", "TH-G8", "TH-G9"},
            "qualified guarantee IDs changed")
    require(all(statement for statement in guarantees.values()), "qualified guarantee statement is empty")
    require({item["name"] for item in contract["operations"]} == OPERATIONS,
            "lifecycle operations are incomplete or changed")
    require({item["name"] for item in contract["result_states"]} == RESULT_STATES,
            "result-state contract is incomplete or changed")
    require({item["value"] for item in contract["result_states"]} == {0, 1, 2, 3},
            "result-state values changed")
    qualification = contract["qualification"]
    require(set(qualification) == QUALIFICATION_FIELDS, "qualification record fields changed")
    require(qualification["status"] == "qualified-subset", "lifecycle contract must state qualified subset")
    for field in ("qualified_scope", "known_behavior", "unqualified_targets"):
        require(isinstance(qualification[field], list) and qualification[field],
                "qualification field is empty: " + field)
    unqualified = "\n".join(qualification["unqualified_targets"])
    for target in ("post-normal-claim cancellation suppression", "cancel-join-drain", "TaskScope", "PlaceState"):
        require(target in unqualified, "qualification target is not explicitly deferred: " + target)
    native_evidence = qualification["native_evidence"]
    require(isinstance(native_evidence, list) and native_evidence, "qualification native evidence is empty")
    evidence = []

    for operation in contract["operations"]:
        require(set(operation) == {"name", "source_states", "target_states", "handle_obligation", "result_obligation", "guarantees"},
                "operation record fields changed")
        require(operation["source_states"] and operation["target_states"] and operation["guarantees"],
                "operation record is empty")
        operation_guarantees = "\n".join(operation["guarantees"])
        require("cancel-join-drain" not in operation_guarantees and
                "suppress source-visible delivery" not in operation_guarantees,
                "operation overstates an unqualified cancellation guarantee")
    for state in contract["result_states"]:
        require(set(state) == {"name", "value", "meaning", "successor_states"},
                "result-state fields changed")

    with tempfile.TemporaryDirectory(prefix="toka-taskhandle-lifecycle-") as temp:
        temp_dir = Path(temp)
        for index, redline in enumerate(contract["redline_tests"]):
            require(set(redline) == {"path", "proves", "guarantee_ids"} and redline["proves"],
                    "redline test record changed")
            require(set(redline["guarantee_ids"]).issubset(guarantees),
                    "redline names an unknown guarantee")
            source = ROOT / redline["path"]
            require(source.is_file(), "redline source is missing: %s" % source)
            executable = temp_dir / ("redline-%d" % index + suffix)
            compile_result = subprocess.run([str(tokac), str(source), "-o", str(executable)], cwd=ROOT,
                                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            require(compile_result.returncode == 0, "redline failed to compile: %s\n%s%s" %
                    (source, compile_result.stdout, compile_result.stderr))
            try:
                run_result = subprocess.run([str(executable)], cwd=ROOT, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.timeout)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError("redline timed out: %s\n%s%s" %
                                   (source, error.stdout or "", error.stderr or ""))
            require(run_result.returncode == 0, "redline failed: %s\n%s%s" %
                    (source, run_result.stdout, run_result.stderr))
            evidence.append({"kind": "redline", "path": redline["path"],
                             "guarantee_ids": redline["guarantee_ids"], "result": "pass"})

    for native in native_evidence:
        require(set(native) == {"target", "path", "proves", "guarantee_ids"} and native["target"] and native["proves"],
                "native evidence record changed")
        require(set(native["guarantee_ids"]).issubset(guarantees), "native evidence names an unknown guarantee")
        source = ROOT / native["path"]
        require(source.is_file(), "native evidence source is missing: %s" % source)
        result = subprocess.run(["ctest", "--test-dir", str(ROOT / args.build_dir), "--output-on-failure",
                                 "-R", "^%s$" % native["target"]], cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(result.returncode == 0, "native lifecycle evidence failed: %s\n%s%s" %
                (native["target"], result.stdout, result.stderr))
        evidence.append({"kind": "native", "target": native["target"], "path": native["path"],
                         "guarantee_ids": native["guarantee_ids"], "result": "pass"})

    if args.conformance_output:
        write_conformance(args.conformance_output.resolve(), contract, evidence)
    print("TaskHandle Lifecycle Contract v2 gate PASSED")


if __name__ == "__main__":
    main()
