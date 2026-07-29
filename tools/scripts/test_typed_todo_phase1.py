#!/usr/bin/env python3

"""Fail-closed parser and semantic-boundary gate for Typed Todo v1."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CASES = {
    "explicit_contract.tk": "E04603",
    "underconstrained.tk": "E04604",
    "unsupported_prefix.tk": "E04605",
    "unsupported_postfix.tk": "E04605",
    "unsupported_cede.tk": "E04605",
    "assignment_contract.tk": "E04603",
    "if_bool_contract.tk": "E04603",
    "loop_bool_contract.tk": "E04603",
    "call_contract.tk": "E04603",
    "explicit_generic_contract.tk": "E04603",
    "generic_inference_underconstrained.tk": "E04604",
    "cede_parameter_unsupported.tk": "E04605",
}
RESERVED_IDENTIFIER = "reserved_identifier.tk"
FORMER_KEYWORD_IDENTIFIER = "former_keyword_identifier.tk"


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    require(tokac.is_file(), "tokac is missing")

    source_dir = ROOT / "tests/tooling/typed_todo"
    for name, code in CASES.items():
        source = source_dir / name
        result = run([tokac, "--diagnostics-json", "--check-only", source], 1)
        payload = json.loads(result.stdout)
        require(payload["schema"] == "toka.diagnostics" and
                payload["version"] == 2 and not payload["success"],
                name + " did not produce a failed diagnostic envelope")
        diagnostics = payload["diagnostics"]
        require(len(diagnostics) == 1 and diagnostics[0]["code"] == code,
                name + " did not produce the required primary todo diagnostic")

    reserved = source_dir / RESERVED_IDENTIFIER
    result = run([tokac, "--diagnostics-json", "--check-only", reserved], 1)
    payload = json.loads(result.stdout)
    require(not payload["success"] and payload["diagnostics"],
            "the reserved todo keyword was accepted as an identifier")

    former_keyword = source_dir / FORMER_KEYWORD_IDENTIFIER
    run([tokac, "--check-only", former_keyword], 0)

    with tempfile.TemporaryDirectory(prefix="toka-todo-boundary-") as temp:
        temp_dir = Path(temp)
        output = temp_dir / "must-not-exist"
        run([tokac, source_dir / "explicit_contract.tk", "-o", output], 1)
        require(not any(temp_dir.iterdir()),
                "a program containing todo emitted an executable or interface")
        run([tokac, source_dir / "explicit_contract.tk", "-c", "-o", output], 1)
        require(not any(temp_dir.iterdir()),
                "a program containing todo emitted an object, interface, or cache artifact")

    print("Typed Todo v1 phase 1 boundary gate passed")


if __name__ == "__main__":
    main()
