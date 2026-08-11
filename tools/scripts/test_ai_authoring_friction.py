#!/usr/bin/env python3

"""Executable baseline for recurring AI-authoring H/P misunderstandings."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests/tooling/ai_authoring_friction.v1.json"
DIMENSIONS = {
    "declaration-vs-assignment",
    "handle-vs-payload",
    "call-site-request-vs-declaration-authority",
    "option-borrowed-pattern",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def parse_single_document(stdout, case_id):
    require(stdout, case_id + " emitted no JSON on stdout")
    decoder = json.JSONDecoder()
    try:
        document, end = decoder.raw_decode(stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(case_id + " stdout is not JSON: " + str(error)) from error
    require(not stdout[end:].strip(), case_id + " emitted trailing stdout")
    require(isinstance(document, dict), case_id + " JSON root is not an object")
    return document


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    suffix = ".exe" if sys.platform == "win32" else ""
    toka = ROOT / args.build_dir / "bin" / ("toka" + suffix)
    require(toka.is_file(), "toka SDK manager is missing")

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    require(corpus.get("schema") == "toka.ai-authoring-friction" and
            corpus.get("version") == 1 and corpus.get("status") == "baseline",
            "AI authoring friction corpus identity changed")
    cases = corpus.get("cases")
    require(isinstance(cases, list) and len(cases) == len(DIMENSIONS),
            "AI authoring friction corpus must retain one case per dimension")
    require({case.get("dimension") for case in cases} == DIMENSIONS,
            "AI authoring friction dimensions drifted")

    checked = []
    for case in cases:
        case_id = case.get("id")
        expected = case.get("expected", {})
        repairs = case.get("repair_directions")
        require(isinstance(case_id, str) and case_id and
                isinstance(case.get("intent"), str) and case["intent"] and
                isinstance(repairs, list) and len(repairs) == 2 and
                all(isinstance(repair, str) and repair for repair in repairs),
                "AI authoring friction case is incomplete")
        source = ROOT / case.get("path", "")
        require(source.is_file(), case_id + " source fixture is missing")
        result = subprocess.run(
            [str(toka), "check", "--json", str(source)], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(result.returncode == expected.get("exit"),
                "%s exit changed: expected %r, got %d\n%s%s" % (
                    case_id, expected.get("exit"), result.returncode,
                    result.stdout, result.stderr))
        document = parse_single_document(result.stdout, case_id)
        require(document.get("schema") == "toka.diagnostics" and
                document.get("version") == 2 and not document.get("success"),
                case_id + " did not expose rejected semantics as diagnostics JSON")
        require(any(diagnostic.get("code") == expected.get("code")
                    for diagnostic in document.get("diagnostics", [])),
                case_id + " diagnostic code changed")
        checked.append(case_id)

    print(json.dumps({
        "cases": checked,
        "count": len(checked),
        "result": "pass",
        "schema": "toka.ai-authoring-friction-test",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
