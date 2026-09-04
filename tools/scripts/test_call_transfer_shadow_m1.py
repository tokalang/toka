#!/usr/bin/env python3

"""Qualify the audit-only RC9 M1 call-transfer shadow planner."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
STAGE1_LEGACY = "--stage1-legacy-ordinary-cede"
TRANSACTIONS = {}
PARITY_CASES = set()
MISSING_PRE_MUTATION = []
TRANSACTION_RECORD_GAPS = []
if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(tokac, source, expected_error=None, check_only=True, extra=()):
    command = [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json"]
    if check_only:
        command.append("--check-only")
    command.extend(extra)
    command.append(source)
    process = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    if expected_error:
        require(process.returncode != 0, source + " unexpectedly compiled")
    else:
        require(process.returncode == 0, source + ":\n" + process.stderr)
    if expected_error:
        require(expected_error in process.stderr,
                source + " missed " + expected_error + ":\n" + process.stderr)
    try:
        payload = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(source + " emitted invalid shadow JSON") from error
    require(payload.get("schema") == "toka.internal.call-transfer-shadow",
            source + " emitted the wrong schema")
    require(payload.get("version") == 5, source + " emitted the wrong version")
    require(payload.get("status") == "audit-only",
            source + " did not identify audit-only output")
    records = payload.get("records")
    require(isinstance(records, list), source + " omitted records")
    transactions = payload.get("transactions")
    require(isinstance(transactions, list), source + " omitted transactions")
    TRANSACTIONS[source] = transactions
    required = {
        "callee", "route", "parameter", "argument_index", "formal_index",
        "value_category", "spelling", "transfer", "source", "dependency",
        "place_eligibility", "drop", "execution_boundary",
        "source_root_id", "source_path", "source_identity",
        "referent_path", "referent_identity", "dependency_paths",
        "cleanup_mask", "formal_ceded", "formal_init", "actual_init",
        "legacy_caller_rule_applied", "legacy_cede_exempt",
        "legacy_missing_cede", "async", "location", "contract_location",
        "stage0",
    }
    stage0_required = {
        "plan_origin", "syntax_purpose", "surface_spelling",
        "source_category", "exact_path", "referent_root", "referent_path",
        "dependency_roots", "dependency", "dependency_complete",
        "actual_type", "formal_type",
        "formal_contract", "declared_formal_morphology",
        "formal_morphology", "formal_ownership",
        "formal_transfer_class", "formal_contract_origin",
        "formal_declaration_complete",
        "formal_capabilities",
        "actual_capabilities", "ownership", "copy_proof", "eligibility",
        "temporary_eligibility", "type_compatibility",
        "eligibility_context", "obligation_before",
        "outcome", "rejection", "value_production", "source",
        "destination", "drop", "source_obligation_action",
        "source_obligation_after",
        "destination_obligation_action", "destination_obligation_after",
        "source_view", "reachability", "semantic_root",
    }
    seen = set()
    for record in records:
        require(set(record) == required,
                source + " shadow record fields changed")
        require(record["argument_index"] > 0 and record["formal_index"] > 0,
                source + " emitted a plan without a selected formal")
        require(isinstance(record["dependency_paths"], list),
                source + " dependency paths are not an array")
        require(record["cleanup_mask"] is None or
                isinstance(record["cleanup_mask"], int),
                source + " cleanup mask has an invalid representation")
        require(isinstance(record["actual_init"], bool),
                source + " actual init spelling is not boolean")
        stage0 = record["stage0"]
        require(isinstance(stage0, dict) and set(stage0) == stage0_required,
                source + " emitted an incomplete Stage-0 plan")
        capability_fields = {"complete", "handle_rebind", "payload_write"}
        require(set(stage0["formal_capabilities"]) == capability_fields and
                set(stage0["actual_capabilities"]) == capability_fields,
                source + " emitted incomplete Stage-0 capability facts")
        require(isinstance(stage0["dependency_roots"], list) and
                isinstance(stage0["dependency_complete"], bool),
                source + " emitted invalid Stage-0 dependency facts")
        require(bool(stage0["actual_type"]) or
                (stage0["outcome"] == "Rejected" and
                 stage0["rejection"] in
                 ("IncompleteFacts", "MissingPreMutationTransaction")),
                source + " omitted Stage-0 actual resolved type without "
                "rejecting incomplete facts")
        if stage0["rejection"] == "MissingPreMutationTransaction":
            require(stage0["outcome"] == "Rejected" and
                    stage0["source"] == "NoStateChange",
                    source + " missing pre-mutation plan did not fail closed")
            MISSING_PRE_MUTATION.append((source, record["callee"],
                                         record["location"]["line"]))
        declaration_hidden = record["route"] in ("indirect-fn", "indirect-dyn-fn")
        if not record["formal_init"] and not declaration_hidden:
            require(bool(stage0["formal_type"]) and
                    stage0["declared_formal_morphology"] not in
                    ("None", "Indeterminate") and
                    stage0["formal_morphology"] not in
                    ("None", "Indeterminate") and
                    stage0["formal_contract_origin"] not in
                    ("None", "Indeterminate") and
                    stage0["formal_declaration_complete"] and
                    stage0["formal_capabilities"]["complete"],
                    source + " omitted Stage-0 formal morphology facts")
        if declaration_hidden:
            require(stage0["declared_formal_morphology"] == "Indeterminate" and
                    stage0["formal_contract_origin"] == "Indeterminate" and
                    not stage0["formal_declaration_complete"] and
                    stage0["outcome"] == "Rejected" and
                    stage0["rejection"] == "IncompleteFacts",
                    source + " inferred a declaration contract for an "
                    "indirect/source-hidden call")
        require(stage0["outcome"] in ("Admitted", "Rejected"),
                source + " emitted an invalid Stage-0 outcome")
        if stage0["outcome"] == "Rejected":
            require(stage0["rejection"] != "None" and
                    stage0["source"] == "NoStateChange",
                    source + " rejected Stage-0 plan could change state")
        else:
            require(stage0["rejection"] == "None",
                    source + " admitted Stage-0 plan retained rejection")
        if record["value_category"] in ("Place", "InitStorage"):
            require(record["source_root_id"] > 0 and
                    bool(record["source_identity"]),
                    source + " place plan omitted structured source identity")
            require(bool(stage0["semantic_root"]) or
                    (stage0["outcome"] == "Rejected" and
                     stage0["rejection"] == "IncompleteFacts"),
                    source + " Stage-0 place invented authority without a "
                    "stable root or failed to reject incomplete facts")
            if stage0["semantic_root"]:
                require("crate:" in stage0["semantic_root"] and
                        "/private/tmp/" not in stage0["semantic_root"] and
                        str(ROOT) not in stage0["semantic_root"],
                        source + " Stage-0 root contains a physical/worktree "
                        "path instead of a logical module coordinate")
        if record["value_category"] in ("Temporary", "Indeterminate"):
            require(record["source_root_id"] == 0,
                    source + " non-place plan retained a source root")
            require(not stage0["semantic_root"],
                    source + " Stage-0 non-place invented a semantic root")
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":"))
        require(encoded not in seen, source + " emitted duplicate records")
        seen.add(encoded)
    transaction_fields = {
        "callee", "route", "outcome", "rejection", "local_plan_admitted",
        "commit_allowed", "arity_complete", "validation_complete",
        "prepared_before_legacy_mutation", "has_receiver",
        "snapshot_revision", "pal_revision", "expected_argument_count",
        "actual_argument_count", "argument_count", "items", "location",
    }
    transaction_item_fields = {
        "role", "index", "formal_index", "actual_type", "formal_type",
        "formal_contract", "outcome", "rejection", "exact_path",
        "referent_path", "dependency_roots", "source_view",
        "surface_spelling", "copy_proof",
        "eligibility", "obligation_before", "reachability",
        "source_liveness", "init_mask", "cleanup_mask",
        "liability_identity", "value_production", "source", "destination",
        "drop", "source_obligation_action", "source_obligation_after",
        "destination_obligation_action", "destination_obligation_after",
    }
    for transaction in transactions:
        require(set(transaction) == transaction_fields,
                source + " emitted an incomplete Stage-0 transaction")
        require(transaction["prepared_before_legacy_mutation"],
                source + " emitted a post-mutation Stage-0 transaction")
        require(transaction["snapshot_revision"] > 0,
                source + " transaction omitted its shared snapshot revision")
        require(transaction["pal_revision"] == transaction["snapshot_revision"],
                source + " transaction mixed PAL and source revisions")
        generic_resolution_rejection = (
            transaction["outcome"] == "Rejected" and
            transaction["rejection"].startswith("Generic") and
            not transaction["local_plan_admitted"] and
            not transaction["commit_allowed"] and
            transaction["argument_count"] == 0 and
            not transaction["items"])
        if transaction["actual_argument_count"] != transaction["argument_count"]:
            require(generic_resolution_rejection or
                    (not transaction["arity_complete"] and
                     not transaction["commit_allowed"]),
                    source + " transaction silently omitted an actual slot")
        require(len(transaction["items"]) ==
                transaction["argument_count"] +
                (1 if transaction["has_receiver"] else 0),
                source + " transaction item count does not match its slots")
        require(transaction["validation_complete"],
                source + " published an unfinalized transaction")
        require(transaction["commit_allowed"] ==
                (transaction["local_plan_admitted"] and
                 transaction["arity_complete"] and
                 transaction["validation_complete"] and
                 transaction["prepared_before_legacy_mutation"] and
                 transaction["pal_revision"] ==
                 transaction["snapshot_revision"] and
                 transaction["outcome"] == "Admitted"),
                source + " transaction commit bit disagrees with outcome")
        for item in transaction["items"]:
            require(set(item) == transaction_item_fields,
                    source + " emitted an incomplete transaction item")
            require(item["role"] in ("receiver", "argument"),
                    source + " emitted an unknown transaction role")
            require(item["formal_index"] > 0 or item["role"] == "receiver",
                    source + " transaction item omitted its formal index")
            require(isinstance(item["dependency_roots"], list),
                    source + " transaction item dependencies are not typed")
            if item["exact_path"] and item["outcome"] == "Admitted":
                require(item["source_liveness"] != "Indeterminate" and
                        item["init_mask"] is not None and
                        item["cleanup_mask"] is not None,
                        source + " named transaction item omitted dynamic "
                        "liveness/cleanup state")
            if item["drop"] in (
                    "SourceRetainsLiability", "CalleeAssumesLiability",
                    "DestinationAssumesLiability",
                    "StatementEndAssumesLiability"):
                require(bool(item["liability_identity"]),
                        source + " liability transfer omitted its identity")
            if item["outcome"] == "Rejected":
                require(item["source"] == "NoStateChange",
                        source + " rejected transaction item changes state")

    def evidence_key(callee, route, location, index, formal_index):
        return (callee, route, location.get("file", ""),
                location.get("line", 0), index, formal_index)

    records_by_item = {}
    for record in records:
        if not record["location"]["file"].endswith(source):
            continue
        key = evidence_key(
            record["callee"], record["route"], record["location"],
            record["argument_index"], record["formal_index"])
        records_by_item.setdefault(key, []).append(record)

    transaction_items_by_record = {}
    shared_stage0_fields = (
        "actual_type", "formal_type", "formal_contract", "outcome",
        "rejection", "exact_path", "referent_path", "dependency_roots",
        "source_view", "surface_spelling", "copy_proof", "eligibility",
        "obligation_before", "reachability", "value_production", "source",
        "destination", "drop", "source_obligation_action",
        "source_obligation_after", "destination_obligation_action",
        "destination_obligation_after",
    )
    for transaction in transactions:
        if not transaction["location"]["file"].endswith(source):
            continue
        for item in transaction["items"]:
            if item["role"] != "argument":
                continue
            key = evidence_key(
                transaction["callee"], transaction["route"],
                transaction["location"], item["index"],
                item["formal_index"])
            matches = records_by_item.get(key, [])
            if len(matches) != 1:
                TRANSACTION_RECORD_GAPS.append((source, key, len(matches)))
                continue
            stage0 = matches[0]["stage0"]
            for field in shared_stage0_fields:
                require(item[field] == stage0[field],
                        source + " transaction/record Stage-0 %s differs: %r"
                        % (field, key))
            transaction_items_by_record.setdefault(key, []).append(item)

    for key, matching_records in records_by_item.items():
        require(len(matching_records) == 1,
                source + " emitted duplicate argument records: %r" % (key,))
        matches = transaction_items_by_record.get(key, [])
        require(len(matches) == 1,
                source + " argument record has %d matching transaction items: %r"
                % (len(matches), key))
    return records


def require_shadow_parity(tokac, source, expected_error=None, extra=()):
    PARITY_CASES.add(source)
    normal = subprocess.run(
        [str(tokac), STAGE1_LEGACY, "--check-only", *extra, source], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30,
    )
    shadow = subprocess.run(
        [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json", "--check-only", *extra,
         source],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30,
    )
    replay = subprocess.run(
        [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json", "--check-only", *extra,
         source],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30,
    )
    require(shadow.returncode == replay.returncode and
            shadow.stderr == replay.stderr and shadow.stdout == replay.stdout,
            source + " legacy replay was not deterministic")
    require(normal.returncode == shadow.returncode,
            source + " shadow changed the normal return code: normal=%d, "
            "shadow=%d" % (normal.returncode, shadow.returncode))
    require(normal.stderr == shadow.stderr,
            source + " shadow changed normal diagnostics:\nnormal:\n" +
            normal.stderr + "\nshadow:\n" + shadow.stderr)
    require(not normal.stdout, source + " changed normal stdout")
    json.loads(shadow.stdout)
    if expected_error:
        require(expected_error in shadow.stderr,
                source + " legacy replay missed " + expected_error)


def find(records, source_file, **expected):
    matches = []
    for record in records:
        location = record.get("location", {}).get("file", "")
        if not location.endswith(source_file):
            continue
        if all((record.get("location", {}).get("line") if key == "location_line"
                else record.get(key)) == value
               for key, value in expected.items()):
            matches.append(record)
    require(len(matches) == 1,
            "%s expected one %s record, found %d"
            % (source_file, json.dumps(expected, sort_keys=True), len(matches)))
    return matches[0]


def require_stage0(record, source_file, **expected):
    stage0 = record["stage0"]
    for key, value in expected.items():
        require(stage0.get(key) == value,
                "%s expected Stage-0 %s=%r, got %r"
                % (source_file, key, value, stage0.get(key)))


def find_transaction(source_file, **expected):
    matches = []
    for transaction in TRANSACTIONS.get(source_file, []):
        location = transaction.get("location", {}).get("file", "")
        if not location.endswith(source_file):
            continue
        if all((transaction.get("location", {}).get("line")
                if key == "location_line" else transaction.get(key)) == value
               for key, value in expected.items()):
            matches.append(transaction)
    require(len(matches) == 1,
            "%s expected one transaction %s, found %d"
            % (source_file, json.dumps(expected, sort_keys=True),
               len(matches)))
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir
    tokac = build_dir / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    receipts = []

    source = "tests/semantics/call_transfer_shadow_m1/copy_places.tk"
    records = run(tokac, source)
    require(records == run(tokac, source),
            "call transfer shadow output is not deterministic")
    require(records == run(tokac, source, check_only=False),
            "shadow mode without --check-only changed output")
    record = find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="CopyValue",
        source="KeepLive", source_path="plain", value_category="Place",
        drop="NoLiability", formal_ceded=True, formal_index=1,
        legacy_caller_rule_applied=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="MissingCedeForNamedSource",
                   source="NoStateChange")
    record = find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="ConsumeTemporary",
        source="NoSourcePlace", source_path="", value_category="Temporary",
        drop="NoLiability", formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   value_production="CopyValue", source="NoSourcePlace")
    receipts.append(find(
        records, source, callee="consume_pair", route="ordinary",
        parameter="value", spelling="implicit", transfer="CopyValue",
        source="KeepLive", source_path="pair", value_category="Place",
        formal_ceded=True, legacy_cede_exempt=True,
    ))
    record = find(
        records, source, callee="consume_resource", route="ordinary",
        parameter="value", spelling="explicit",
        transfer="ConsumeTemporary", source="NoSourcePlace",
        value_category="Temporary", drop="DestinationAssumesLiability",
        formal_ceded=True,
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="ExplicitCedeRequiresSource",
                   source="NoStateChange")

    source = "tests/semantics/call_transfer_shadow_m1/copy_explicit_invalidates.tk"
    records = run(tokac, source, expected_error="E0438")
    record = find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="explicit", transfer="CopyValue",
        source="InvalidatePlace", source_path="plain", value_category="Place",
        formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
        place_eligibility="PendingValidation",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   value_production="CopyValue",
                   source="InvalidateSubtree")
    transaction = find_transaction(
        source, callee="consume", route="ordinary", location_line=9,
    )
    require(transaction["outcome"] == "Admitted" and
            transaction["rejection"] == "None" and
            transaction["commit_allowed"] and
            transaction["argument_count"] == 1 and
            transaction["items"][0]["value_production"] == "CopyValue" and
            transaction["items"][0]["source"] == "InvalidateSubtree" and
            transaction["items"][0]["drop"] == "NoLiability" and
            transaction["items"][0]["destination_obligation_action"] ==
            "CreateOutstanding",
            source + " admitted transaction lost transfer dimensions")

    source = "tests/semantics/call_transfer_shadow_m1/addr_copy_policy.tk"
    records = run(tokac, source)
    addr_record = find(
        records, source, callee="consume_addr", route="ordinary",
        parameter="value", location_line=16,
    )
    oaddr_record = find(
        records, source, callee="consume_oaddr", route="ordinary",
        parameter="value", location_line=17,
    )
    receipts.extend((addr_record, oaddr_record))
    for record in (addr_record, oaddr_record):
        require_stage0(record, source, outcome="Admitted",
                       rejection="None", value_production="CopyValue",
                       ownership="PlainValue", copy_proof="ProvenCopy",
                       source="InvalidateSubtree")

    source = "tests/conformance/diagnostics/static_cede_parameter_requires_explicit_transfer.tk"
    require_shadow_parity(tokac, source)
    records = run(tokac, source)
    record = find(
        records, source, callee="Token::consume", route="static",
        parameter="token", spelling="implicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="source", formal_ceded=True,
        legacy_cede_exempt=False, legacy_missing_cede=True,
        legacy_caller_rule_applied=True,
        place_eligibility="PendingValidation",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="MissingCedeForNamedSource",
                   source="NoStateChange")
    transaction = find_transaction(
        source, callee="Token::consume", route="static",
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallItemRejected" and
            not transaction["has_receiver"] and
            transaction["argument_count"] == 1 and
            transaction["items"][0]["rejection"] ==
            "MissingCedeForNamedSource" and
            transaction["items"][0]["source"] == "NoStateChange",
            source + " static transaction did not preserve item rejection")

    source = "tests/conformance/diagnostics/callable_cede_parameter_requires_explicit_transfer.tk"
    require_shadow_parity(tokac, source)
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consumer", route="callable",
        parameter="token", spelling="explicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="source", formal_ceded=True,
        formal_index=2, legacy_cede_exempt=False, legacy_missing_cede=False,
    ))
    transaction = find_transaction(
        source, callee="consumer", route="callable",
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallItemRejected" and
            transaction["has_receiver"] and
            transaction["argument_count"] == 1 and
            [item["role"] for item in transaction["items"]] ==
            ["receiver", "argument"] and
            transaction["items"][1]["rejection"] ==
            "MissingCedeForNamedSource" and
            transaction["items"][1]["source"] == "NoStateChange",
            source + " callable transaction lost its formal rejection")

    source = "tests/conformance/diagnostics/cede_shared_projection_cannot_supply_method_payload_parameter.tk"
    records = run(tokac, source, expected_error="E04510")
    receipts.append(find(
        records, source, callee="overwrite", route="method", parameter="p",
        spelling="explicit", transfer="TransferShared",
        source="InvalidatePlace", source_path="holder.cell",
        formal_ceded=True, legacy_cede_exempt=False,
        legacy_missing_cede=False, place_eligibility="PendingValidation",
    ))

    source = "tests/semantics/tki_replay/cases/own_cede_003_generic_methods/fail_generic_missing_cede.tk"
    require_shadow_parity(tokac, source, expected_error="E0438")
    records = run(tokac, source, expected_error="E0438")
    receipts.append(find(
        records, source, callee="forward_parcel", route="ordinary",
        parameter="parcel", spelling="explicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="parcel", formal_ceded=True,
        legacy_cede_exempt=False, legacy_missing_cede=False,
    ))
    transaction = find_transaction(
        source, callee="forward_parcel", route="ordinary",
        location_line=7,
    )
    require(not transaction["local_plan_admitted"] and
            not transaction["commit_allowed"] and
            transaction["items"][0]["outcome"] == "Rejected" and
            transaction["items"][0]["rejection"] ==
            "MissingCedeForNamedSource" and
            transaction["items"][0]["source"] == "NoStateChange",
            source + " generic shadow lost the future explicit-cede policy")

    source = "tests/semantics/tki_replay/cases/async_start_001_cede_handoff/pass_start_cede.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume_async", route="ordinary",
        parameter="payload", spelling="explicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="payload", formal_ceded=True,
        execution_boundary="StartHandoff", **{"async": True},
    ))

    source = "tests/fail/start_borrowed_str.tk"
    require_shadow_parity(tokac, source, expected_error="E04583")
    run(tokac, source, expected_error="E04583")
    transaction = find_transaction(
        source, callee="worker", route="ordinary", location_line=7,
    )
    require(transaction["local_plan_admitted"] and
            transaction["validation_complete"] and
            transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallValidationFailed" and
            not transaction["commit_allowed"] and
            transaction["items"][0]["outcome"] == "Admitted",
            source + " published commit before execution-boundary validation")

    source = "tests/conformance/diagnostics/cede_argument_to_borrowed_parameter_rejected.tk"
    records = run(tokac, source, expected_error="E04640")
    record = find(
        records, source, callee="borrow", route="ordinary",
        parameter="value", spelling="explicit", transfer="Reject",
        source="NoStateChange", source_path="value", formal_ceded=False,
        place_eligibility="NotApplicable", drop="NoStateChange",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="ExplicitCedeToOrdinaryFormal",
                   source="NoStateChange")

    source = "tests/pass/g03_unsafe_null_privilege.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="libc_free", route="extern",
        parameter="ptr", spelling="implicit", transfer="CopyIdentity",
        source="KeepLive", dependency="RawUnsafe", formal_ceded=False,
    ))
    transaction = find_transaction(
        source, callee="libc_free", route="extern", location_line=12,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallItemRejected" and
            not transaction["has_receiver"] and
            transaction["items"][0]["outcome"] == "Rejected" and
            transaction["items"][0]["source"] == "NoStateChange",
            source + " extern transaction did not fail closed")

    source = "tests/pass/g08_dyn_closure.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="cb", route="indirect-dyn-fn",
        parameter="arg1", argument_index=1, formal_index=1,
        value_category="Place", transfer="CopyValue", source="KeepLive",
        legacy_caller_rule_applied=False,
    ))
    transaction = find_transaction(
        source, callee="cb", route="indirect-dyn-fn", location_line=28,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallItemRejected" and
            transaction["items"][0]["rejection"] == "IncompleteFacts" and
            transaction["items"][0]["source"] == "NoStateChange",
            source + " indirect dyn transaction inferred declaration facts")
    receipts.append(find(
        records, source, callee="libc_printf", route="extern",
        location_line=38, argument_index=1, formal_index=1,
    ))
    require(sum(1 for record in records
                if record["callee"] == "libc_printf" and
                record["location"]["file"].endswith(source) and
                record["location"]["line"] == 38) == 1,
            "variadic tail produced a plan without a selected formal")

    source = "tests/pass/g08_callable_protocol.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="nested_callback", route="indirect-fn",
        parameter="arg1", value_category="Temporary", transfer="CopyValue",
        source="KeepLive", legacy_caller_rule_applied=False,
    ))
    transaction = find_transaction(
        source, callee="nested_callback", route="indirect-fn",
        location_line=54,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallItemRejected" and
            transaction["items"][0]["rejection"] == "IncompleteFacts" and
            transaction["items"][0]["source"] == "NoStateChange",
            source + " indirect fn transaction inferred declaration facts")
    consuming_callable = find_transaction(
        source, callee="take", route="indirect-fn", location_line=80,
    )
    require(consuming_callable["has_receiver"] and
            consuming_callable["items"][0]["role"] == "receiver" and
            consuming_callable["items"][0]["surface_spelling"] ==
            "ExplicitCede" and
            consuming_callable["items"][0]["formal_contract"] == "Cede",
            source + " consuming callable receiver lost its cede spelling")

    source = "tests/semantics/call_transfer_shadow_m1/dynamic_trait_method.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="Transform::apply",
        route="dynamic-trait-method", parameter="value", argument_index=1,
        formal_index=2, value_category="Place", transfer="CopyValue",
        source="KeepLive", legacy_caller_rule_applied=False,
    ))
    transaction = find_transaction(
        source, callee="Transform::apply", route="dynamic-trait-method",
        location_line=14,
    )
    require(transaction["has_receiver"] and
            transaction["argument_count"] == 1 and
            [item["role"] for item in transaction["items"]] ==
            ["receiver", "argument"] and
            transaction["items"][1]["destination"] == "CalleeParameter" and
            transaction["items"][1]["source"] == "KeepLive" and
            transaction["items"][1]["drop"] == "NoLiability",
            source + " dynamic transaction lost receiver/argument roles")

    source = "tests/semantics/call_transfer_shadow_m1/dynamic_trait_isolation.tk"
    require_shadow_parity(tokac, source, expected_error="E0473")
    records = run(tokac, source, expected_error="E0473")
    receipts.append(find(
        records, source, callee="Sink::take",
        route="dynamic-trait-method", parameter="value",
        spelling="explicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="value",
        formal_ceded=True,
    ))

    source = "tests/semantics/call_transfer_shadow_m1/rvalue_and_unknown.tk"
    records = run(tokac, source, expected_error="E0402")
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        location_line=9, spelling="explicit", value_category="Temporary",
        transfer="ConsumeTemporary", source="NoSourcePlace", source_path="",
    ))
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        location_line=11, spelling="explicit", value_category="Temporary",
        transfer="ConsumeTemporary", source="NoSourcePlace", source_path="",
    ))
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        location_line=12, spelling="implicit", value_category="Indeterminate",
        transfer="Reject", source="NoStateChange",
        dependency="Indeterminate", source_path="",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/address_rvalue.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume_raw", route="ordinary",
        parameter="value", spelling="explicit", value_category="Temporary",
        transfer="ConsumeTemporary", source="NoSourcePlace",
        dependency="RawUnsafe", source_path="",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/raw_selector_obligation.tk"
    records = run(tokac, source)
    record = find(
        records, source, callee="consume_raw", route="ordinary",
        parameter="value", value_category="Place",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   value_production="CopyIdentity",
                   source="InvalidateBinding", source_view="RawHandle",
                   obligation_before="Outstanding",
                   source_obligation_action="TransferToCallee",
                   source_obligation_after="Discharged",
                   destination_obligation_action="ReceiveTransferred",
                   destination_obligation_after="Outstanding")
    require("/dereference" not in record["stage0"]["exact_path"],
            source + " retained selector syntax as a place projection")

    source = "tests/semantics/call_transfer_shadow_m1/raw_selector_borrow_conflict.tk"
    records = run(tokac, source, expected_error="E0440")
    record = find(
        records, source, callee="consume_raw", route="ordinary",
        parameter="value", location_line=8, value_category="Place",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="ActiveDerivedBorrow",
                   source="NoStateChange", source_view="RawHandle")
    require(bool(record["stage0"]["exact_path"]) and
            "/dereference" not in record["stage0"]["exact_path"],
            source + " queried PAL with an unnormalized raw selector path")

    source = "tests/semantics/call_transfer_shadow_m1/borrow_construction_facts.tk"
    records = run(tokac, source)
    record = find(
        records, source, callee="borrow_identity", route="ordinary",
        parameter="'value", value_category="Temporary",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   value_production="CopyIdentity", source="NoSourcePlace",
                   source_view="ReferenceConstruction",
                   temporary_eligibility="Ineligible",
                   formal_ownership="Borrowed",
                   formal_transfer_class="IdentityTransfer",
                   declared_formal_morphology="Morphic",
                   formal_contract_origin="MorphicGenericDeclaration")
    require(record["stage0"]["dependency"] == "Borrowed" and
            record["stage0"]["dependency_complete"] and
            bool(record["stage0"]["referent_root"]) and
            bool(record["stage0"]["referent_path"]) and
            len(record["stage0"]["dependency_roots"]) == 1,
            source + " did not derive fresh borrow referent/dependency facts")

    source = "tests/semantics/call_transfer_shadow_m1/postfix_rvalue.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume", location_line=7,
        spelling="explicit", value_category="Temporary",
        transfer="ConsumeTemporary", source="NoSourcePlace",
        source_path="",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/execution_boundaries.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume_async", route="ordinary",
        execution_boundary="StartHandoff", **{"async": True},
    ))
    receipts.append(find(
        records, source, callee="wrap", route="ordinary",
        execution_boundary="None", **{"async": False},
    ))

    source = "tests/pass/g09_thread_example.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="thread_spawn", route="ordinary",
        location_line=17, execution_boundary="ThreadHandoff",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/boundary_identity.tk"
    require_shadow_parity(tokac, source, expected_error="E0476")
    records = run(tokac, source, expected_error="E0476")
    receipts.append(find(
        records, source, callee="thread_spawn", location_line=8,
        execution_boundary="None",
    ))
    receipts.append(find(
        records, source, callee="spawn", location_line=9,
        execution_boundary="ThreadHandoff",
    ))

    source = "tests/pass/g16_init_parameter_test.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="initialize", route="ordinary",
        parameter="out", value_category="InitStorage",
        transfer="InitStorage", formal_init=True, actual_init=True,
        formal_index=1,
    ))

    source = "tests/semantics/call_transfer_shadow_m1/init_spelling_mismatch.tk"
    records = run(tokac, source, expected_error="E04622")
    receipts.append(find(
        records, source, callee="initialize", location_line=8,
        formal_init=True, actual_init=False,
        value_category="Indeterminate", transfer="Reject",
    ))
    receipts.append(find(
        records, source, callee="observe", location_line=10,
        formal_init=False, actual_init=True,
        value_category="Place", transfer="CopyValue",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/borrowed_view_paths.tk"
    records = run(tokac, source)
    record = find(
        records, source, callee="consume_view", route="ordinary",
        parameter="value", value_category="Place", transfer="CopyIdentity",
        source="InvalidatePlace", dependency="Borrowed", source_path="view",
        dependency_paths=["owner.buf"],
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   value_production="CopyIdentity",
                   formal_ownership="Borrowed",
                   formal_transfer_class="IdentityTransfer",
                   formal_contract_origin="ConcreteDeclaration")

    source = "tests/semantics/call_transfer_shadow_m1/generic_borrowed_contract_origin.tk"
    records = run(tokac, source)
    record = find(
        records, source, callee="consume_generic", route="ordinary",
        parameter="value", value_category="Place",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Rejected",
                   rejection="OwnershipContractMismatch",
                   formal_contract_origin="GenericValueDeclaration",
                   formal_transfer_class="ValueTransfer",
                   formal_ownership="Borrowed")

    source = "tests/semantics/call_transfer_shadow_m1/concrete_alias_contract_origin.tk"
    records = run(tokac, source)
    record = find(
        records, source, callee="consume_alias", route="ordinary",
        parameter="value", value_category="Place",
    )
    receipts.append(record)
    require_stage0(record, source, outcome="Admitted", rejection="None",
                   declared_formal_morphology="DirectValue",
                   formal_morphology="DirectValue",
                   formal_contract_origin="ConcreteDeclaration",
                   formal_transfer_class="IdentityTransfer")

    source = "tests/semantics/call_transfer_shadow_m1/generic_morphology_contract_origin.tk"
    records = run(tokac, source)
    generic_raw = find(
        records, source, callee="consume_generic", route="ordinary",
        location_line=9,
    )
    receipts.append(generic_raw)
    require_stage0(generic_raw, source, outcome="Rejected",
                   rejection="OwnershipContractMismatch",
                   declared_formal_morphology="DirectValue",
                   formal_morphology="RawHandle",
                   formal_contract_origin="GenericValueDeclaration",
                   formal_transfer_class="ValueTransfer")
    generic_unique = find(
        records, source, callee="consume_generic", route="ordinary",
        location_line=13,
    )
    receipts.append(generic_unique)
    require_stage0(generic_unique, source,
                   declared_formal_morphology="DirectValue",
                   formal_morphology="UniqueHandle",
                   formal_contract_origin="GenericValueDeclaration",
                   formal_transfer_class="ValueTransfer")
    generic_callable = find(
        records, source, callee="consume_generic", route="ordinary",
        location_line=17,
    )
    receipts.append(generic_callable)
    require_stage0(generic_callable, source, outcome="Rejected",
                   rejection="OwnershipContractMismatch",
                   declared_formal_morphology="DirectValue",
                   formal_morphology="Callable",
                   formal_contract_origin="GenericValueDeclaration",
                   formal_transfer_class="ValueTransfer")

    source = "tests/semantics/call_transfer_shadow_m1/borrowed_projection_paths.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume", location_line=13,
        source_path="borrowed.value", referent_path="holder.value",
        referent_identity="holder.value", dependency="Borrowed",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/miss_outcome_carriers.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="observe", location_line=34,
        transfer="BorrowCapture", source="KeepLive",
        dependency="Unclassified", drop="SourceRetainsLiability",
        source_path="token",
    ))
    receipts.append(find(
        records, source, callee="consume_number", location_line=37,
        transfer="CopyValue", source="InvalidatePlace",
        dependency="None", drop="NoLiability", source_path="number",
    ))
    receipts.append(find(
        records, source, callee="consume_borrow", location_line=41,
        transfer="CopyIdentity", source="InvalidatePlace",
        dependency="Borrowed", drop="NoLiability", source_path="borrowed",
    ))

    source = "tests/pass/g09_sync_condvar.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="wait_cond", location_line=27,
        source_path="lock",
    ))
    require(sum(1 for record in records
                if record["callee"] == "wait_cond" and
                record["location"]["file"].endswith(source) and
                record["location"]["line"] == 27) == 1,
            "closure capture precompute emitted a speculative plan")
    require(sum(1 for transaction in TRANSACTIONS[source]
                if transaction["callee"] == "wait_cond" and
                transaction["location"]["file"].endswith(source) and
                transaction["location"]["line"] == 27) == 1,
            "closure capture precompute emitted a speculative transaction")

    source = "tests/semantics/call_transfer_shadow_m1/closure_callable_replay.tk"
    records = run(tokac, source)
    observed = find(
        records, source, callee="observe", location_line=15,
        source_path="value",
    )
    callable_record = find(
        records, source, callee="consumer", route="callable",
        location_line=16, source_path="value",
    )
    require(observed["source_root_id"] == callable_record["source_root_id"],
            "callable replay retained a precompute-scope source identity")
    receipts.extend((observed, callable_record))

    source = "tests/semantics/call_transfer_shadow_m1/multi_argument.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="combine", route="ordinary",
        parameter="first", argument_index=1, formal_index=1,
        transfer="MoveOwned", source_path="first",
    ))
    receipts.append(find(
        records, source, callee="combine", route="ordinary",
        parameter="second", argument_index=2, formal_index=2,
        transfer="MoveOwned", source_path="second",
    ))

    source = "tests/semantics/call_transfer_shadow_m1/transaction_argument_alias.tk"
    require_shadow_parity(tokac, source, expected_error="E0475")
    run(tokac, source, expected_error="E0475")
    transaction = find_transaction(
        source, callee="combine", route="ordinary", location_line=8,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallAliasConflict" and
            not transaction["commit_allowed"] and
            not transaction["has_receiver"] and
            transaction["argument_count"] == 2 and
            [item["source"] for item in transaction["items"]] ==
            ["InvalidateSubtree", "KeepLive"],
            source + " did not reject invalidation-vs-read atomically")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_receiver_alias.tk"
    require_shadow_parity(tokac, source, expected_error="E0438")
    run(tokac, source, expected_error="E0438")
    transaction = find_transaction(
        source, callee="combine", route="method", location_line=10,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallAliasConflict" and
            not transaction["commit_allowed"] and
            transaction["has_receiver"] and
            transaction["argument_count"] == 1 and
            [item["role"] for item in transaction["items"]] ==
            ["receiver", "argument"] and
            [item["source"] for item in transaction["items"]] ==
            ["InvalidateSubtree", "KeepLive"],
            source + " did not reject receiver-vs-argument atomically")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_source_liveness.tk"
    require_shadow_parity(tokac, source, expected_error="E0438")
    run(tokac, source, expected_error="E0438")
    moved_transaction = find_transaction(
        source, callee="consume", route="ordinary", location_line=9,
    )
    uninit_transaction = find_transaction(
        source, callee="consume", route="ordinary", location_line=14,
    )
    require(moved_transaction["items"][0]["source_liveness"] == "Moved" and
            moved_transaction["items"][0]["rejection"] == "SourceNotLive" and
            not moved_transaction["commit_allowed"] and
            uninit_transaction["items"][0]["source_liveness"] ==
            "Uninitialized" and
            uninit_transaction["items"][0]["rejection"] ==
            "SourceNotLive" and
            not uninit_transaction["commit_allowed"],
            source + " transaction admitted a non-live source")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_partial_cleanup.tk"
    require_shadow_parity(tokac, source, expected_error="E0410")
    run(tokac, source, expected_error="E0410")
    live_projection = find_transaction(
        source, callee="consume", route="ordinary", location_line=10,
    )
    moved_projection = find_transaction(
        source, callee="consume", route="ordinary", location_line=11,
    )
    require(live_projection["items"][0]["source_liveness"] == "Live" and
            live_projection["items"][0]["cleanup_mask"] == 3 and
            live_projection["commit_allowed"] and
            moved_projection["items"][0]["source_liveness"] == "Moved" and
            moved_projection["items"][0]["cleanup_mask"] == 2 and
            moved_projection["items"][0]["rejection"] == "SourceNotLive" and
            not moved_projection["commit_allowed"],
            source + " transaction lost partial cleanup/liveness state")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_arity.tk"
    require_shadow_parity(tokac, source, expected_error="E04568")
    run(tokac, source, expected_error="E04568")
    transaction = find_transaction(
        source, callee="combine", route="ordinary", location_line=6,
    )
    require(transaction["outcome"] == "Rejected" and
            transaction["rejection"] == "WholeCallArityIncomplete" and
            not transaction["local_plan_admitted"] and
            not transaction["commit_allowed"] and
            not transaction["arity_complete"] and
            transaction["validation_complete"] and
            transaction["expected_argument_count"] == 2 and
            transaction["actual_argument_count"] == 1 and
            transaction["argument_count"] == 1,
            source + " transaction omitted an unmatched formal slot")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_receiver_paths.tk"
    require_shadow_parity(tokac, source)
    run(tokac, source)
    member_receiver = find_transaction(
        source, callee="inspect", route="method", location_line=12,
    )
    index_receiver = find_transaction(
        source, callee="inspect", route="method", location_line=13,
    )
    for transaction in (member_receiver, index_receiver):
        item = transaction["items"][0]
        require(transaction["has_receiver"] and
                transaction["commit_allowed"] and
                item["actual_type"] == "i32" and
                item["source_liveness"] == "Live" and
                item["outcome"] == "Admitted",
                source + " member/index receiver lost pre-mutation facts")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_temporary_liability.tk"
    require_shadow_parity(tokac, source)
    run(tokac, source)
    transaction = find_transaction(
        source, callee="consume_pair", route="ordinary", location_line=16,
    )
    liability_ids = [item["liability_identity"]
                     for item in transaction["items"]]
    require(transaction["commit_allowed"] and
            all(item["value_production"] == "ConsumeTemporary"
                for item in transaction["items"]) and
            all(item["drop"] == "CalleeAssumesLiability"
                for item in transaction["items"]) and
            len(liability_ids) == 2 and all(liability_ids) and
            len(set(liability_ids)) == 2 and
            all("crate:workspace;module:" in identity and
                "/private/tmp/" not in identity
                for identity in liability_ids),
            source + " temporary cleanup identities collided or were unstable")

    for source, route, expected_count, actual_count in (
        ("tests/semantics/call_transfer_shadow_m1/transaction_indirect_fn_arity.tk",
         "indirect-fn", 2, 1),
        ("tests/semantics/call_transfer_shadow_m1/transaction_indirect_dyn_arity.tk",
         "indirect-dyn-fn", 2, 3),
    ):
        require_shadow_parity(tokac, source, expected_error="E04553")
        run(tokac, source, expected_error="E04553")
        transaction = find_transaction(
            source, callee="callback", route=route, location_line=3,
        )
        require(transaction["outcome"] == "Rejected" and
                transaction["rejection"] == "WholeCallArityIncomplete" and
                not transaction["commit_allowed"] and
                not transaction["arity_complete"] and
                transaction["validation_complete"] and
                transaction["expected_argument_count"] == expected_count and
                transaction["actual_argument_count"] == actual_count and
                transaction["argument_count"] ==
                min(expected_count, actual_count) and
                all(item["role"] in ("receiver", "argument")
                    for item in transaction["items"]) and
                transaction["has_receiver"],
                source + " indirect arity failure omitted transaction facts")

    source = "tests/semantics/call_transfer_shadow_m1/transaction_indirect_receiver_handshake.tk"
    require_shadow_parity(tokac, source, expected_error="E04591")
    run(tokac, source, expected_error="E04591")
    handshake = {}
    for callee, line in (("ordinary_bare", 3), ("ordinary_explicit", 6),
                         ("consuming_bare", 9),
                         ("consuming_explicit", 12)):
        transaction = find_transaction(
            source, callee=callee, route="indirect-fn", location_line=line,
        )
        handshake[callee] = (
            transaction["items"][0]["surface_spelling"],
            transaction["items"][0]["formal_contract"],
        )
    require(handshake == {
                "ordinary_bare": ("Bare", "Ordinary"),
                "ordinary_explicit": ("ExplicitCede", "Ordinary"),
                "consuming_bare": ("Bare", "Cede"),
                "consuming_explicit": ("ExplicitCede", "Cede"),
            },
            source + " indirect receiver collapsed caller/formal handshake")

    source = "tests/semantics/call_transfer_shadow_m1/generic_nested_calls.tk"
    require_shadow_parity(tokac, source)
    records = run(tokac, source)
    receipts.extend((
        find(records, source, callee="ordinary_inner", route="ordinary",
             location_line=25),
        find(records, source, callee="consuming_inner", route="ordinary",
             location_line=27),
    ))
    nested_cases = (
        (25, "ordinary_inner", "ordinary"),
        (26, "read", "method"),
        (27, "consuming_inner", "ordinary"),
    )
    for line, callee, route in nested_cases:
        outer = find_transaction(
            source, callee="generic_outer", route="ordinary",
            location_line=line)
        inner = find_transaction(
            source, callee=callee, route=route, location_line=line)
        require(inner["snapshot_revision"] ==
                outer["snapshot_revision"] + 1 and
                outer["commit_allowed"] and inner["commit_allowed"] and
                sum(1 for transaction in TRANSACTIONS[source]
                    if transaction["callee"] == callee and
                    transaction["route"] == route and
                    transaction["location"]["file"].endswith(source) and
                    transaction["location"]["line"] == line) == 1,
                source + ":" + str(line) +
                " did not publish exactly one final nested transaction")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_nested_generic_calls.tk")
    require_shadow_parity(tokac, source)
    run(tokac, source)
    outer = find_transaction(
        source, callee="generic_outer", route="ordinary", location_line=14)
    middle = find_transaction(
        source, callee="generic_middle", route="ordinary", location_line=14)
    inner = find_transaction(
        source, callee="inner", route="ordinary", location_line=14)
    require(middle["snapshot_revision"] ==
            outer["snapshot_revision"] + 1 and
            inner["snapshot_revision"] ==
            middle["snapshot_revision"] + 1 and
            all(transaction["commit_allowed"]
                for transaction in (outer, middle, inner)) and
            sum(1 for transaction in TRANSACTIONS[source]
                if transaction["callee"] == "inner" and
                transaction["location"]["file"].endswith(source)) == 1,
            source + " did not nest exact-depth generic permits")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_nested_deduction_failure.tk")
    require_shadow_parity(tokac, source, expected_error="E04554")
    run(tokac, source, expected_error="E04554")
    rejection = find_transaction(
        source, callee="require_same", route="ordinary", location_line=10)
    require(rejection["outcome"] == "Rejected" and
            rejection["rejection"] == "GenericDeductionFailed" and
            not rejection["local_plan_admitted"] and
            not rejection["commit_allowed"] and
            rejection["validation_complete"] and
            rejection["argument_count"] == 0 and
            rejection["items"] == [] and
            not any(transaction["callee"] == "ordinary_inner" and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " leaked a discarded candidate-local nested journal")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_type_argument_count_failure.tk")
    require_shadow_parity(tokac, source, expected_error="[FAILED]")
    run(tokac, source, expected_error="[FAILED]")
    rejection = find_transaction(
        source, callee="generic_outer", route="ordinary", location_line=6)
    require(rejection["outcome"] == "Rejected" and
            rejection["rejection"] ==
            "GenericTypeArgumentCountMismatch" and
            not rejection["local_plan_admitted"] and
            not rejection["commit_allowed"] and
            rejection["validation_complete"] and
            rejection["argument_count"] == 0 and
            rejection["items"] == [],
            source + " omitted its generic-resolution rejection envelope")

    fixture_dir = ROOT / "tests/semantics/call_transfer_shadow_m1"
    overload_transactions = []
    for ordering in ("forward", "reverse"):
        source = ("tests/semantics/call_transfer_shadow_m1/"
                  "generic_nested_overload_" + ordering + ".tk")
        include = ("-I", str(fixture_dir))
        require_shadow_parity(tokac, source, extra=include)
        run(tokac, source, extra=include)
        outer = find_transaction(
            source, callee="generic_outer", route="ordinary",
            location_line=12)
        selected = find_transaction(
            source, callee="choose", route="ordinary", location_line=12)
        inner = find_transaction(
            source, callee="inner", route="ordinary", location_line=12)
        require(selected["snapshot_revision"] ==
                outer["snapshot_revision"] + 1 and
                inner["snapshot_revision"] ==
                selected["snapshot_revision"] + 1 and
                outer["commit_allowed"] and
                selected["commit_allowed"] and inner["commit_allowed"] and
                sum(1 for transaction in TRANSACTIONS[source]
                    if transaction["callee"] == "inner" and
                    transaction["location"]["file"].endswith(source) and
                    transaction["location"]["line"] == 12) == 1,
                source + " leaked a candidate-probe nested transaction")
        overload_transactions.append((outer, selected, inner))
    require(all((left["outcome"], left["rejection"],
                 left["commit_allowed"], left["argument_count"]) ==
                (right["outcome"], right["rejection"],
                 right["commit_allowed"], right["argument_count"])
                for left, right in zip(overload_transactions[0],
                                       overload_transactions[1])),
            "reversing overload candidate declaration order changed the "
            "selected nested transaction")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_body_semantic_failure.tk")
    require_shadow_parity(tokac, source, expected_error="E0408")
    run(tokac, source, expected_error="E0408")
    failed_parent = find_transaction(
        source, callee="generic_bad", route="ordinary", location_line=10)
    require(failed_parent["outcome"] == "Rejected" and
            failed_parent["rejection"] == "WholeCallValidationFailed" and
            not failed_parent["commit_allowed"] and
            not any(transaction["callee"] == "inner" and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " committed a child journal after body Sema failure")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_body_call_isolation.tk")
    require_shadow_parity(tokac, source)
    run(tokac, source)
    instantiated_calls = [
        transaction for transaction in TRANSACTIONS[source]
        if transaction["callee"] == "generic_body" and
        transaction["location"]["file"].endswith(source)]
    require(len(instantiated_calls) == 2 and
            all(transaction["commit_allowed"]
                for transaction in instantiated_calls) and
            not any(transaction["callee"] == "body_call" and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " leaked generic-body calls across monomorphizations")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_specialization_cache_invalid.tk")
    require_shadow_parity(tokac, source, expected_error="E0408")
    run(tokac, source, expected_error="E0408")
    invalid_calls = [
        find_transaction(source, callee="generic_bad", route="ordinary",
                         location_line=line)
        for line in (14, 15)]
    require(all(transaction["outcome"] == "Rejected" and
                transaction["rejection"] == "WholeCallValidationFailed" and
                not transaction["commit_allowed"]
                for transaction in invalid_calls) and
            not any(transaction["callee"] in
                    ("first_inner", "second_inner") and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " forgot invalid specialization state on cache hit")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_specialization_cache_valid.tk")
    require_shadow_parity(tokac, source)
    run(tokac, source)
    valid_calls = [
        find_transaction(source, callee="generic_good", route="ordinary",
                         location_line=line)
        for line in (14, 15)]
    valid_children = [
        find_transaction(source, callee=callee, route="ordinary",
                         location_line=line)
        for callee, line in (("first_inner", 14), ("second_inner", 15))]
    require(all(transaction["commit_allowed"]
                for transaction in valid_calls + valid_children) and
            all(child["snapshot_revision"] ==
                parent["snapshot_revision"] + 1
                for parent, child in zip(valid_calls, valid_children)),
            source + " lost a real argument evaluation on valid cache hit")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_validation_transitive_invalid.tk")
    require_shadow_parity(tokac, source, expected_error="E0408")
    run(tokac, source, expected_error="E0408")
    invalid_chain = [
        find_transaction(source, callee=callee, route="ordinary",
                         location_line=line)
        for callee, line in (("generic_bad", 19),
                             ("generic_wrapper2", 20),
                             ("generic_wrapper", 21),
                             ("generic_wrapper2", 22))]
    require(all(transaction["outcome"] == "Rejected" and
                transaction["rejection"] == "WholeCallValidationFailed" and
                not transaction["commit_allowed"]
                for transaction in invalid_chain) and
            not any(transaction["callee"] in
                    ("first_inner", "second_inner", "third_inner",
                     "fourth_inner") and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " did not propagate invalid validation dependencies")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_validation_transitive_valid.tk")
    require_shadow_parity(tokac, source)
    run(tokac, source)
    valid_chain = [
        find_transaction(source, callee="generic_wrapper2", route="ordinary",
                         location_line=line)
        for line in (17, 18)]
    valid_chain_children = [
        find_transaction(source, callee=callee, route="ordinary",
                         location_line=line)
        for callee, line in (("first_inner", 17), ("second_inner", 18))]
    require(all(transaction["commit_allowed"]
                for transaction in valid_chain + valid_chain_children),
            source + " rejected a valid specialization dependency chain")

    source = ("tests/semantics/call_transfer_shadow_m1/"
              "generic_validation_recursion.tk")
    require_shadow_parity(tokac, source, expected_error="E0406")
    run(tokac, source, expected_error="E0406")
    recursive = find_transaction(
        source, callee="recursive", route="ordinary", location_line=8)
    require(recursive["outcome"] == "Rejected" and
            recursive["rejection"] == "WholeCallValidationFailed" and
            not recursive["commit_allowed"] and
            not any(transaction["callee"] == "inner" and
                    transaction["location"]["file"].endswith(source)
                    for transaction in TRANSACTIONS[source]),
            source + " did not fail closed on Unchecked recursion")

    with tempfile.TemporaryDirectory(
            prefix="toka-call-transfer-mangled-cache-") as temp:
        mangled_source = Path(temp) / "mangled_lookup.tk"
        mangled_source.write_text(
            "fn inner() -> i32 { return 42:i32 }\n\n\n"
            "fn generic_bad<T>(value: T) -> T {\n"
            "    return generic_bad(value)\n"
            "}\n\n"
            "fn main() -> i32 {\n"
            "    return generic_bad(inner())\n"
            "}\n")
        recursion_probe = subprocess.run(
            [str(tokac), STAGE1_LEGACY, "--check-only", str(mangled_source)], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        require(recursion_probe.returncode != 0 and
                "recursive generic specialization is not supported:" in
                recursion_probe.stderr,
                "mangled cache probe did not expose its concrete alias")
        match = re.search(
            r"recursive generic specialization is not supported: "
            r"([A-Za-z0-9_]+)", recursion_probe.stderr)
        require(match is not None,
                "mangled cache probe emitted no reusable alias")
        mangled_name = match.group(1)

        mangled_source.write_text(
            "fn first_inner() -> i32 { return 20:i32 }\n"
            "fn second_inner() -> i32 { return 22:i32 }\n\n"
            "fn generic_bad<T>(value: T) -> T {\n"
            "    return true\n"
            "}\n\n"
            "fn main() -> i32 {\n"
            "    auto prime = generic_bad(first_inner())\n"
            "    auto cached = " + mangled_name + "(second_inner())\n"
            "    return prime + cached\n"
            "}\n")
        source = str(mangled_source)
        require_shadow_parity(tokac, source, expected_error="E0408")
        mangled_records = run(tokac, source, expected_error="E0408")
        source_call = find_transaction(
            source, callee="generic_bad", route="ordinary", location_line=9)
        mangled_call = find_transaction(
            source, callee=mangled_name, route="ordinary", location_line=10)
        mangled_record = find(
            mangled_records, source, callee=mangled_name, route="ordinary",
            location_line=10, argument_index=1, formal_index=1)
        mangled_fixture_transactions = [
            transaction for transaction in TRANSACTIONS[source]
            if transaction["location"]["file"].endswith(source)]
        require(not source_call["commit_allowed"] and
                not mangled_call["commit_allowed"] and
                source_call["rejection"] == "WholeCallValidationFailed" and
                mangled_call["rejection"] == "WholeCallValidationFailed" and
                mangled_record["stage0"]["outcome"] == "Admitted" and
                not any(transaction["callee"] in
                        ("first_inner", "second_inner") and
                        transaction["location"]["file"].endswith(source)
                        for transaction in TRANSACTIONS[source]),
                "mangled cache alias discarded specialization validity: " +
                json.dumps(mangled_fixture_transactions, sort_keys=True))
        require(not any(record["callee"] in
                        ("first_inner", "second_inner") and
                        record["location"]["file"].endswith(source)
                        for record in mangled_records),
                "mangled cache alias retained a nested child record")

    replay_case = ROOT / "tests/semantics/tki_replay/cases/own_cede_003_generic_methods"
    with tempfile.TemporaryDirectory(prefix="toka-call-transfer-shadow-") as temp:
        work = Path(temp)
        for name in ("lib.tk", "fail_generic_missing_cede.tk"):
            shutil.copy2(replay_case / name, work / name)
        provider = subprocess.run(
            [str(tokac), STAGE1_LEGACY, "-c", work / "lib.tk", "-o", work / "lib.o"],
            cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30,
        )
        require(provider.returncode == 0 and (work / "lib.tki").is_file(),
                "source-less shadow provider compilation failed")

        def replay_record():
            normal = subprocess.run(
                [str(tokac), STAGE1_LEGACY, work / "fail_generic_missing_cede.tk"],
                cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=30,
            )
            shadow = subprocess.run(
                [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json",
                 work / "fail_generic_missing_cede.tk"],
                cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=30,
            )
            require(normal.returncode == shadow.returncode and
                    normal.stderr == shadow.stderr and
                    normal.returncode != 0 and "E0438" in normal.stderr,
                    "source-less shadow consumer diverged from normal")
            payload = json.loads(shadow.stdout)
            matches = [record for record in payload["records"]
                       if record["callee"] == "forward_parcel" and
                       record["location"]["file"].endswith(
                           "fail_generic_missing_cede.tk")]
            require(len(matches) == 1,
                    "source-less shadow consumer omitted its plan")
            record = dict(matches[0])
            record.pop("location")
            record.pop("contract_location")
            record.pop("source_root_id")
            transactions = [transaction
                            for transaction in payload["transactions"]
                            if transaction["callee"] == "forward_parcel" and
                            transaction["location"]["file"].endswith(
                                "fail_generic_missing_cede.tk")]
            require(len(transactions) == 1,
                    "source-less shadow consumer omitted its transaction")
            transaction = dict(transactions[0])
            require(not transaction["local_plan_admitted"] and
                    not transaction["commit_allowed"] and
                    transaction["items"][0]["rejection"] ==
                    "MissingCedeForNamedSource" and
                    transaction["items"][0]["source"] == "NoStateChange",
                    "source-less transaction lost explicit-cede policy")
            transaction.pop("location")
            transaction.pop("snapshot_revision")
            transaction.pop("pal_revision")
            return record, transaction

        source_record, source_transaction = replay_record()
        (work / "lib.tk").rename(work / "lib.tk.source-hidden")
        hidden_record, hidden_transaction = replay_record()
        require(source_record == hidden_record,
                "source and source-less shadow plans differ")
        require(source_transaction == hidden_transaction,
                "source and source-less transactions differ")
        receipts.append(hidden_record)

    mixed = subprocess.run(
        [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json",
         "--cede-obligations=json", "--check-only", source],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    require(mixed.returncode == 1,
            "mixed shadow/evidence output mode was accepted")
    require(not mixed.stdout,
            "mixed shadow/evidence output emitted ambiguous stdout")
    require("cannot be combined" in mixed.stderr,
            "mixed shadow/evidence output missed its diagnostic")

    diagnostics_mixed = subprocess.run(
        [str(tokac), STAGE1_LEGACY, "--call-transfer-shadow=json", "--diagnostics-json",
         source],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
    )
    require(diagnostics_mixed.returncode == 1,
            "mixed shadow/diagnostics output mode was accepted")
    require(not diagnostics_mixed.stdout,
            "mixed shadow/diagnostics output emitted ambiguous stdout")
    require("cannot be combined" in diagnostics_mixed.stderr,
            "mixed shadow/diagnostics output missed its diagnostic")

    normal_cases = [
        ("tests/semantics/call_transfer_shadow_m1/copy_places.tk", None),
        ("tests/conformance/diagnostics/static_cede_parameter_requires_explicit_transfer.tk", None),
        ("tests/conformance/diagnostics/cede_argument_to_borrowed_parameter_rejected.tk", "E04640"),
        ("tests/conformance/diagnostics/aggregate_owned_value_requires_cede.tk", "E04652"),
    ]
    for normal_source, expected_error in normal_cases:
        normal = subprocess.run(
            [str(tokac), STAGE1_LEGACY, "--check-only", normal_source], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30,
        )
        require(not normal.stdout, normal_source + " changed normal stdout")
        if expected_error:
            require(normal.returncode != 0 and expected_error in normal.stderr,
                    normal_source + " changed normal diagnostics")
        else:
            require(normal.returncode == 0,
                    normal_source + " changed normal success behavior")

    all_transactions = [transaction
                        for transactions in TRANSACTIONS.values()
                        for transaction in transactions]
    require(not MISSING_PRE_MUTATION,
            "qualified routes emitted records without pre-mutation "
            "transactions: " + repr(MISSING_PRE_MUTATION[:5]))
    require(not TRANSACTION_RECORD_GAPS,
            "transaction/record correspondence gaps: " +
            repr(TRANSACTION_RECORD_GAPS))
    expected_transaction_routes = {
        "ordinary", "static", "method", "callable", "extern",
        "indirect-fn", "indirect-dyn-fn", "dynamic-trait-method",
    }
    transaction_routes = {transaction["route"]
                          for transaction in all_transactions}
    require(expected_transaction_routes <= transaction_routes,
            "Stage-0 transaction coverage missed routes: " +
            ", ".join(sorted(expected_transaction_routes -
                             transaction_routes)))
    for transaction in all_transactions:
        if transaction["route"] in (
                "method", "callable", "dynamic-trait-method",
                "indirect-fn", "indirect-dyn-fn"):
            require(transaction["has_receiver"],
                    transaction["route"] +
                    " transaction omitted its receiver slot")
        if (transaction["route"] in ("indirect-fn", "indirect-dyn-fn") and
                transaction["argument_count"] > 0 and
                transaction["arity_complete"]):
            require(transaction["outcome"] == "Rejected" and
                    transaction["rejection"] == "WholeCallItemRejected" and
                    not transaction["commit_allowed"],
                    transaction["route"] +
                    " transaction inferred source-hidden authority")

    print(json.dumps({
        "schema": "toka.rc9-m1-call-transfer-shadow-audit",
        "version": 5,
        "result": "pass",
        "cases": len(receipts),
        "transactions": len(all_transactions),
        "normal_cases": len(normal_cases),
        "parity_cases": len(PARITY_CASES),
        "routes": sorted({record["route"] for record in receipts}),
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
