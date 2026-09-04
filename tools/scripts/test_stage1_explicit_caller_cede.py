#!/usr/bin/env python3

"""Qualify Stage-1 caller-side explicit cede for ordinary parameters."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_explicit_cede"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check(tokac, name, *extra):
    return subprocess.run(
        [str(tokac), *extra, "--check-only", str(FIXTURES / name)],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    named_failures = (
        "named_copy_requires_cede.tk",
        "named_unique_requires_cede.tk",
        "named_copy_aggregate_requires_cede.tk",
        "static_named_copy_requires_cede.tk",
        "extern_named_copy_requires_cede.tk",
        "generic_named_copy_requires_cede.tk",
    )
    for name in named_failures:
        result = check(tokac, name)
        require(result.returncode != 0 and
                result.stderr.count("error[E04570]") == 1 and
                "E0438" not in result.stderr and
                "E0410" not in result.stderr,
                name + " did not reject one bare named source atomically")

    structured = check(tokac, "named_copy_requires_cede.tk",
                       "--diagnostics-json")
    document = json.loads(structured.stdout)
    diagnostic = next(item for item in document["diagnostics"]
                      if item["code"] == "E04570")
    require(len(diagnostic["fixes"]) == 1 and
            diagnostic["fixes"][0]["applicability"] ==
            "machine-applicable" and
            diagnostic["fixes"][0]["edits"] == [{
                "file": diagnostic["primary"]["file"],
                "newText": "cede ",
                "range": {
                    "start": diagnostic["primary"]["range"]["start"],
                    "end": diagnostic["primary"]["range"]["start"],
                },
            }], "E04570 has no exact machine-applicable cede insertion")

    unique_structured = check(tokac, "named_unique_requires_cede.tk",
                              "--diagnostics-json")
    unique_document = json.loads(unique_structured.stdout)
    unique_diagnostic = next(item for item in unique_document["diagnostics"]
                             if item["code"] == "E04570")
    unique_edit = unique_diagnostic["fixes"][0]["edits"][0]
    unique_start = unique_edit["range"]["start"]
    unique_line = (FIXTURES / "named_unique_requires_cede.tk").read_text(
        encoding="utf-8").splitlines()[unique_start["line"]]
    require(unique_edit["newText"] == "cede " and
            unique_edit["range"]["end"] == unique_start and
            unique_line[unique_start["character"]:].startswith("^value"),
            "E04570 fix did not preserve the selected unique-handle view")

    multi = check(tokac, "multi_argument_atomic_rejection.tk")
    require(multi.returncode != 0 and
            multi.stderr.count("error[E04570]") == 1 and
            "E0438" not in multi.stderr and "E0410" not in multi.stderr and
            "E0474" not in multi.stderr,
            "multi-argument rejection mutated a source before commit")

    explicit = check(tokac, "explicit_copy_invalidates.tk")
    require(explicit.returncode != 0 and "error[E0438]" in explicit.stderr,
            "explicit cede of Copy did not invalidate its source")

    positive = check(tokac, "temporary_and_copy_keep_live.tk")
    require(positive.returncode == 0 and not positive.stderr,
            "Copy KeepLive or no-source temporary exemption regressed")
    with tempfile.TemporaryDirectory(prefix="toka-stage1-cede-") as temp:
        artifact = Path(temp) / "stage1-positive"
        built = subprocess.run(
            [str(tokac), str(FIXTURES / "temporary_and_copy_keep_live.tk"),
             "-o", str(artifact)], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=30)
        require(built.returncode == 0 and artifact.is_file(), built.stderr)
        ran = subprocess.run(
            [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=10)
        require(ran.returncode == 0 and not ran.stderr,
                "Stage-1 positive artifact failed")

    source = "named_copy_requires_cede.tk"
    normal = check(tokac, source)
    shadow = check(tokac, source, "--call-transfer-shadow=json")
    require(normal.returncode == shadow.returncode and
            normal.stderr == shadow.stderr,
            "call shadow did not inherit activated Stage-1 diagnostics")
    payload = json.loads(shadow.stdout)
    transactions = [
        transaction for transaction in payload["transactions"]
        if transaction["callee"] == "consume" and
        transaction["location"]["file"].endswith(source)]
    require(len(transactions) == 1 and
            transactions[0]["outcome"] == "Rejected" and
            transactions[0]["rejection"] == "WholeCallItemRejected" and
            not transactions[0]["commit_allowed"] and
            transactions[0]["items"][0]["rejection"] ==
            "MissingCedeForNamedSource" and
            transactions[0]["items"][0]["source"] == "NoStateChange",
            "Stage-1 diagnostic and frozen Stage-0 plan disagree")

    legacy = check(tokac, source, "--stage1-legacy-ordinary-cede")
    require(legacy.returncode == 0,
            "explicit historical replay flag no longer preserves v5 fixture")

    print("stage1 explicit caller cede: pass")


if __name__ == "__main__":
    main()
