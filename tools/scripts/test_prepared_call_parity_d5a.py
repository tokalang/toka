#!/usr/bin/env python3
"""Qualify the bounded D.5a pre-legacy prepared-call parity slice."""

import argparse
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
MATRIX = ROOT / "tests/semantics/direct_call_observation_d3a/matrix.tk"
ORDER_A = ROOT / "tests/semantics/direct_call_observation_d3a/copy_order_a.tk"
ORDER_B = ROOT / "tests/semantics/direct_call_observation_d3a/copy_order_b.tk"
FIXTURES = ROOT / "tests/semantics/prepared_call_parity_d5a"
SLAB = FIXTURES / "slabid.tk"
MANAGED = FIXTURES / "managed_no_drop.tk"
FAULT_BASE = FIXTURES / "fault_base.tk"
FLAG = "--m1b-d5a-prepared-call-parity=json"
SHADOW = "--m1b-d5a-shadow"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def exact_keys(value, expected, context):
    require(set(value) == set(expected),
            f"{context} keys changed: {sorted(value)}")


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def audit(tokac, source):
    result = run([str(tokac), FLAG, "--check-only", str(source)])
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{source} emitted invalid D.5a JSON") from error
    require(payload["schema"] ==
            "toka.internal.m1b-d5a-prepared-call-parity" and
            payload["version"] == 1 and payload["status"] == "shadow-only",
            "D.5a schema/version/status changed")
    exact_keys(payload, {
        "schema", "version", "status", "considered_call_count",
        "pre_factory_invocation_count", "post_oracle_invocation_count",
        "prepared_count", "gate_excluded_count_by_reason",
        "excluded_count_by_reason", "rejected_count_by_reason",
        "parity_failure_count_by_reason",
        "infrastructure_error_count_by_reason", "receipts"}, "D.5a receipt")
    receipt_keys = {
        "call_site", "callee", "formal_identity", "source_identity",
        "actual_type", "formal_type", "source_state_before",
        "pal_state_before", "source_init_mask", "dependency_bearing_actual",
        "legacy_cede_requirement", "pre_admission",
        "post_admission", "pre_reason", "parity_error",
        "infrastructure_error", "pre_plan",
        "post_plan", "authority_projection", "legacy_diagnostic_codes",
        "final_legacy_check_count", "same_call_structural_parity",
        "pre_factory_parent_unchanged", "post_factory_parent_unchanged",
        "pre_differing_parent_fields", "post_differing_parent_fields"}
    plan_keys = {"type_proof", "transfer", "source", "boundary_access",
                 "dependency", "spelling", "liability_source",
                 "liability_target",
                 "evaluation_entries", "boundary_entries",
                 "finalization_entries", "normalized_boundary_entries",
                 "normalized_finalization_entries", "patch_payloads",
                 "region_terminal"}
    for receipt in payload["receipts"]:
        exact_keys(receipt, receipt_keys, "D.5a call receipt")
        exact_keys(receipt["call_site"], {"file", "line", "column"},
                   "D.5a call site")
        for field in ("pre_plan", "post_plan", "authority_projection"):
            if receipt[field] is not None:
                exact_keys(receipt[field], plan_keys, f"D.5a {field}")
    return result, payload


def source_receipts(payload, source, callee=None):
    receipts = []
    for receipt in payload["receipts"]:
        if not receipt["call_site"]["file"].endswith(source.name):
            continue
        if callee is not None and receipt["callee"] != callee:
            continue
        receipts.append(receipt)
    return receipts


def require_public_parity(tokac, source):
    normal = run([str(tokac), "--check-only", str(source)])
    observed, payload = audit(tokac, source)
    require((normal.returncode, normal.stderr) ==
            (observed.returncode, observed.stderr),
            f"{source} D.5a changed public diagnostics or exit")
    require(normal.stdout == "", f"{source} normal check wrote stdout")
    return normal, observed, payload


def allowed_difference(left, right, allowed):
    keys = set(left) | set(right)
    differences = {key for key in keys if left.get(key) != right.get(key)}
    return differences == set(allowed), differences


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    matrix_normal, _, matrix_payload = require_public_parity(tokac, MATRIX)
    repeated_result, repeated_payload = audit(tokac, MATRIX)
    first_result, _ = audit(tokac, MATRIX)
    require((repeated_result.returncode, repeated_result.stdout,
             repeated_result.stderr, repeated_payload) ==
            (first_result.returncode, first_result.stdout,
             first_result.stderr, json.loads(first_result.stdout)),
            "D.5a receipt is not deterministic")
    expected = {
        35: ("inspect", None, "BorrowCapture", "KeepLive", "SharedBorrow",
             "implicit", []),
        38: ("consume", "ExplicitRequired", "MoveOwned", "InvalidateWhole",
             "Invalidation", "explicit", []),
        41: ("consume", "ExplicitRequired", "MoveOwned", "InvalidateWhole",
             "Invalidation", "implicit", ["E04570"]),
        44: ("consume_pair", "ImplicitExempt", "CopyValue", "KeepLive",
             "SharedBorrow", "implicit", []),
        47: ("consume_pair", "ImplicitExempt", "CopyValue",
             "InvalidateWhole", "Invalidation", "explicit", []),
    }
    admitted = {}
    for receipt in source_receipts(matrix_payload, MATRIX):
        line = receipt["call_site"]["line"]
        if line not in expected:
            continue
        admitted[line] = receipt
        callee, policy, transfer, source, boundary, spelling, diagnostics = \
            expected[line]
        plan = receipt["pre_plan"]
        require(receipt["callee"] == callee and
                receipt["legacy_cede_requirement"] == policy and
                receipt["pre_admission"] == "Admitted" and
                receipt["pre_reason"] is None and
                receipt["parity_error"] is None and
                receipt["same_call_structural_parity"] is True and
                receipt["final_legacy_check_count"] == 1 and
                receipt["pre_factory_parent_unchanged"] is True and
                receipt["post_factory_parent_unchanged"] is True and
                receipt["pre_differing_parent_fields"] == [] and
                receipt["post_differing_parent_fields"] == [] and
                receipt["legacy_diagnostic_codes"] == diagnostics and
                plan == receipt["post_plan"] and
                plan["transfer"] == transfer and plan["source"] == source and
                plan["boundary_access"] == boundary and
                plan["spelling"] == spelling and
                plan["evaluation_entries"] == 0,
                f"D.5a admitted row {line} is incomplete")
    require(set(admitted) == set(expected), "D.5a five-row matrix incomplete")

    _, order_a = audit(tokac, ORDER_A)
    _, order_b = audit(tokac, ORDER_B)
    order_a_pair = source_receipts(order_a, ORDER_A, "consume_pair")
    order_b_pair = source_receipts(order_b, ORDER_B, "consume_pair")
    require(len(order_a_pair) == len(order_b_pair) == 1 and
            order_a_pair[0]["authority_projection"] ==
            order_b_pair[0]["authority_projection"],
            "D.5a plan changed with fixture scheduling order")

    ok, differences = allowed_difference(
        admitted[38]["authority_projection"],
        admitted[41]["authority_projection"], {"spelling"})
    require(ok, f"non-Copy bare/explicit differences changed: {differences}")
    ok, differences = allowed_difference(
        admitted[44]["authority_projection"],
        admitted[47]["authority_projection"],
        {"spelling", "source", "boundary_access", "boundary_entries",
         "normalized_boundary_entries", "patch_payloads"})
    require(ok, f"Copy bare/explicit differences changed: {differences}")

    for fixture, callee in ((SLAB, "consume_id"),
                            (MANAGED, "consume_managed")):
        normal, _, payload = require_public_parity(tokac, fixture)
        receipts = source_receipts(payload, fixture, callee)
        require(normal.returncode == 0 and "E04570" not in normal.stderr and
                len(receipts) == 1 and
                receipts[0]["legacy_cede_requirement"] == "ImplicitExempt" and
                receipts[0]["pre_admission"] == "NotInSlice" and
                receipts[0]["pre_reason"] ==
                    "CededNonCopyLegacyExempt" and
                receipts[0]["pre_plan"] is None and
                receipts[0]["post_plan"] is None and
                receipts[0]["final_legacy_check_count"] == 1 and
                receipts[0]["pre_factory_parent_unchanged"] is True and
                receipts[0]["post_factory_parent_unchanged"] is True,
                f"{fixture} did not preserve the RC8 cede exemption")

    infrastructure_errors = (
        "InvalidCallSiteIdentity",
        "InvalidCalleeIdentity",
        "InvalidFormalOrDestinationIdentity",
        "InvalidSourcePlaceIdentity",
        "ConflictingPatchPayload",
        "MalformedPreparedResult",
    )
    for injected in infrastructure_errors:
        result = run([str(tokac), FLAG,
                      "--m1b-d5a-inject-error=" + injected,
                      "--check-only", str(FAULT_BASE)])
        payload = json.loads(result.stdout)
        failures = [receipt for receipt in payload["receipts"]
                    if receipt["infrastructure_error"] == injected]
        require(result.returncode != 0 and
                result.stderr.count("error[") == 1 and
                "error[E0406]" in result.stderr and
                payload["infrastructure_error_count_by_reason"][injected] == 1 and
                len(failures) == 1 and
                failures[0]["pre_plan"] is None and
                failures[0]["post_plan"] is None and
                failures[0]["final_legacy_check_count"] == 0 and
                failures[0]["pre_factory_parent_unchanged"] is True and
                failures[0]["post_factory_parent_unchanged"] is True,
                f"{injected} did not terminate D.5a atomically")

    parity_errors = (
        "PrePostFactMismatch",
        "PreparedPlanMismatch",
        "LegacyOutcomeMismatch",
        "LegacyCheckCountMismatch",
        "NonEmptyEvaluationDelta",
    )
    for injected in parity_errors:
        result = run([str(tokac), FLAG,
                      "--m1b-d5a-inject-parity=" + injected,
                      "--check-only", str(FAULT_BASE)])
        payload = json.loads(result.stdout)
        require(result.returncode != 0 and
                result.stderr.count("error[") == 1 and
                "error[E0406]" in result.stderr and
                payload["parity_failure_count_by_reason"][injected] == 1 and
                len([receipt for receipt in payload["receipts"]
                     if receipt["parity_error"] == injected]) == 1,
                f"{injected} did not fail D.5a qualification")

    d3 = run([str(tokac), "--m1b-d3-direct-call-observation=json",
              "--check-only", str(MATRIX)])
    d3_shadow = run([str(tokac), "--m1b-d3-direct-call-observation=json",
                     SHADOW, "--check-only", str(MATRIX)])
    require((d3.returncode, d3.stdout, d3.stderr) ==
            (d3_shadow.returncode, d3_shadow.stdout, d3_shadow.stderr),
            "D.5a changed the qualified D.3a receipt")

    evidence = run([str(tokac), "--cede-obligations=json", "--check-only",
                    str(MATRIX)])
    evidence_shadow = run([str(tokac), "--cede-obligations=json", SHADOW,
                           "--check-only", str(MATRIX)])
    require((evidence.returncode, evidence.stdout, evidence.stderr) ==
            (evidence_shadow.returncode, evidence_shadow.stdout,
             evidence_shadow.stderr), "D.5a changed Evidence v1")

    for conflict in ("--diagnostics-json",
                     "--m1b-d3-direct-call-observation=json"):
        result = run([str(tokac), FLAG, conflict, "--check-only", str(MATRIX)])
        require(result.returncode != 0 and result.stdout == "" and
                "cannot be combined" in result.stderr,
                f"D.5a output conflict with {conflict} leaked stdout")

    sema_text = (ROOT / "src/Sema/Sema.cpp").read_text()
    helper_text = sema_text[
        sema_text.index("bool Sema::canImplicitlyPassToCede"):]
    helper_text = helper_text[:helper_text.index("bool Sema::isStartBoundaryScalar")]
    require('resolved == "SlabID"' not in helper_text and
            'resolved == "TimerHeap"' not in helper_text,
            "legacy helper regained a duplicate named-policy table")
    pure_text = (ROOT / "src/Sema/DirectCallObservation.cpp").read_text()
    forbidden = ("toka/Sema.h", "DiagnosticEngine", "SemanticEvidence",
                 "PALChecker", "getenv(", "std::cout", "CodeGen")
    require(not any(token in pure_text for token in forbidden),
            "D.5a pure core gained a forbidden dependency")

    require(matrix_normal.returncode != 0,
            "D.5a matrix unexpectedly lost legacy E04570")
    print("D.5a prepared-call parity tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"D.5a prepared-call parity tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
