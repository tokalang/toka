#!/usr/bin/env python3

"""ABI gate for the narrow Typed Hole Conditional Facts v1 binding slice."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/tooling/typed_hole/conditional_binding_facts.tk"
UNDERCONSTRAINED = ROOT / "tests/tooling/typed_hole/underconstrained.tk"


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


def validate(payload):
    require(set(payload) == {"schema", "version", "facts"},
            "conditional-facts envelope changed")
    require(payload["schema"] == "toka.conditional-facts",
            "conditional-facts schema changed")
    require(payload["version"] == 1, "conditional-facts version changed")
    facts = payload["facts"]
    require(isinstance(facts, list) and len(facts) == 3,
            "fixture must produce three conditional binding facts")
    expected_symbols = ["answer", "forwarded", "forwarded_again"]
    for fact, symbol in zip(facts, expected_symbols):
        require(set(fact) == {"symbol", "type", "status", "conditional_on", "location"},
                "fact fields changed")
        require(fact["symbol"] == symbol and fact["type"] == "i32",
                "binding fact identity changed")
        require(fact["status"] == "conditional" and fact["conditional_on"] == [1],
                "hole dependency did not propagate through a direct alias")
        location = fact["location"]
        require(set(location) == {"file", "line", "column"},
                "location fields changed")
        require(isinstance(location["file"], str) and
                isinstance(location["line"], int) and
                isinstance(location["column"], int),
                "location values changed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    schema = ROOT / "schemas/toka.conditional-facts.v1.schema.json"
    require(tokac.is_file() and toka.is_file() and schema.is_file(),
            "Conditional Facts prerequisites are missing")
    schema_doc = json.loads(schema.read_text(encoding="utf-8"))
    require(schema_doc["properties"]["schema"] == {"const": "toka.conditional-facts"},
            "schema identity is not frozen")
    require(schema_doc["properties"]["version"] == {"const": 1},
            "schema version is not frozen")

    first = run([tokac, "--conditional-facts=json", "--check-only", SOURCE], 1)
    second = run([tokac, "--conditional-facts=json", "--check-only", SOURCE], 1)
    require(first.stdout == second.stdout, "conditional facts are not deterministic")
    payload = json.loads(first.stdout)
    validate(payload)

    unavailable = run([tokac, "--conditional-facts=json", "--check-only",
                       UNDERCONSTRAINED], 1)
    unavailable_payload = json.loads(unavailable.stdout)
    require(unavailable_payload == {
        "schema": "toka.conditional-facts", "version": 1, "facts": []
    }, "an underconstrained hole fabricated a conditional binding fact")

    wrapped = run([toka, "conditional-facts", "--json", "--check-only", SOURCE], 1)
    require(json.loads(wrapped.stdout) == payload,
            "SDK wrapper changed conditional-facts output")
    print("Typed Hole Conditional Facts v1 binding gate passed")


if __name__ == "__main__":
    main()
