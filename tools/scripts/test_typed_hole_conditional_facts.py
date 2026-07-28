#!/usr/bin/env python3

"""ABI gate for the narrow Typed Hole Conditional Facts v1 binding slice."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/tooling/typed_hole/conditional_binding_facts.tk"
EXPRESSION_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_expression_facts.tk"
IF_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_if_facts.tk"
MATCH_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_match_facts.tk"
ASSIGNMENT_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_assignment_facts.tk"
ASSIGNMENT_RESET_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_assignment_reset.tk"
LOOP_BOUNDARY_SOURCE = ROOT / "tests/tooling/typed_hole/conditional_loop_boundary.tk"
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

    expression = run([tokac, "--conditional-facts=json", "--check-only",
                      EXPRESSION_SOURCE], 1)
    expression_payload = json.loads(expression.stdout)
    expression_facts = expression_payload["facts"]
    require([fact["symbol"] for fact in expression_facts] == [
        "answer", "arithmetic", "through_call"
    ], "expression/call conditional facts were not emitted deterministically")
    require(all(fact["conditional_on"] == [1] for fact in expression_facts),
            "expression/call propagation lost the source hole")

    if_result = run([tokac, "--conditional-facts=json", "--check-only",
                     IF_SOURCE], 1)
    if_facts = json.loads(if_result.stdout)["facts"]
    require([fact["symbol"] for fact in if_facts] == ["answer", "live"],
            "conditional if join did not preserve live or suppress dead branch facts")
    require(all(fact["conditional_on"] == [1] for fact in if_facts),
            "conditional if join lost the source hole")

    match_result = run([tokac, "--conditional-facts=json", "--check-only",
                        MATCH_SOURCE], 1)
    match_facts = json.loads(match_result.stdout)["facts"]
    require([fact["symbol"] for fact in match_facts] == ["answer", "chosen"],
            "conditional match join did not preserve the live arm dependency")
    require(all(fact["conditional_on"] == [1] for fact in match_facts),
            "conditional match join lost the source hole")

    assignment_result = run([tokac, "--conditional-facts=json", "--check-only",
                             ASSIGNMENT_SOURCE], 1)
    assignment_facts = json.loads(assignment_result.stdout)["facts"]
    require([fact["symbol"] for fact in assignment_facts] ==
                ["answer", "observed"],
            "conditional assignment did not update the later declaration")
    require(all(fact["conditional_on"] == [1]
                for fact in assignment_facts),
            "conditional assignment lost the source hole")

    reset_result = run([tokac, "--conditional-facts=json", "--check-only",
                        ASSIGNMENT_RESET_SOURCE], 1)
    reset_facts = json.loads(reset_result.stdout)["facts"]
    require([fact["symbol"] for fact in reset_facts] == ["answer"],
            "a complete direct assignment did not clear the stale dependency")

    loop_result = run([tokac, "--conditional-facts=json", "--check-only",
                       LOOP_BOUNDARY_SOURCE], 1)
    loop_facts = json.loads(loop_result.stdout)["facts"]
    require([fact["symbol"] for fact in loop_facts] == ["answer"],
            "loop assignment leaked beyond the v1 dataflow boundary")

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
