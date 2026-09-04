#!/usr/bin/env python3

"""Qualify the audit-only Stage-0 non-call transfer planner."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = "tests/semantics/non_call_transfer_shadow/stage0_routes.tk"
VIEWS = "tests/semantics/non_call_transfer_shadow/stage0_views.tk"
REJECTIONS = "tests/semantics/non_call_transfer_shadow/stage0_rejections.tk"
OVERLAP = "tests/fail/cede_existing_destination_self_transfer.tk"
GROUP_TEMPORARIES = (
    "tests/semantics/non_call_transfer_shadow/stage0_group_temporaries.tk")
GROUP_ALIAS = "tests/semantics/non_call_transfer_shadow/stage0_group_alias.tk"
CAPTURE_GROUP_ALIAS = (
    "tests/semantics/non_call_transfer_shadow/stage0_capture_group_alias.tk")
PREFLIGHT = "tests/semantics/non_call_transfer_shadow/stage0_preflight.tk"
INVALID_GENERIC = (
    "tests/semantics/call_transfer_shadow_m1/"
    "generic_body_semantic_failure.tk")
PERMISSION_ROOT = "tests/semantics/tki_replay/cases/permission_002_shared_flow/"
GENERIC_GROUP = (
    "tests/semantics/non_call_transfer_shadow/stage0_generic_group_identity.tk")
FOR_ALIAS_CAPTURE = (
    "tests/semantics/non_call_transfer_shadow/stage0_for_alias_capture.tk")

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
        "boundary", "group_identity", "edge", "edge_index",
        "group_outcome", "group_rejection", "group_plan_admitted",
        "plan_origin", "syntax_purpose", "source_category",
        "dependency", "type_compatibility", "eligibility_context",
        "destination_exact_path", "destination_view",
        "destination_reachability", "destination_morphology",
        "destination_capabilities", "destination_flow_ceiling",
        "source_flow_ceiling",
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
        capability_fields = {"complete", "handle_rebind", "payload_write"}
        require(set(record["destination_capabilities"]) ==
                capability_fields and
                set(record["destination_flow_ceiling"]) ==
                capability_fields and
                set(record["source_flow_ceiling"]) == capability_fields,
                "non-call capability tuple changed")
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
                bool(record["group_identity"]) and
                record["group_outcome"] == "Admitted" and
                record["group_rejection"] == "None" and
                record["group_plan_admitted"] and
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
        if key == ("assignment", 13):
            require(bool(record["destination_exact_path"]) and
                    record["destination_view"] == "DirectValue" and
                    record["destination_reachability"] == "ExactSubtree",
                    "assignment omitted destination identity")
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

    overlap_normal = invoke(tokac, False, OVERLAP)
    overlap_shadow = invoke(tokac, True, OVERLAP)
    require(overlap_normal.returncode == overlap_shadow.returncode and
            overlap_normal.stderr == overlap_shadow.stderr and
            overlap_normal.returncode != 0 and
            "E04615" in overlap_normal.stderr,
            "assignment-overlap shadow changed normal diagnostics")
    overlap_payload = json.loads(overlap_shadow.stdout)
    overlap_records = [
        record for record in overlap_payload["records"]
        if record["location"]["file"].endswith(OVERLAP) and
        record["boundary"] == "assignment" and
        record["location"]["line"] == 5]
    require(len(overlap_records) == 1 and
            overlap_records[0]["plan"]["outcome"] == "Rejected" and
            overlap_records[0]["plan"]["rejection"] ==
            "DestinationOverlap" and
            overlap_records[0]["plan"]["source"] == "NoStateChange" and
            overlap_records[0]["plan"]["exact_path"] ==
            overlap_records[0]["destination_exact_path"],
            "assignment self-overlap received transfer authority")

    temporary_normal = invoke(tokac, False, GROUP_TEMPORARIES)
    temporary_shadow = invoke(tokac, True, GROUP_TEMPORARIES)
    require(temporary_normal.returncode == temporary_shadow.returncode and
            temporary_normal.stderr == temporary_shadow.stderr and
            temporary_normal.returncode == 0,
            "aggregate temporary shadow changed normal behavior")
    temporary_payload = json.loads(temporary_shadow.stdout)
    temporary_records = [
        record for record in temporary_payload["records"]
        if record["location"]["file"].endswith(GROUP_TEMPORARIES) and
        record["boundary"] == "aggregate" and
        record["edge"] in ("left", "right")]
    require(len(temporary_records) == 2 and
            len({record["group_identity"]
                 for record in temporary_records}) == 1 and
            len({record["snapshot_revision"]
                 for record in temporary_records}) == 1 and
            all(record["group_plan_admitted"] and
                record["group_outcome"] == "Admitted" and
                record["plan"]["value_production"] == "ConsumeTemporary" and
                record["plan"]["drop"] == "DestinationAssumesLiability"
                for record in temporary_records) and
            len({record["plan"]["liability_identity"]
                 for record in temporary_records}) == 2 and
            all(record["plan"]["liability_identity"] and
                "/private/tmp/" not in
                record["plan"]["liability_identity"]
                for record in temporary_records),
            "aggregate temporary liability edges collided")

    alias_normal = invoke(tokac, False, GROUP_ALIAS)
    alias_shadow = invoke(tokac, True, GROUP_ALIAS)
    require(alias_normal.returncode == alias_shadow.returncode and
            alias_normal.stderr == alias_shadow.stderr and
            alias_normal.returncode != 0 and "E0438" in alias_normal.stderr,
            "aggregate alias shadow changed normal diagnostics")
    alias_payload = json.loads(alias_shadow.stdout)
    alias_records = [
        record for record in alias_payload["records"]
        if record["location"]["file"].endswith(GROUP_ALIAS) and
        record["boundary"] == "aggregate" and
        record["edge"] in ("left", "right")]
    require(len(alias_records) == 2 and
            len({record["snapshot_revision"]
                 for record in alias_records}) == 1 and
            all(record["plan"]["outcome"] == "Admitted" and
                record["plan"]["source_liveness"] == "Live" and
                not record["group_plan_admitted"] and
                record["group_rejection"] ==
                "NonCallGroupAliasConflict"
                for record in alias_records),
            "aggregate group did not reject duplicate source atomically")

    capture_normal = invoke(tokac, False, CAPTURE_GROUP_ALIAS)
    capture_shadow = invoke(tokac, True, CAPTURE_GROUP_ALIAS)
    require(capture_normal.returncode == capture_shadow.returncode and
            capture_normal.stderr == capture_shadow.stderr and
            capture_normal.returncode == 0,
            "closure group shadow changed normal behavior")
    capture_payload = json.loads(capture_shadow.stdout)
    capture_records = [
        record for record in capture_payload["records"]
        if record["location"]["file"].endswith(CAPTURE_GROUP_ALIAS) and
        record["boundary"] == "closure_capture"]
    require(len(capture_records) == 2 and
            len({record["snapshot_revision"]
                 for record in capture_records}) == 1 and
            all(record["plan"]["source_liveness"] == "Live" and
                record["group_rejection"] ==
                "NonCallGroupAliasConflict" and
                not record["group_plan_admitted"]
                for record in capture_records),
            "closure capture group did not reject duplicate source")

    preflight_normal = invoke(tokac, False, PREFLIGHT)
    preflight_shadow = invoke(tokac, True, PREFLIGHT)
    require(preflight_normal.returncode == preflight_shadow.returncode and
            preflight_normal.stderr == preflight_shadow.stderr and
            preflight_normal.returncode == 0,
            "read-only preflight changed normal behavior")
    preflight_payload = json.loads(preflight_shadow.stdout)
    preflight_records = [
        record for record in preflight_payload["records"]
        if record["location"]["file"].endswith(PREFLIGHT)]

    def preflight_record(boundary, line):
        matches = [record for record in preflight_records
                   if record["boundary"] == boundary and
                   record["location"]["line"] == line]
        require(len(matches) == 1,
                "%s:%d preflight record count changed" % (boundary, line))
        return matches[0]

    binary_init = preflight_record("initialization", 14)
    new_assignment = preflight_record("assignment", 16)
    value_ascription = preflight_record("return", 4)
    unique_ascription = preflight_record("return", 8)
    binary_return = preflight_record("return", 19)
    require(binary_init["plan"]["actual_type"] == "i32" and
            binary_init["plan"]["outcome"] == "Admitted" and
            new_assignment["plan"]["actual_type"] == "^Cell" and
            new_assignment["plan"]["outcome"] == "Admitted" and
            value_ascription["plan"]["actual_type"] == "i32" and
            value_ascription["plan"]["source_view"] == "DirectValue" and
            bool(value_ascription["plan"]["exact_path"]) and
            value_ascription["plan"]["outcome"] == "Admitted" and
            unique_ascription["plan"]["actual_type"] == "^Cell" and
            unique_ascription["plan"]["source_view"] == "UniqueHandle" and
            unique_ascription["plan"]["surface_spelling"] ==
            "IntrinsicUniqueMove" and
            unique_ascription["plan"]["outcome"] == "Admitted" and
            binary_return["plan"]["actual_type"] == "i32" and
            binary_return["plan"]["outcome"] == "Admitted",
            "read-only type/view preflight lost a common expression")

    invalid_normal = invoke(tokac, False, INVALID_GENERIC)
    invalid_shadow = invoke(tokac, True, INVALID_GENERIC)
    require(invalid_normal.returncode == invalid_shadow.returncode and
            invalid_normal.stderr == invalid_shadow.stderr and
            invalid_normal.returncode != 0 and
            "E0408" in invalid_normal.stderr,
            "invalid generic non-call shadow changed normal diagnostics")
    invalid_payload = json.loads(invalid_shadow.stdout)
    require(not any(record["location"]["file"].endswith(INVALID_GENERIC) and
                    record["location"]["line"] == 6
                    for record in invalid_payload["records"]),
            "invalid generic specialization leaked a non-call body record")

    permission_cases = (
        ("fail_cede_shared_existing_lhs_payload_write.tk", "assignment", 7,
         "AccessCapabilityMismatch"),
        ("fail_readonly_field_fresh_binding.tk", "initialization", 7,
         "AccessCapabilityMismatch"),
        ("fail_readonly_field_payload_return.tk", "return", 5,
         "AccessCapabilityMismatch"),
        ("fail_shared_field_from_readonly.tk", "aggregate", 6,
         "AccessCapabilityMismatch"),
        ("fail_closure_capture_readonly_field.tk", "closure_capture", 8,
         None),
    )
    for name, boundary, line, expected_rejection in permission_cases:
        source = PERMISSION_ROOT + name
        normal = invoke(tokac, False, source)
        shadow = invoke(tokac, True, source)
        require(normal.returncode == shadow.returncode and
                normal.stderr == shadow.stderr and normal.returncode != 0 and
                "E04573" in normal.stderr,
                source + " permission shadow changed normal diagnostics")
        permission_payload = json.loads(shadow.stdout)
        matches = [
            record for record in permission_payload["records"]
            if record["location"]["file"].endswith(name) and
            record["boundary"] == boundary and
            record["location"]["line"] == line]
        require(len(matches) == 1 and
                matches[0]["plan"]["outcome"] == "Rejected" and
                matches[0]["plan"]["source"] == "NoStateChange" and
                not matches[0]["group_plan_admitted"] and
                matches[0]["destination_capabilities"]["complete"] and
                matches[0]["source_flow_ceiling"]["complete"] and
                (expected_rejection is None or
                 matches[0]["plan"]["rejection"] == expected_rejection),
                source + " received permission-amplifying authority")

    generic_normal = invoke(tokac, False, GENERIC_GROUP)
    generic_shadow = invoke(tokac, True, GENERIC_GROUP)
    require(generic_normal.returncode == generic_shadow.returncode and
            generic_normal.stderr == generic_shadow.stderr and
            generic_normal.returncode == 0,
            "generic group identity shadow changed normal behavior")
    generic_payload = json.loads(generic_shadow.stdout)
    generic_records = [
        record for record in generic_payload["records"]
        if record["location"]["file"].endswith(GENERIC_GROUP) and
        record["boundary"] == "aggregate" and
        record["edge"] in ("left", "right")]
    require(len(generic_records) == 4 and
            len({record["group_identity"]
                 for record in generic_records}) == 2 and
            len({record["plan"]["liability_identity"]
                 for record in generic_records}) == 4 and
            all(record["group_identity"] and
                "/private/tmp/" not in record["group_identity"] and
                record["group_plan_admitted"]
                for record in generic_records),
            "generic monomorphizations reused group/liability identity")

    with tempfile.TemporaryDirectory(
            prefix="toka-non-call-external-source-") as temp:
        external_source = Path(temp) / "plain.tk"
        external_source.write_text(
            "fn main() -> i32 {\n"
            "    auto value = 1:i32\n"
            "    cede value\n"
            "    return 0\n"
            "}\n")
        external = invoke(tokac, True, str(external_source))
        require(external.returncode == 0, external.stderr)
        external_payload = json.loads(external.stdout)
        standalone = [record for record in external_payload["records"]
                      if record["boundary"] == "standalone" and
                      record["location"]["file"].endswith("plain.tk")]
        require(len(standalone) == 1 and standalone[0]["group_identity"] and
                "/private/" not in standalone[0]["group_identity"] and
                standalone[0]["group_plan_admitted"],
                "plain external source produced an empty/physical group id: " +
                repr(standalone))

    alias_normal = invoke(tokac, False, FOR_ALIAS_CAPTURE)
    alias_shadow = invoke(tokac, True, FOR_ALIAS_CAPTURE)
    require(alias_normal.returncode == alias_shadow.returncode and
            alias_normal.stderr == alias_shadow.stderr and
            alias_normal.returncode != 0 and "E04647" in alias_normal.stderr,
            "for-alias capture shadow changed normal diagnostics")
    alias_payload = json.loads(alias_shadow.stdout)
    alias_records = [
        record for record in alias_payload["records"]
        if record["location"]["file"].endswith(FOR_ALIAS_CAPTURE) and
        record["boundary"] == "closure_capture" and record["edge"] == "^x"]
    require(len(alias_records) == 1 and
            alias_records[0]["plan"]["exact_path"] and
            alias_records[0]["plan"]["outcome"] == "Rejected" and
            alias_records[0]["plan"]["source"] == "NoStateChange" and
            not alias_records[0]["group_plan_admitted"],
            "for-alias capture lacks a stable fail-closed source identity")

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
