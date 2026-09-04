#!/usr/bin/env python3
"""Qualification gate for signature-driven cede evidence v2 and its lint."""

import argparse
import json
import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/semantics/signature_driven_cede_direct/runtime.tk"
FLAG = "--experimental-signature-driven-cede"
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"

    first = run([str(tokac), FLAG, "--cede-obligations=v2", "--check-only",
                 str(SOURCE)])
    second = run([str(tokac), FLAG, "--cede-obligations=v2", "--check-only",
                  str(SOURCE)])
    require(first.returncode == 0 and first.stdout == second.stdout and
            first.stderr == second.stderr,
            "cede evidence v2 is not deterministic")
    payload = json.loads(first.stdout)
    require(set(payload) == {"schema", "version", "records"} and
            payload["schema"] == "toka.cede-obligation-evidence" and
            payload["version"] == 2,
            "cede evidence v2 envelope changed")

    caller = [record for record in payload["records"]
              if record["stage"] == "caller-transfer"]
    expected_keys = {"stage", "status", "reason", "subject", "origin",
                     "spelling", "transfer", "source", "location",
                     "contract_location"}
    require(caller and all(set(record) == expected_keys for record in caller),
            "cede evidence v2 caller schema changed")
    facts = {(record["spelling"], record["transfer"], record["source"])
             for record in caller}
    require(("implicit", "MoveOwned", "InvalidatePlace") in facts and
            ("implicit", "CopyValue", "KeepLive") in facts and
            ("implicit", "ConsumeTemporary", "NoSourcePlace") in facts,
            "cede evidence v2 lost its orthogonal transfer matrix")
    noncaller = [record for record in payload["records"]
                 if record["stage"] != "caller-transfer"]
    require(noncaller and all(record["spelling"] is None and
                              record["transfer"] is None and
                              record["source"] is None
                              for record in noncaller),
            "callee/return v2 records invented caller facts")

    lint = run([str(tokac), FLAG, "--warn-implicit-call-move", "--check-only",
                str(SOURCE)])
    require(lint.returncode == 0 and lint.stderr.count("W0409") == 1 and
            "source" in lint.stderr,
            "implicit-call-move lint did not report only invalidating places")

    print("Cede Obligation Evidence v2 and lint tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Cede Obligation Evidence v2 tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
