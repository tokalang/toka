#!/usr/bin/env python3
"""Qualify the Shadow-only RC9 M1b D.3a ordinary direct-call slice."""

import argparse
import json
import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"
MATRIX = ROOT / "tests/semantics/direct_call_observation_d3a/matrix.tk"
ORDER_A = ROOT / "tests/semantics/direct_call_observation_d3a/copy_order_a.tk"
ORDER_B = ROOT / "tests/semantics/direct_call_observation_d3a/copy_order_b.tk"
DYNAMIC = ROOT / "tests/semantics/call_transfer_shadow_m1/dynamic_trait_method.tk"
CALLABLE = ROOT / "tests/semantics/call_transfer_shadow_m1/closure_callable_replay.tk"
BOUNDARY = ROOT / "tests/semantics/call_transfer_shadow_m1/boundary_identity.tk"
COPY_PLACES = ROOT / "tests/semantics/call_transfer_shadow_m1/copy_places.tk"
OVERLOAD = ROOT / "tests/semantics/direct_call_observation_d3a/overload_gate.tk"
NESTED = ROOT / "tests/semantics/direct_call_observation_d3a/nested_complex.tk"
STABLE_PLACE = ROOT / "tests/semantics/direct_call_observation_d3a/stable_place.tk"
UNIQUE_PLACE = ROOT / "tests/semantics/direct_call_observation_d3a/unique_place.tk"
PROBE = ROOT / "tests/semantics/direct_call_observation_d3a/probe_consumer.tk"
GENERIC_PROBE = ROOT / "tests/semantics/direct_call_observation_d3a/generic_probe.tk"
GLOBAL_PLACE = ROOT / "tests/semantics/direct_call_observation_d3a/global_place.tk"
NONCEDE_UNIQUE = ROOT / "tests/semantics/direct_call_observation_d3a/noncede_unique.tk"
ASYNC_DANGLING = ROOT / "tests/fail/async_dangling_expression_contexts.tk"
CEDE_BORROW_CONFLICT = ROOT / "tests/conformance/diagnostics/cede_unique_parameter_borrow_conflict.tk"
SCHEMA_PATH = ROOT / "tests/semantics/direct_call_observation_d3a/receipt_schema_v1.json"
FLAG = "--m1b-d3-direct-call-observation=json"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def require_exact_keys(value, expected, context):
    require(set(value) == set(expected),
            f"{context} keys changed: {sorted(value)}")


def validate_schema(payload):
    schema = json.loads(SCHEMA_PATH.read_text())
    require_exact_keys(payload, schema["top_level"], "top-level receipt")
    require_exact_keys(payload["gate_exclusions"], schema["gate_exclusions"],
                       "gate exclusions")
    for envelope in payload["envelopes"]:
        require_exact_keys(envelope, schema["envelope"], "envelope")
        require_exact_keys(envelope["comparison"], schema["comparison"],
                           "comparison")
        record = envelope["factory_record"]
        expected_record = list(schema["factory_record_common"])
        if record["admission"] == "Admitted":
            expected_record += schema["factory_record_admitted"]
        require_exact_keys(record, expected_record, "factory record")
        require_exact_keys(record["call_site"], schema["location"],
                           "call location")
        require_exact_keys(record["formal"], schema["formal"], "formal")
        require_exact_keys(record["formal"]["contract_location"],
                           schema["location"], "formal contract location")
        require_exact_keys(record["legacy_outcome"], schema["legacy_outcome"],
                           "legacy outcome")
        require_exact_keys(record["prospective_outcome"],
                           schema["prospective_outcome"],
                           "prospective outcome")
        for planned in record["transfer_edges"]:
            require_exact_keys(planned, schema["edge"], "transfer edge")
            for name in ("liability_source", "liability_target"):
                liability = planned[name]
                require_exact_keys(liability, schema["liability"], name)
                if liability["subject"] is not None:
                    require_exact_keys(liability["subject"], schema["subject"],
                                       name + " subject")
        for name in ("evaluation_delta", "boundary_delta",
                     "finalization_delta"):
            delta = record[name]
            if delta is None:
                continue
            require_exact_keys(delta, schema["delta"], name)
            for entry in delta["entries"]:
                require_exact_keys(entry, schema["delta_entry"],
                                   name + " entry")
                require_exact_keys(entry["subject_identity"],
                                   schema["subject"],
                                   name + " subject identity")
        for patch in record["semantic_model_patch"]:
            require_exact_keys(patch, schema["patch_entry"], "patch entry")
        if record["region_witness"] is not None:
            require_exact_keys(record["region_witness"],
                               schema["region_witness"], "region witness")
            require_exact_keys(record["region_witness"]["subject"],
                               schema["subject"], "region subject")


def audit(tokac, source, with_check_only=True, extra_args=()):
    command = [str(tokac), FLAG]
    if with_check_only:
        command.append("--check-only")
    command.extend(extra_args)
    command.append(str(source))
    result = run(command)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{source} emitted invalid or mixed D.3 JSON") from error
    require(payload.get("schema") ==
            "toka.internal.m1b-d3-direct-call-observation",
            f"{source} changed the D.3 schema")
    require(payload.get("version") == 1 and
            payload.get("status") == "shadow-only",
            f"{source} changed the D.3 protocol version/status")
    require(payload.get("integrity") is True,
            f"{source} failed a D.3 sentinel or count check")
    validate_schema(payload)
    count = payload.get("considered_call_count")
    invocations = payload.get("factory_invocation_count")
    envelopes = payload.get("envelopes")
    require(count == invocations == len(envelopes),
            f"{source} violated considered/factory/envelope parity")
    for envelope in envelopes:
        comparison = envelope.get("comparison", {})
        require(comparison.get("pre_fact_capture_unchanged") is True and
                comparison.get("post_cache_and_factory_unchanged") is True and
                comparison.get("differing_sentinel_fields") == [],
                f"{source} reported a mutable observation path")
    return result, payload


def source_records(payload, source):
    suffix = source.name
    return [entry["factory_record"] for entry in payload["envelopes"]
            if entry["factory_record"]["call_site"]["file"].endswith(suffix)]


def record_at(records, line, callee):
    matches = [record for record in records
               if record["call_site"]["line"] == line and
               record["callee"] == callee]
    require(len(matches) == 1,
            f"expected one D.3 record for {callee} at line {line}, got {len(matches)}")
    return matches[0]


def edge(record):
    require(len(record.get("transfer_edges", [])) == 1,
            f"{record['callee']} did not produce one transfer edge")
    return record["transfer_edges"][0]


def require_parity(tokac, source, extra_args=()):
    normal = run([str(tokac), "--check-only", *extra_args, str(source)])
    observed, payload = audit(tokac, source, extra_args=extra_args)
    require(normal.stdout == "",
            f"{source} normal check-only unexpectedly wrote stdout")
    return normal, observed, payload


def require_delta_provenance(record):
    admitted_edge = edge(record)
    edge_id = admitted_edge["edge_id"]
    for lane_name in ("evaluation_delta", "boundary_delta",
                      "finalization_delta"):
        lane = record[lane_name]
        require(lane is not None, f"{record['callee']} omitted {lane_name}")
        for entry in lane["entries"]:
            require(entry["edge_id"] == edge_id,
                    f"{record['callee']} delta escaped its transfer edge")
            require(set(entry) == {"edge_id", "state_domain",
                                   "subject_identity", "expected_before",
                                   "result_after", "provenance"},
                    f"{record['callee']} delta entry lost structural facts")


def copy_facts(payload, source):
    facts = {}
    for record in source_records(payload, source):
        if record["callee"] not in ("consume_pair", "consume_i32"):
            continue
        planned = edge(record)
        facts[record["callee"]] = (
            planned["type_proof"], planned["transfer_mode"],
            planned["source_disposition"])
    return facts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    build_dir = pathlib.Path(args.build_dir).resolve()
    tokac = build_dir / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    normal, first, matrix_payload = require_parity(tokac, MATRIX)
    require(normal.returncode == 1 and "E04570" not in normal.stderr and
            "E04640" in normal.stderr,
            "default matrix did not activate nested temporary transfer")
    require(first.returncode == 1 and "E04570" in first.stderr and
            "E04640" in first.stderr,
            "D.3 replay did not preserve its frozen legacy diagnostics")
    second, second_payload = audit(tokac, MATRIX)
    require(first.stdout == second.stdout and first.stderr == second.stderr and
            first.returncode == second.returncode,
            "D.3 receipt is not deterministic")

    implied, implied_payload = audit(tokac, MATRIX, with_check_only=False)
    require(implied.stdout == first.stdout and implied.stderr == first.stderr and
            implied.returncode == first.returncode and
            implied_payload == matrix_payload,
            "D.3 flag did not imply check-only deterministically")

    records = source_records(matrix_payload, MATRIX)
    expected = {
        (35, "inspect"): ("Admitted", None, "BorrowCapture", "KeepLive"),
        (38, "consume"): ("Admitted", None, "MoveOwned", "InvalidateWhole"),
        (41, "consume"): ("Admitted", None, "MoveOwned", "InvalidateWhole"),
        (44, "consume_pair"): ("Admitted", None, "CopyValue", "KeepLive"),
        (47, "consume_pair"): ("Admitted", None, "CopyValue", "InvalidateWhole"),
        (49, "consume"): ("Admitted", None, "ConsumeTemporary", "NoSourcePlace"),
        (52, "inspect_i32"): ("NotInSlice", "NonCedeScalar", None, None),
        (53, "inspect"): ("NotInSlice", "NonCedeAggregateTemporary", None, None),
        (56, "inspect"): ("Rejected", "BorrowedFormalExplicitCede", None, None),
        (59, "consume"): ("NotInSlice", "Projection", None, None),
        (61, "consume"): ("NotInSlice", "NestedObservation", None, None),
    }
    for (line, callee), wanted in expected.items():
        record = record_at(records, line, callee)
        transfer = source = None
        if record["transfer_edges"]:
            planned = edge(record)
            transfer = planned["transfer_mode"]
            source = planned["source_disposition"]
            require_delta_provenance(record)
            require(record["semantic_model_patch"] is not None and
                    record["region_witness"] is not None,
                    f"{callee} admitted without patch/region facts")
        else:
            require(record["evaluation_delta"] is None and
                    record["boundary_delta"] is None and
                    record["finalization_delta"] is None and
                    record["semantic_model_patch"] == [] and
                    record["region_witness"] is None,
                    f"{callee} exclusion/rejection carried semantic authority")
        actual = (record["admission"], record["reason"], transfer, source)
        require(actual == wanted,
                f"{callee} line {line} planned {actual}, expected {wanted}")

    explicit_move = record_at(records, 38, "consume")
    implicit_move = record_at(records, 41, "consume")
    require(edge(explicit_move)["transfer_mode"] ==
            edge(implicit_move)["transfer_mode"] and
            edge(explicit_move)["liability_target"] ==
            edge(implicit_move)["liability_target"],
            "implicit/explicit non-Copy liability plans diverged")
    require(implicit_move["legacy_outcome"]["status"] == "rejected" and
            "E04570" in implicit_move["legacy_outcome"]["diagnostic_codes"],
            "Shadow implicit move hid the frozen legacy caller rule")
    require(matrix_payload["gate_exclusions"]["NestedObservationContext"] >= 1,
            "nested inner direct call invoked the factory")
    inspect_edge = edge(record_at(records, 35, "inspect"))
    require(inspect_edge["dependency"] == "BorrowedCallRegion" and
            inspect_edge["boundary_access"] == "SharedBorrow" and
            inspect_edge["liability_source"]["kind"] == "SourceRetained",
            "BorrowCapture edge contradicts its loan/liability deltas")

    order_a_first, order_a_payload = audit(tokac, ORDER_A)
    order_a_second, order_a_payload_second = audit(tokac, ORDER_A)
    _, order_b_payload = audit(tokac, ORDER_B)
    require(order_a_first.stdout == order_a_second.stdout and
            order_a_payload == order_a_payload_second,
            "cold/repeated Copy observation changed its receipt")
    require(copy_facts(order_a_payload, ORDER_A) ==
            copy_facts(order_b_payload, ORDER_B) == {
                "consume_pair": ("Aggregate/ProvenCopy/Trivial", "CopyValue", "KeepLive"),
                "consume_i32": ("Scalar/ProvenCopy/Trivial", "CopyValue", "KeepLive"),
            }, "Copy proof depends on call order or cache warmth")

    _, _, copy_places_payload = require_parity(tokac, COPY_PLACES)
    copy_place_records = source_records(copy_places_payload, COPY_PLACES)
    scalar_temporary = record_at(copy_place_records, 28, "consume")
    require(edge(scalar_temporary)["liability_source"]["kind"] ==
            "NoLiability" and
            scalar_temporary["evaluation_delta"]["entries"][0]
            ["subject_identity"]["kind"] == "Temporary",
            "trivial temporary was mislabeled as a cleanup obligation")

    _, _, overload_payload = require_parity(tokac, OVERLOAD)
    require(not source_records(overload_payload, OVERLOAD) and
            overload_payload["gate_exclusions"][
                "CandidateProbeOrSpeculativeContext"] >= 1,
            "same-lexical overload bypassed the considered-call gate")

    _, _, nested_payload = require_parity(tokac, NESTED)
    nested_records = source_records(nested_payload, NESTED)
    require(len(nested_records) == 1 and
            nested_records[0]["callee"] == "consume_i32" and
            nested_records[0]["admission"] == "NotInSlice" and
            nested_records[0]["reason"] == "NestedObservation" and
            nested_payload["gate_exclusions"]["NestedObservationContext"] >= 1,
            "nested binary call produced an admitted edge or inner receipt")

    _, _, stable_payload = require_parity(tokac, STABLE_PLACE)
    stable_records = source_records(stable_payload, STABLE_PLACE)
    stable_edges = [edge(record) for record in stable_records
                    if record["callee"] in ("inspect_a", "inspect_b")]
    require(len(stable_edges) == 2 and
            stable_edges[0]["source_place"] == stable_edges[1]["source_place"],
            "source PlaceId depends on the destination callee")

    _, _, unique_payload = require_parity(tokac, UNIQUE_PLACE)
    unique_records = source_records(unique_payload, UNIQUE_PLACE)
    unique_edge = edge(record_at(unique_records, 10, "consume"))
    require(unique_edge["value_category"] == "WholePlace" and
            unique_edge["transfer_mode"] == "MoveOwned" and
            unique_edge["source_disposition"] == "InvalidateWhole",
            "cede ^local_source was classified as a temporary")

    probe_args = ("-I", str(PROBE.parent))
    _, _, probe_payload = require_parity(tokac, PROBE, probe_args)
    probe_records = source_records(probe_payload, PROBE)
    require(len(probe_records) == 1 and probe_records[0]["callee"] == "inner" and
            probe_payload["gate_exclusions"][
                "CandidateProbeOrSpeculativeContext"] >= 1,
            "candidate probe emitted a speculative inner receipt")

    _, _, generic_payload = require_parity(tokac, GENERIC_PROBE)
    require(not source_records(generic_payload, GENERIC_PROBE) and
            generic_payload["gate_exclusions"][
                "CandidateProbeOrSpeculativeContext"] >= 1,
            "generic deduction or instantiation emitted a speculative receipt")

    _, _, global_payload = require_parity(tokac, GLOBAL_PLACE)
    global_inspects = [record for record in source_records(
        global_payload, GLOBAL_PLACE) if record["callee"] == "inspect"]
    require(len(global_inspects) == 2 and
            all(record["admission"] == "NotInSlice" and
                record["reason"] == "NonLocalPlace" and
                record["transfer_edges"] == [] for record in global_inspects),
            "module-global binding was admitted as a whole local place")

    _, _, noncede_unique_payload = require_parity(tokac, NONCEDE_UNIQUE)
    noncede_unique_records = source_records(noncede_unique_payload,
                                             NONCEDE_UNIQUE)
    unique_borrow = record_at(noncede_unique_records, 9, "inspect_unique")
    require(unique_borrow["admission"] == "NotInSlice" and
            unique_borrow["reason"] == "UnsupportedTypeCategory" and
            unique_borrow["transfer_edges"] == [],
            "non-cede OwnedIdentity silently expanded the D.3a slice")

    async_normal, _, async_payload = require_parity(tokac, ASYNC_DANGLING)
    require(async_normal.returncode == 1 and "E0702" in async_normal.stderr,
            "async dangling fixture lost its legacy error")
    async_records = source_records(async_payload, ASYNC_DANGLING)
    fetch_records = [record for record in async_records
                     if record["callee"] == "fetch"]
    require(len(fetch_records) == 1 and
            fetch_records[0]["legacy_outcome"]["status"] == "rejected" and
            "E0702" in fetch_records[0]["legacy_outcome"]["diagnostic_codes"],
            "receipt was emitted before complete async legacy checking")

    borrow_normal, _, borrow_payload = require_parity(tokac,
                                                       CEDE_BORROW_CONFLICT)
    require(borrow_normal.returncode == 1,
            "borrow-conflict fixture unexpectedly passed")
    borrow_records = source_records(borrow_payload, CEDE_BORROW_CONFLICT)
    consume_records = [record for record in borrow_records
                       if record["callee"] == "consume"]
    require(len(consume_records) == 1 and
            consume_records[0]["admission"] == "Rejected" and
            consume_records[0]["transfer_edges"] == [],
            "non-spelling legacy rejection produced an admitted edge")

    _, _, dynamic_payload = require_parity(tokac, DYNAMIC)
    dynamic_records = source_records(dynamic_payload, DYNAMIC)
    require(not any(record["callee"] == "apply" for record in dynamic_records),
            "dynamic-trait method entered the D.3 factory")

    _, _, callable_payload = require_parity(tokac, CALLABLE)
    callable_records = source_records(callable_payload, CALLABLE)
    require(not any(record["callee"] in ("consumer", "invoke")
                    for record in callable_records),
            "callable/indirect route entered the D.3 factory")
    require(callable_payload["gate_exclusions"]["NonFinalSemanticTraversal"] >= 1,
            "closure precompute was not excluded")

    _, _, boundary_payload = require_parity(tokac, BOUNDARY)
    boundary_records = source_records(boundary_payload, BOUNDARY)
    require(not any(record["call_site"]["line"] == 9
                    for record in boundary_records),
            "cross-module imported call entered the D.3 factory")
    require(boundary_payload["gate_exclusions"]["NonSameLexical"] +
            boundary_payload["gate_exclusions"][
                "CandidateProbeOrSpeculativeContext"] >= 1,
            "cross-module/generic exclusion was not observed")

    for conflicting in ("--diagnostics-json", "--cede-obligations=json",
                        "--call-transfer-shadow=json"):
        result = run([str(tokac), FLAG, conflicting, "--check-only",
                      str(MATRIX)])
        require(result.returncode != 0 and result.stdout == "" and
                "cannot be combined" in result.stderr,
                f"D.3 output conflict with {conflicting} leaked stdout")

    pure_source = ROOT / "src/Sema/DirectCallObservation.cpp"
    pure_text = pure_source.read_text()
    forbidden = ("toka/Sema.h", "toka/AST.h", "DiagnosticEngine",
                 "SemanticEvidence", "PALChecker", "std::cout", "getenv(")
    require(not any(token in pure_text for token in forbidden),
            "pure D.3 factory TU gained a forbidden compiler/global dependency")

    print("D.3a direct-call observation tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"D.3a direct-call observation tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
