#!/usr/bin/env python3

"""Qualify the audit-only Stage-0 non-call transfer planner."""

import argparse
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SOURCE = "tests/semantics/non_call_transfer_shadow/stage0_routes.tk"
VIEWS = "tests/semantics/non_call_transfer_shadow/stage0_views.tk"
REJECTIONS = "tests/semantics/non_call_transfer_shadow/stage0_rejections.tk"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def invoke(tokac, shadow, source=SOURCE):
    command = [str(tokac)]
    if shadow:
        command.append("--non-call-transfer-shadow=json")
    command.extend(("--check-only", source))
    return subprocess.run(
        command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    normal = invoke(tokac, False)
    shadow = invoke(tokac, True)
    replay = invoke(tokac, True)
    require(normal.returncode == 0, normal.stderr)
    require(shadow.returncode == normal.returncode and
            shadow.stderr == normal.stderr and not normal.stdout,
            "non-call shadow changed normal behavior")
    require(shadow.stdout == replay.stdout and shadow.stderr == replay.stderr,
            "non-call shadow output is not deterministic")

    payload = json.loads(shadow.stdout)
    require(payload.get("schema") ==
            "toka.internal.non-call-transfer-shadow" and
            payload.get("version") == 1 and
            payload.get("status") == "audit-only" and
            isinstance(payload.get("records"), list),
            "non-call shadow schema changed")

    expected = {
        ("standalone", 9): "StatementEndDiscard",
        ("assignment", 13): "Assignment",
        ("initialization", 16): "Initialization",
        ("aggregate", 19): "AggregateMember",
        ("match_binding", 22): "MatchBinding",
        ("closure_capture", 27): "ClosureCapture",
        ("return", 4): "Return",
    }
    source_records = [
        record for record in payload["records"]
        if record["location"]["file"].endswith(SOURCE)]
    record_fields = {
        "boundary", "plan_origin", "syntax_purpose", "source_category",
        "dependency", "type_compatibility", "eligibility_context",
        "prepared_before_legacy_mutation", "snapshot_revision", "plan",
        "location",
    }
    plan_fields = {
        "actual_type", "formal_type", "formal_contract", "outcome",
        "rejection", "exact_path", "referent_path", "dependency_roots",
        "source_view", "surface_spelling", "copy_proof", "eligibility",
        "obligation_before", "reachability", "source_liveness",
        "init_mask", "cleanup_mask", "liability_identity",
        "value_production", "source", "destination", "drop",
        "source_obligation_action", "source_obligation_after",
        "destination_obligation_action", "destination_obligation_after",
    }
    for record in source_records:
        require(set(record) == record_fields and
                set(record["plan"]) == plan_fields,
                "non-call record schema changed")
        require(record["prepared_before_legacy_mutation"] and
                record["snapshot_revision"] > 0,
                "non-call record was not prepared from an entry snapshot")
        if record["plan"]["outcome"] == "Rejected":
            require(record["plan"]["source"] == "NoStateChange",
                    "rejected non-call plan can change source state")
        identity = record["plan"]["liability_identity"]
        require(not identity or "/private/tmp/" not in identity,
                "non-call liability identity contains a physical path")
    qualified = []
    for key, destination in expected.items():
        boundary, line = key
        matches = [
            record for record in source_records
            if record["boundary"] == boundary and
            record["location"]["line"] == line]
        require(len(matches) == 1,
                "%s:%d expected one record, found %d" %
                (boundary, line, len(matches)))
        record = matches[0]
        plan = record["plan"]
        require(record["prepared_before_legacy_mutation"] and
                record["snapshot_revision"] > 0 and
                record["plan_origin"] == "UserSource" and
                record["syntax_purpose"] == "SourceInvalidation" and
                record["source_category"] == "NamedSourcePlace" and
                plan["surface_spelling"] == "ExplicitCede" and
                plan["outcome"] == "Admitted" and
                plan["rejection"] == "None" and
                plan["source"] == "InvalidateSubtree" and
                plan["destination"] == destination and
                plan["formal_contract"] == "None" and
                plan["actual_type"] == "i32",
                "%s:%d emitted an incomplete plan" % (boundary, line))
        if key == ("return", 4):
            require(plan["obligation_before"] == "Outstanding" and
                    plan["source_obligation_action"] ==
                    "DischargeToReturn" and
                    plan["source_obligation_after"] == "Discharged",
                    "return did not discharge its exact cede obligation")
        qualified.append(record)

    views_normal = invoke(tokac, False, VIEWS)
    views_shadow = invoke(tokac, True, VIEWS)
    views_replay = invoke(tokac, True, VIEWS)
    require(views_normal.returncode == views_shadow.returncode and
            views_normal.stderr == views_shadow.stderr and
            views_normal.returncode == 0,
            "unique-view shadow changed normal behavior: " +
            views_shadow.stderr)
    require(views_shadow.stdout == views_replay.stdout and
            views_shadow.stderr == views_replay.stderr,
            "unique-view shadow output is not deterministic")
    views_payload = json.loads(views_shadow.stdout)
    views_records = [
        record for record in views_payload["records"]
        if record["location"]["file"].endswith(VIEWS)]

    def unique_view(boundary, line):
        matches = [record for record in views_records
                   if record["boundary"] == boundary and
                   record["location"]["line"] == line]
        require(len(matches) == 1,
                "%s:%d unique-view record count changed" % (boundary, line))
        return matches[0]

    intrinsic = unique_view("return", 4)
    redundant = unique_view("return", 8)
    payload = unique_view("return", 12)
    discard = unique_view("standalone", 17)
    require(intrinsic["plan"]["surface_spelling"] ==
            "IntrinsicUniqueMove" and
            intrinsic["plan"]["outcome"] == "Admitted" and
            intrinsic["plan"]["source"] == "InvalidateRoot" and
            redundant["plan"]["rejection"] ==
            "RedundantIntrinsicUniqueCede" and
            payload["plan"]["rejection"] ==
            "DereferencedOwningPayload" and
            discard["plan"]["outcome"] == "Admitted" and
            discard["plan"]["source_view"] == "UniqueHandle" and
            discard["plan"]["source"] == "InvalidateRoot" and
            discard["plan"]["destination"] == "StatementEndDiscard",
            "unique handle/payload exact-view rules changed")

    rejected_normal = invoke(tokac, False, REJECTIONS)
    rejected_shadow = invoke(tokac, True, REJECTIONS)
    rejected_replay = invoke(tokac, True, REJECTIONS)
    require(rejected_normal.returncode == rejected_shadow.returncode and
            rejected_normal.stderr == rejected_shadow.stderr and
            rejected_normal.returncode != 0 and
            "E0438" in rejected_normal.stderr,
            "non-call rejection shadow changed normal diagnostics")
    require(rejected_shadow.stdout == rejected_replay.stdout and
            rejected_shadow.stderr == rejected_replay.stderr,
            "non-call rejection output is not deterministic")
    rejected_payload = json.loads(rejected_shadow.stdout)
    rejected_records = [
        record for record in rejected_payload["records"]
        if record["location"]["file"].endswith(REJECTIONS)]
    temporary = [record for record in rejected_records
                 if record["boundary"] == "return" and
                 record["location"]["line"] == 2]
    moved = [record for record in rejected_records
             if record["boundary"] == "standalone" and
             record["location"]["line"] == 8]
    require(len(temporary) == 1 and
            temporary[0]["plan"]["rejection"] ==
            "ExplicitCedeRequiresSource" and
            temporary[0]["plan"]["source"] == "NoStateChange" and
            len(moved) == 1 and
            moved[0]["plan"]["rejection"] == "SourceNotLive" and
            moved[0]["plan"]["source"] == "NoStateChange",
            "non-call rejection plans did not fail closed")

    mixed = subprocess.run(
        [str(tokac), "--non-call-transfer-shadow=json",
         "--call-transfer-shadow=json", "--check-only", SOURCE], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
    require(mixed.returncode != 0 and not mixed.stdout and
            "cannot be combined" in mixed.stderr,
            "non-call/call JSON modes were not isolated")

    print(json.dumps({
        "schema": "toka.stage0-non-call-transfer-shadow-audit",
        "version": 1,
        "result": "pass",
        "qualified_routes": len(qualified),
        "routes": sorted({record["boundary"] for record in qualified}),
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
