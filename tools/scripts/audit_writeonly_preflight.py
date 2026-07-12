#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TOKAC = os.path.abspath(os.environ.get(
    "TOKAC", os.path.join(ROOT, "build/bin/tokac")))
SOURCE = os.path.join(
    ROOT, "tests/semantics/memory_summary/writeonly_preflight/provider.tk")


def run(command):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    return result


def contract(document):
    matches = [entry for entry in document["records"]
               if entry["function"] == "write_payload" and
               entry["parameter"] == "data" and
               entry["contract"] == "writeonly"]
    if len(matches) != 1:
        raise RuntimeError("write_payload writeonly record is missing")
    return matches[0]


def summary(document):
    matches = [entry for entry in document["functions"]
               if entry["name"] == "write_payload"]
    if len(matches) != 1:
        raise RuntimeError("write_payload summary is missing")
    roots = [root for root in matches[0]["roots"] if root["name"] == "data"]
    if len(roots) != 1:
        raise RuntimeError("write_payload data root is missing")
    return matches[0], roots[0]


def main():
    if not os.path.isfile(TOKAC):
        raise RuntimeError("tokac not found: " + TOKAC)
    with tempfile.TemporaryDirectory(prefix="toka_writeonly_preflight_") as work:
        contract_result = run([
            TOKAC, "--dump-memory-contracts=json", "-c", SOURCE,
            "-o", os.path.join(work, "provider.o"),
        ])
        if contract_result.returncode != 0:
            raise RuntimeError(contract_result.stderr)
        contract_document = json.loads(contract_result.stdout)
        writeonly = contract(contract_document)

        summary_result = run([
            TOKAC, "--dump-memory-summaries=json", "-c", SOURCE,
            "-o", os.path.join(work, "summary.o"),
        ])
        if summary_result.returncode != 0:
            raise RuntimeError(summary_result.stderr)
        summary_document = json.loads(summary_result.stdout)
        function, root = summary(summary_document)

        unsupported = run([
            TOKAC, "--experimental-memory-contracts=writeonly", "-c",
            SOURCE, "-o", os.path.join(work, "unsupported.o"),
        ])
        if unsupported.returncode == 0 or \
                "unsupported experimental memory contract" not in unsupported.stderr:
            raise RuntimeError("writeonly must remain non-emitting")

    expected_effects = {"read", "write"}
    actual_effects = set(root["effects"])
    if writeonly["decision"] != "Reject" or \
            writeonly["reason"] != "ReadsMemory" or \
            actual_effects != expected_effects:
        raise RuntimeError("writeonly preflight result changed unexpectedly")
    document = {
        "case": "tests/semantics/memory_summary/writeonly_preflight/provider.tk",
        "decision": "StopBeforeOptimizerAudit",
        "reason": "SummaryLatticeCannotDistinguishPureWrite",
        "root_effects": sorted(actual_effects),
        "schema": "toka.writeonly-preflight",
        "summary_effects": function["effects"],
        "version": 1,
        "writeonly_record": {
            "decision": writeonly["decision"],
            "reason": writeonly["reason"],
            "emitted": writeonly["emitted"],
        },
    }
    print(json.dumps(document, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print("Writeonly preflight FAILED: %s" % error, file=sys.stderr)
        sys.exit(1)
