#!/usr/bin/env python3

"""Qualify specialization-scoped generic-body Stage-0 call evidence."""

import argparse
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
VALID = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_body_call_qualification.tk")
NESTED = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_body_call_nested_qualification.tk")
INVALID = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_body_call_invalid_rollback.tk")
NESTED_SURVIVES = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_body_nested_valid_survives_invalid_parent.tk")
RECURSIVE = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_validation_recursion.tk")

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def invoke(tokac, source, mode):
    command = [str(tokac)]
    if mode == "qualification":
        command.append("--generic-body-call-qualification=json")
    elif mode == "v5":
        command.append("--call-transfer-shadow=json")
    command.extend(("--check-only", source))
    return subprocess.run(
        command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30)


def qualification_payload(tokac, source, expected_error=None):
    normal = invoke(tokac, source, "normal")
    observed = invoke(tokac, source, "qualification")
    replay = invoke(tokac, source, "qualification")
    require(normal.returncode == observed.returncode and
            normal.stderr == observed.stderr,
            source + " qualification mode changed normal diagnostics")
    require(observed.returncode == replay.returncode and
            observed.stderr == replay.stderr and
            observed.stdout == replay.stdout,
            source + " qualification evidence is not deterministic")
    if expected_error:
        require(normal.returncode != 0 and expected_error in normal.stderr,
                source + " did not retain expected " + expected_error)
    else:
        require(normal.returncode == 0, normal.stderr)
    payload = json.loads(observed.stdout)
    require(payload.get("schema") ==
            "toka.internal.generic-body-call-qualification" and
            payload.get("version") == 1 and
            payload.get("status") == "audit-only",
            source + " emitted the wrong qualification envelope")
    require(all("specialization_identity" in record
                for record in payload.get("records", ())) and
            all("specialization_identity" in transaction
                for transaction in payload.get("transactions", ())) and
            isinstance(payload.get("specializations"), list) and
            all(summary.get("specialization_identity") and
                summary.get("validation") == "Valid" and
                summary.get("qualification_complete") is True and
                isinstance(summary.get("receipt_count"), int) and
                isinstance(summary.get("transaction_count"), int)
                for summary in payload["specializations"]),
            source + " omitted specialization identity")
    return payload


def body_transactions(payload, source, callee):
    return [
        transaction for transaction in payload["transactions"]
        if transaction["callee"] == callee and
        transaction["specialization_identity"] and
        transaction["location"]["file"].endswith(source)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    valid = qualification_payload(tokac, VALID)
    by_callee = {
        callee: body_transactions(valid, VALID, callee)
        for callee in ("ordinary_body", "read", "consuming_body")
    }
    specialization_sets = []
    for callee, transactions in by_callee.items():
        identities = {transaction["specialization_identity"]
                      for transaction in transactions}
        require(len(transactions) == 2 and len(identities) == 2 and
                all(transaction["commit_allowed"] and
                    transaction["outcome"] == "Admitted"
                    for transaction in transactions) and
                all("/private/" not in identity and
                    str(ROOT) not in identity
                    for identity in identities),
                VALID + " did not publish " + callee +
                " exactly once per specialization")
        specialization_sets.append(identities)
    require(all(identities == specialization_sets[0]
                for identities in specialization_sets[1:]),
            VALID + " routes disagree on specialization identity")
    require({summary["specialization_identity"]
             for summary in valid["specializations"]} ==
            specialization_sets[0] and
            all(summary["qualification_complete"]
                for summary in valid["specializations"]),
            VALID + " omitted a specialization completion marker")

    argument_records = [
        record for record in valid["records"]
        if record["callee"] in ("ordinary_body", "consuming_body") and
        record["specialization_identity"] and
        record["location"]["file"].endswith(VALID)]
    require(len(argument_records) == 4,
            VALID + " omitted specialization-scoped argument receipts")
    for record in argument_records:
        matches = [
            transaction for transaction in by_callee[record["callee"]]
            if transaction["specialization_identity"] ==
            record["specialization_identity"]]
        require(len(matches) == 1,
                VALID + " transaction/receipt specialization mismatch")

    v5 = invoke(tokac, VALID, "v5")
    require(v5.returncode == 0, v5.stderr)
    v5_payload = json.loads(v5.stdout)
    require(v5_payload.get("version") == 5 and
            all("specialization_identity" not in record
                for record in v5_payload["records"]) and
            all("specialization_identity" not in transaction
                for transaction in v5_payload["transactions"]) and
            not any(transaction["callee"] in by_callee and
                    transaction["location"]["file"].endswith(VALID)
                    for transaction in v5_payload["transactions"]),
            "frozen call-transfer v5 exposed generic-body qualification")
    conflict = subprocess.run(
        [str(tokac), "--call-transfer-shadow=json",
         "--generic-body-call-qualification=json", "--check-only", VALID],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        timeout=30)
    require(conflict.returncode != 0 and not conflict.stdout and
            "cannot be combined" in conflict.stderr,
            "v5 and generic-body qualification modes were combined")

    nested = qualification_payload(tokac, NESTED)
    inner = body_transactions(nested, NESTED, "inner")
    leaf = body_transactions(nested, NESTED, "leaf")
    require(len(inner) == 2 and len(leaf) == 2 and
            all(transaction["commit_allowed"]
                for transaction in inner + leaf) and
            len({transaction["specialization_identity"]
                 for transaction in inner}) == 2 and
            len({transaction["specialization_identity"]
                 for transaction in leaf}) == 2,
            NESTED + " did not qualify nested generic bodies")
    require(len(nested["specializations"]) == 4,
            NESTED + " omitted nested specialization completion markers")

    invalid = qualification_payload(tokac, INVALID, expected_error="E0408")
    require(not body_transactions(invalid, INVALID, "body_call") and
            not any(record["callee"] == "body_call" and
                    record["specialization_identity"]
                    for record in invalid["records"]) and
            not invalid["specializations"],
            INVALID + " retained evidence from an invalid specialization")

    survives = qualification_payload(
        tokac, NESTED_SURVIVES, expected_error="E0408")
    surviving_leaf = body_transactions(survives, NESTED_SURVIVES, "leaf")
    rejected_outer_calls = body_transactions(
        survives, NESTED_SURVIVES, "inner")
    require(len(surviving_leaf) == 1 and
            surviving_leaf[0]["commit_allowed"] and
            not rejected_outer_calls and
            len(survives["specializations"]) == 1 and
            survives["specializations"][0]["specialization_identity"] ==
            surviving_leaf[0]["specialization_identity"],
            NESTED_SURVIVES + " allowed an invalid parent journal to erase "
            "a valid nested specialization or retain the invalid parent edge")

    recursive = qualification_payload(tokac, RECURSIVE, expected_error="E0406")
    require(not recursive["specializations"] and
            not any(transaction["specialization_identity"]
                    for transaction in recursive["transactions"]),
            RECURSIVE + " published an unchecked recursive specialization")

    print(json.dumps({
        "schema": "toka.stage0-generic-body-call-qualification",
        "version": 1,
        "result": "pass",
        "specializations": 2,
        "qualified_routes": ["method", "ordinary"],
        "valid_body_transactions": sum(len(value)
                                       for value in by_callee.values()),
        "nested_body_transactions": len(inner) + len(leaf),
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
