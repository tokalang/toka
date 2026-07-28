#!/usr/bin/env python3

"""Fail-closed contract and executable-redline gate for TaskHandle Lifecycle v1."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
OPERATIONS = {"create", "activate", "await", "cancel", "drop", "detach"}
RESULT_STATES = {"pending", "ready-live", "taken", "canceled"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--timeout", type=int, default=10)
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    contract_path = ROOT / "spec/taskhandle_lifecycle.v1.json"
    schema_path = ROOT / "schemas/toka.taskhandle-lifecycle.v1.schema.json"
    require(tokac.is_file(), "tokac is missing")
    require(contract_path.is_file() and schema_path.is_file(),
            "TaskHandle lifecycle contract files are missing")

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    require(schema["properties"]["schema"] == {"const": "toka.taskhandle-lifecycle"},
            "lifecycle schema identity is not frozen")
    require(schema["properties"]["version"] == {"const": 1},
            "lifecycle schema version is not frozen")
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    require(set(contract) == {"schema", "version", "operations", "result_states", "redline_tests"},
            "lifecycle contract fields changed")
    require(contract["schema"] == "toka.taskhandle-lifecycle" and contract["version"] == 1,
            "lifecycle contract identity changed")
    require({item["name"] for item in contract["operations"]} == OPERATIONS,
            "lifecycle operations are incomplete or changed")
    require({item["name"] for item in contract["result_states"]} == RESULT_STATES,
            "result-state contract is incomplete or changed")
    require({item["value"] for item in contract["result_states"]} == {0, 1, 2, 3},
            "result-state values changed")
    for operation in contract["operations"]:
        require(set(operation) == {"name", "source_states", "target_states", "handle_obligation", "result_obligation", "guarantees"},
                "operation record fields changed")
        require(operation["source_states"] and operation["target_states"] and operation["guarantees"],
                "operation record is empty")
    for state in contract["result_states"]:
        require(set(state) == {"name", "value", "meaning", "successor_states"},
                "result-state fields changed")

    with tempfile.TemporaryDirectory(prefix="toka-taskhandle-lifecycle-") as temp:
        temp_dir = Path(temp)
        for index, redline in enumerate(contract["redline_tests"]):
            require(set(redline) == {"path", "proves"} and redline["proves"],
                    "redline test record changed")
            source = ROOT / redline["path"]
            require(source.is_file(), "redline source is missing: %s" % source)
            executable = temp_dir / ("redline-%d" % index + suffix)
            compile_result = subprocess.run(
                [str(tokac), str(source), "-o", str(executable)], cwd=ROOT,
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            require(compile_result.returncode == 0,
                    "redline failed to compile: %s\n%s%s" %
                    (source, compile_result.stdout, compile_result.stderr))
            try:
                run_result = subprocess.run(
                    [str(executable)], cwd=ROOT, text=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    timeout=args.timeout,
                )
            except subprocess.TimeoutExpired as error:
                raise RuntimeError("redline timed out: %s\n%s%s" %
                                   (source, error.stdout or "", error.stderr or ""))
            require(run_result.returncode == 0,
                    "redline failed: %s\n%s%s" %
                    (source, run_result.stdout, run_result.stderr))

    print("TaskHandle Lifecycle Contract v1 gate PASSED")


if __name__ == "__main__":
    main()
