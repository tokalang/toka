#!/usr/bin/env python3

"""Fail-closed ABI gate for Typed Todo Goals v1."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = ROOT / "tests/tooling/typed_todo"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, expected):
    result = subprocess.run([str(part) for part in command], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != expected:
        raise RuntimeError("expected exit %d, got %d: %s\n%s%s" % (
            expected, result.returncode, " ".join(map(str, command)),
            result.stdout, result.stderr))
    return result


def validate_location(location):
    require(set(location) == {"file", "line", "column"},
            "location fields changed")
    require(isinstance(location["file"], str), "location file is not text")
    require(isinstance(location["line"], int) and location["line"] >= 0,
            "location line is invalid")
    require(isinstance(location["column"], int) and location["column"] >= 0,
            "location column is invalid")


def validate_document(payload):
    require(set(payload) == {"schema", "version", "goals"},
            "todo-goals envelope fields changed")
    require(payload["schema"] == "toka.todo-goals", "todo-goals schema changed")
    require(payload["version"] == 1, "todo-goals version changed")
    require(isinstance(payload["goals"], list), "goals is not an array")
    for goal in payload["goals"]:
        require(set(goal) == {"id", "location", "status", "contract"},
                "goal fields changed")
        require(isinstance(goal["id"], int) and goal["id"] > 0,
                "goal id is invalid")
        validate_location(goal["location"])
        require(goal["status"] in {"incomplete", "underconstrained", "unsupported"},
                "unknown goal status")
        contract = goal["contract"]
        if contract is None:
            require(goal["status"] != "incomplete",
                    "incomplete goal omitted its contract")
            continue
        require(goal["status"] == "incomplete",
                "non-incomplete goal fabricated a contract")
        require(set(contract) == {"type", "morphology", "transfer", "permissions", "nullable", "required_dependencies"},
                "contract fields changed")
        require(contract["morphology"] in {"value", "raw", "unique", "shared", "reference"},
                "unknown morphology")
        require(contract["transfer"] == "none", "v1 unexpectedly accepts transfer")
        require(set(contract["permissions"]) == {"handle_rebind", "payload_write"},
                "permission fields changed")
        require(all(isinstance(value, bool) for value in contract["permissions"].values()),
                "permission value is not boolean")
        require(isinstance(contract["nullable"], bool), "nullable is not boolean")
        require(isinstance(contract["required_dependencies"], list),
                "dependencies is not an array")


def compile_goals(tokac, source):
    result = run([tokac, "--todo-goals=json", "--check-only", source], 1)
    payload = json.loads(result.stdout)
    validate_document(payload)
    require(len(payload["goals"]) == 1, "fixture should emit one todo goal")
    return result.stdout, payload["goals"][0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    schema = ROOT / "schemas/toka.todo-goals.v1.schema.json"
    require(tokac.is_file() and toka.is_file() and schema.is_file(),
            "Typed Todo Goals prerequisites are missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["schema"] == {"const": "toka.todo-goals"},
            "schema identity is not frozen")
    require(schema_doc["properties"]["version"] == {"const": 1},
            "schema version is not frozen")

    first, complete = compile_goals(tokac, SOURCE_DIR / "explicit_contract.tk")
    second, repeated = compile_goals(tokac, SOURCE_DIR / "explicit_contract.tk")
    require(first == second and complete == repeated,
            "todo-goals output is not deterministic")
    require(complete["status"] == "incomplete", "explicit contract status")
    contract = complete["contract"]
    require(contract["type"] == "i32" and contract["morphology"] == "value",
            "explicit i32 contract changed")
    require(contract["permissions"] == {"handle_rebind": False, "payload_write": False},
            "value contract permissions changed")

    _, assignment = compile_goals(tokac, SOURCE_DIR / "assignment_contract.tk")
    require(assignment["status"] == "incomplete" and
            assignment["contract"]["permissions"]["payload_write"],
            "assignment contract lost payload-write requirement")

    _, underconstrained = compile_goals(
        tokac, SOURCE_DIR / "generic_inference_underconstrained.tk")
    require(underconstrained["status"] == "underconstrained" and
            underconstrained["contract"] is None,
            "generic inference todo is not underconstrained")

    _, unsupported = compile_goals(tokac, SOURCE_DIR / "unsupported_cede.tk")
    require(unsupported["status"] == "unsupported" and
            unsupported["contract"] is None,
            "cede todo is not unsupported")

    _, cede_parameter = compile_goals(
        tokac, SOURCE_DIR / "cede_parameter_unsupported.tk")
    require(cede_parameter["status"] == "unsupported" and
            cede_parameter["contract"] is None,
            "cede parameter todo was not rejected as an unsupported transfer")

    wrapper = run([toka, "todo-goals", "--json", "--check-only",
                   SOURCE_DIR / "explicit_contract.tk"], 1)
    require(json.loads(wrapper.stdout) == json.loads(first),
            "SDK wrapper changed Typed Todo Goals output")

    print("Typed Todo Goals v1 ABI gate passed")


if __name__ == "__main__":
    main()
