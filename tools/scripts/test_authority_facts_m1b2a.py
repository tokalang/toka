#!/usr/bin/env python3
"""Qualify the bounded M1b.2a authority-first semantic facts slice."""

import argparse
import json
import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"
FIXTURES = ROOT / "tests/semantics/authority_facts_m1b2a"
STABLE = ROOT / "tests/semantics/direct_call_observation_d3a/stable_place.tk"
ORDERED = FIXTURES / "ordered_siblings.tk"
SEPARATE = FIXTURES / "separate_roots.tk"
PLAIN = FIXTURES / "plain_cleanup.tk"
SLAB = FIXTURES / "slabid.tk"
STRING = FIXTURES / "string_cleanup.tk"
BYTES = FIXTURES / "bytes_cleanup.tk"
NESTED = FIXTURES / "nested.tk"
FAULT = FIXTURES / "fault_base.tk"
TASK = ROOT / "tests/pass/g09_async_cold_task_semantics.tk"
GLOBAL = ROOT / "tests/semantics/direct_call_observation_d3a/global_place.tk"
MATRIX = ROOT / "tests/semantics/direct_call_observation_d3a/matrix.tk"
CLOSURE = \
    ROOT / "tests/semantics/call_transfer_shadow_m1/closure_callable_replay.tk"
GENERIC = ROOT / "tests/semantics/direct_call_observation_d3a/generic_probe.tk"
SOURCE_HIDDEN = ROOT / "tests/semantics/pure_nominal_probe/source_hidden_consumer.tk"
PURE_NOMINAL_FIXTURES = ROOT / "tests/semantics/pure_nominal_probe"
FLAG = "--m1b-2a-authority-facts=json"
SHADOW = "--m1b-2a-shadow"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def exact_keys(value, expected, context):
    require(set(value) == set(expected),
            f"{context} keys changed: {sorted(value)}")


def validate_schema(payload):
    exact_keys(payload, {
        "schema", "version", "status", "revision_id", "record_count",
        "cleanup_class_count", "store_build_parent_unchanged",
        "store_publish_parent_unchanged", "store_build_differences",
        "store_publish_differences", "excluded_count_by_reason",
        "indeterminate_count_by_reason", "error_count_by_reason",
        "cleanup_classes", "receipts"}, "M1b.2a top-level receipt")
    for cleanup in payload["cleanup_classes"]:
        exact_keys(cleanup, {"type_id", "class", "reason", "source"},
                   "cleanup class")
    for receipt in payload["receipts"]:
        exact_keys(receipt, {
            "location", "full_expression_id", "observation_id", "result",
            "reason", "build_parent_unchanged", "publish_parent_unchanged",
            "revision_size_before", "revision_size_after",
            "build_differences", "publish_differences", "record"},
            "authority receipt")
        exact_keys(receipt["location"], {"file", "line", "column"},
                   "authority location")
        if receipt["record"] is not None:
            exact_keys(receipt["record"], {
                "full_expression_id", "observation_id", "phase", "place",
                "cleanup", "legacy_policy"}, "authority record")
            if receipt["record"]["place"] is not None:
                exact_keys(receipt["record"]["place"], {
                    "place_id", "symbol_witness", "declaration_id",
                    "owner_id", "state", "init_mask", "type_id"},
                    "authority place")
            exact_keys(receipt["record"]["cleanup"], {
                "kind", "reason", "cleanup_id", "init_mask"},
                "source cleanup")
            if receipt["record"]["legacy_policy"] is not None:
                exact_keys(receipt["record"]["legacy_policy"], {
                    "type_id", "soul", "category", "drop_fact",
                    "derived_requirement"}, "legacy policy")


def audit(tokac, source, extra=()):
    result = run([str(tokac), FLAG, "--check-only", *extra, str(source)])
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{source} emitted invalid M1b.2a JSON") from error
    require(payload["schema"] == "toka.internal.m1b-2a-authority-facts" and
            payload["version"] == 1 and payload["status"] == "authority-only",
            "M1b.2a schema/version/status changed")
    validate_schema(payload)
    require(payload["store_build_parent_unchanged"] is True and
            payload["store_publish_parent_unchanged"] is True and
            payload["store_build_differences"] == [] and
            payload["store_publish_differences"] == [],
            "CleanupClassStore build/publication changed semantic parent")
    return result, payload


def public_parity(tokac, source):
    normal = run([str(tokac), "--check-only", str(source)])
    observed, payload = audit(tokac, source)
    require((normal.returncode, normal.stderr) ==
            (observed.returncode, observed.stderr),
            f"{source} authority mode changed public behavior")
    require(normal.stdout == "", f"{source} normal check wrote stdout")
    return normal, observed, payload


def source_receipts(payload, source):
    return [receipt for receipt in payload["receipts"]
            if receipt["location"]["file"].endswith(source.name)]


def admitted(receipts):
    return [receipt for receipt in receipts if receipt["result"] == "Admitted"]


def require_clean_receipt(receipt):
    require(receipt["build_parent_unchanged"] is True and
            receipt["publish_parent_unchanged"] is True and
            receipt["build_differences"] == [] and
            receipt["publish_differences"] == [],
            "authority fact build/publication changed semantic parent")


def cleanup_class(payload, name):
    identity_component = f";{len(name)}:{name};"
    matches = [entry for entry in payload["cleanup_classes"]
               if identity_component in entry["type_id"]]
    require(len(matches) == 1, f"missing/duplicate cleanup class for {name}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    _, first, stable_payload = public_parity(tokac, STABLE)
    second, repeated = audit(tokac, STABLE)
    require((first.returncode, first.stdout, first.stderr, stable_payload) ==
            (second.returncode, second.stdout, second.stderr, repeated),
            "M1b.2a receipt is not deterministic")
    stable = admitted(source_receipts(stable_payload, STABLE))
    require(len(stable) == 2, "stable sibling fixture missed observations")
    for receipt in stable:
        require_clean_receipt(receipt)
    stable_records = [receipt["record"] for receipt in stable]
    require(len({record["full_expression_id"] for record in stable_records}) == 1 and
            len({record["observation_id"] for record in stable_records}) == 2 and
            len({record["place"]["place_id"] for record in stable_records}) == 1 and
            all(record["cleanup"]["kind"] == "ArmedWholePlace"
                for record in stable_records),
            "stable sibling full-expression/place identity is incorrect")

    ordered_normal, _, ordered_payload = public_parity(tokac, ORDERED)
    require(ordered_normal.returncode != 0 and "E0438" in ordered_normal.stderr,
            "ordered sibling fixture lost the legacy moved-value result")
    ordered = admitted(source_receipts(ordered_payload, ORDERED))
    require(len(ordered) == 2, "ordered sibling fixture missed observations")
    ordered_records = [receipt["record"] for receipt in ordered]
    require([record["place"]["state"] for record in ordered_records] ==
            ["Live", "Moved"] and
            [record["cleanup"]["kind"] for record in ordered_records] ==
            ["ArmedWholePlace", "Indeterminate"] and
            len({record["full_expression_id"] for record in ordered_records}) == 1 and
            len({record["observation_id"] for record in ordered_records}) == 2 and
            len({record["place"]["place_id"] for record in ordered_records}) == 1,
            "ordered pre-evaluation snapshots are not sequence-sensitive")

    _, _, separate_payload = public_parity(tokac, SEPARATE)
    separate = admitted(source_receipts(separate_payload, SEPARATE))
    require(len(separate) == 2 and
            len({r["record"]["full_expression_id"] for r in separate}) == 2 and
            len({r["record"]["place"]["place_id"] for r in separate}) == 1,
            "separate roots did not preserve full-expression/place identity")

    _, _, nested_payload = public_parity(tokac, NESTED)
    nested = source_receipts(nested_payload, NESTED)
    require(len(nested) == 2 and
            len({r["full_expression_id"] for r in nested}) == 1 and
            len({r["observation_id"] for r in nested}) == 2 and
            {r["result"] for r in nested} == {"Admitted", "NotInSlice"},
            "nested observations lost their shared root/distinct identity")

    for fixture, cleanup_kind, requirement in (
            (PLAIN, "NoCleanup", "ImplicitExempt"),
            (SLAB, "ArmedWholePlace", "ImplicitExempt"),
            (STRING, "ArmedWholePlace", "ExplicitRequired"),
            (BYTES, "ArmedWholePlace", "ExplicitRequired")):
        _, _, payload = public_parity(tokac, fixture)
        records = [r["record"] for r in admitted(source_receipts(payload, fixture))]
        require(len(records) == 1 and
                records[0]["cleanup"]["kind"] == cleanup_kind and
                records[0]["legacy_policy"]["derived_requirement"] == requirement,
                f"{fixture} cleanup/legacy authority is incorrect")

    _, task_payload = audit(tokac, TASK)
    require(cleanup_class(task_payload, "TimerHeap") == {
                "type_id": cleanup_class(task_payload, "TimerHeap")["type_id"],
                "class": "OwnedWholeCleanup", "reason": "None",
                "source": "StructuralOwnedField"},
            "TimerHeap was not classified through structural Vec cleanup")
    require(cleanup_class(stable_payload, "Resource")["class"] ==
            "OwnedWholeCleanup", "explicit-drop Resource lost cleanup class")
    require(cleanup_class(stable_payload, "string")["class"] ==
            "OwnedWholeCleanup", "builtin string lost cleanup class")

    exclusion_expectations = (
        (GLOBAL, "GlobalOrSourceHiddenBinding"),
        (MATRIX, "PlaceAliasOrProjection"),
        (MATRIX, "TemporaryOrMissingBinding"),
        (CLOSURE, "CapturedOrGeneratedBinding"),
        (GENERIC, "NonFinalOrSpeculativeTraversal"),
    )
    exclusion_payloads = {}
    for source, reason in exclusion_expectations:
        if source not in exclusion_payloads:
            _, exclusion_payloads[source] = audit(tokac, source)
        require(exclusion_payloads[source]["excluded_count_by_reason"]
                [reason] >= 1,
                f"M1b.2a exclusion {reason} lacks a real fixture")
    _, source_hidden_payload = audit(
        tokac, SOURCE_HIDDEN, ("-I", str(PURE_NOMINAL_FIXTURES)))
    require(source_hidden_payload["excluded_count_by_reason"]
            ["GlobalOrSourceHiddenBinding"] >= 1,
            "M1b.2a source-hidden binding did not fail closed")

    d3 = run([str(tokac), "--m1b-d3-direct-call-observation=json",
              "--check-only", str(STABLE)])
    d3_shadow = run([str(tokac), "--m1b-d3-direct-call-observation=json",
                     SHADOW, "--check-only", str(STABLE)])
    require((d3.returncode, d3.stdout, d3.stderr) ==
            (d3_shadow.returncode, d3_shadow.stdout, d3_shadow.stderr),
            "M1b.2a changed D.3a")
    evidence = run([str(tokac), "--cede-obligations=json", "--check-only",
                    str(STABLE)])
    evidence_shadow = run([str(tokac), "--cede-obligations=json", SHADOW,
                           "--check-only", str(STABLE)])
    require((evidence.returncode, evidence.stdout, evidence.stderr) ==
            (evidence_shadow.returncode, evidence_shadow.stdout,
             evidence_shadow.stderr), "M1b.2a changed Evidence v1")
    ordinary_codegen = run([str(tokac), str(STABLE)])
    # CodeGen still uses argv[1] as its module label, so keep the source first
    # when exercising a no-output internal shadow flag.
    shadow_codegen = run([str(tokac), str(STABLE), SHADOW])
    require((ordinary_codegen.returncode, ordinary_codegen.stdout,
             ordinary_codegen.stderr) ==
            (shadow_codegen.returncode, shadow_codegen.stdout,
             shadow_codegen.stderr), "M1b.2a changed ordinary CodeGen output")

    fault_points = (
        "AfterFullExpressionIdentity", "AfterObservationIdentity",
        "AfterPlaceReverseLookup", "AfterCleanupClassLookup",
        "AfterCleanupFactBuild", "BeforeRevisionValidation", "BeforeSwap")
    for point in fault_points:
        result = run([str(tokac), FLAG, "--m1b-2a-inject-fault=" + point,
                      "--m1b-2a-fault-source", FAULT.name, "--check-only",
                      str(FAULT)])
        payload = json.loads(result.stdout)
        require(result.returncode != 0 and
                result.stderr.count("error[") == 1 and
                "error[E0406]" in result.stderr and
                payload["store_build_parent_unchanged"] is True and
                payload["store_publish_parent_unchanged"] is True,
                f"fault point {point} changed parent or published a record")
        failures = [r for r in payload["receipts"] if r["result"] == "Error"]
        require(len(failures) == 1 and
                failures[0]["revision_size_before"] ==
                failures[0]["revision_size_after"] and
                failures[0]["build_parent_unchanged"] is True and
                failures[0]["publish_parent_unchanged"] is True,
                f"fault point {point} changed the prior revision")

    for conflict in ("--diagnostics-json",
                     "--m1b-d3-direct-call-observation=json"):
        result = run([str(tokac), FLAG, conflict, "--check-only", str(STABLE)])
        require(result.returncode != 0 and result.stdout == "" and
                "cannot be combined" in result.stderr,
                f"authority output conflict with {conflict} leaked stdout")

    pure_text = (ROOT / "src/Sema/AuthorityFacts.cpp").read_text()
    forbidden = ("toka/Sema.h", "DiagnosticEngine", "SemanticEvidence",
                 "PALChecker", "CodeGen", "getenv(", "std::cout")
    require(not any(token in pure_text for token in forbidden),
            "authority pure core gained a forbidden dependency")

    print("M1b.2a authority facts tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"M1b.2a authority facts tests FAILED: {error}", file=sys.stderr)
        sys.exit(1)
