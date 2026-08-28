#!/usr/bin/env python3
"""Qualify the D.4a pure direct-nominal overload query."""

import argparse
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/pure_nominal_probe"
UNIQUE = FIXTURES / "unique_consumer.tk"
MULTIPLE = FIXTURES / "multiple_consumer.tk"
ZERO = FIXTURES / "zero_consumer.tk"
PRIMITIVE = FIXTURES / "primitive_consumer.tk"
VARIANT = FIXTURES / "variant_consumer.tk"
ALIAS = FIXTURES / "alias_consumer.tk"
ACTUAL_ALIAS = FIXTURES / "actual_alias_consumer.tk"
EXCLUSIONS = FIXTURES / "exclusion_consumer.tk"
NON_SOURCE_BACKED = FIXTURES / "non_source_backed_consumer.tk"
SOURCE_HIDDEN = FIXTURES / "source_hidden_consumer.tk"
SCHEMA = FIXTURES / "receipt_schema_v1.json"
FLAG = "--m1b-d4a-pure-nominal-overload-probe=json"
FORCE = "--m1b-d4a-force-legacy"
REVERSE = "--m1b-d4a-reverse-schedule"
INJECT_ERROR = "--m1b-d4a-inject-error="


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def exact_keys(value, expected, context):
    require(set(value) == set(expected),
            f"{context} keys changed: {sorted(value)}")


def validate_schema(payload):
    schema = json.loads(SCHEMA.read_text())
    exact_keys(payload, schema["top_level"], "top-level D.4a receipt")
    exact_keys(payload["excluded_count_by_reason"],
               schema["exclusion_reasons"], "exclusion counts")
    exact_keys(payload["legacy_required_count_by_reason"],
               schema["legacy_reasons"], "legacy counts")
    exact_keys(payload["infrastructure_error_count_by_reason"],
               schema["infrastructure_errors"], "infrastructure counts")
    for batch in payload["batches"]:
        exact_keys(batch, schema["batch"], "batch")
        exact_keys(batch["call_site"], schema["location"], "call site")
        for candidate in batch["candidates"]:
            exact_keys(candidate, schema["candidate"], "candidate")


def audit(tokac, source, force=False, reverse=False, extra=()):
    command = [str(tokac), FLAG, "--check-only"]
    if force:
        command.append(FORCE)
    if reverse:
        command.append(REVERSE)
    command.extend(extra)
    command.append(str(source))
    result = run(command)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{source} emitted invalid D.4a JSON") from error
    require(payload["schema"] ==
            "toka.internal.m1b-d4a-pure-nominal-overload-probe" and
            payload["version"] == 1, "D.4a schema/version changed")
    validate_schema(payload)
    return result, payload


def source_batches(payload, source, callee=None):
    result = []
    for batch in payload["batches"]:
        if not batch["call_site"]["file"].endswith(source.name):
            continue
        if callee is not None and batch["callee"] != callee:
            continue
        result.append(batch)
    return result


def require_parity(tokac, source, extra=()):
    normal = run([str(tokac), "--check-only", *extra, str(source)])
    observed, payload = audit(tokac, source, extra=extra)
    require(normal.returncode == observed.returncode,
            f"{source} pure audit changed exit status")
    require(normal.stderr == observed.stderr,
            f"{source} pure audit changed diagnostics")
    require(normal.stdout == "", f"{source} normal check wrote stdout")
    return normal, observed, payload


def require_exclusion(tokac, source, callee, reason, extra):
    _, _, payload = require_parity(tokac, source, extra)
    batches = source_batches(payload, source, callee)
    require(len(batches) == 1,
            f"{source}:{callee} did not produce one exclusion receipt")
    batch = batches[0]
    require(batch["exclusion"] == reason and batch["candidates"] == [] and
            batch["disposition"] is None and
            batch["parent_unchanged"] is True and
            batch["differing_parent_fields"] == [] and
            payload["pure_batch_count"] == 0 and
            payload["excluded_count_by_reason"][reason] >= 1,
            f"{source}:{callee} did not close {reason}")
    return payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")
    include = ("-I", str(FIXTURES))

    _, first, unique_payload = require_parity(tokac, UNIQUE, include)
    second, second_payload = audit(tokac, UNIQUE, extra=include)
    require(first.stdout == second.stdout and unique_payload == second_payload,
            "D.4a receipt is not deterministic")
    unique_batches = source_batches(unique_payload, UNIQUE, "choose")
    require(len(unique_batches) == 1, "unique fixture missed its pure batch")
    unique = unique_batches[0]
    require(unique["disposition"] == "UniqueCompatible" and
            unique["selected_declaration_id"] is not None and
            unique["legacy_reason"] is None and
            unique["candidate_diagnostic_count"] == 0 and
            unique["final_legacy_check_count"] == 1 and
            unique["parent_unchanged"] is True and
            unique["differing_parent_fields"] == [],
            "unique pure batch facts are incomplete")
    require([candidate["legacy_ordinal"] for candidate in
             unique["candidates"]] == [0, 1, 2],
            "legacy ordinals do not reproduce resolver order")

    _, reverse_payload = audit(tokac, UNIQUE, reverse=True, extra=include)
    reverse = source_batches(reverse_payload, UNIQUE, "choose")[0]
    require(reverse["selected_declaration_id"] ==
            unique["selected_declaration_id"] and
            reverse["candidates"] == unique["candidates"] and
            reverse["parent_unchanged"] is True,
            "reverse scheduling changed pure selection")

    forced_result, forced_payload = audit(tokac, UNIQUE, force=True,
                                          extra=include)
    require(forced_result.returncode == first.returncode and
            forced_result.stderr == first.stderr,
            "forced legacy route changed public behavior")
    forced = source_batches(forced_payload, UNIQUE, "choose")[0]
    require(forced["selected_declaration_id"] ==
            forced["forced_legacy_selected_declaration_id"] and
            forced["candidate_diagnostic_count"] == 0 and
            forced["final_legacy_check_count"] == 1,
            "pure and forced-legacy selection diverged")

    _, _, multiple_payload = require_parity(tokac, MULTIPLE, include)
    multiple = source_batches(multiple_payload, MULTIPLE, "choose")[0]
    require(multiple["disposition"] == "LegacyRequired" and
            multiple["legacy_reason"] == "MultipleCompatible" and
            multiple["forced_legacy_selected_declaration_id"] is not None and
            multiple["final_legacy_check_count"] == 1,
            "multiple-compatible fallback is not closed")

    zero_normal, _, zero_payload = require_parity(tokac, ZERO, include)
    require(zero_normal.returncode == 1, "zero-compatible fixture passed")
    zero = source_batches(zero_payload, ZERO, "choose")[0]
    require(zero["legacy_reason"] == "ZeroCompatible" and
            zero["forced_legacy_selected_declaration_id"] is not None and
            zero["final_legacy_check_count"] == 1,
            "zero-compatible fallback is not closed")

    exclusion_cases = (
        (EXCLUSIONS, "local_probe", "WrongRoute"),
        (NON_SOURCE_BACKED, "non_source_probe", "NonSourceBacked"),
        (EXCLUSIONS, "single_probe", "NotOverloaded"),
        (EXCLUSIONS, "arity_probe", "ArityOrDefault"),
        (EXCLUSIONS, "nonlocal_probe", "NonLocalOrNonLivePlace"),
        (EXCLUSIONS, "actual_non_nominal_probe",
         "NonDirectNominalActual"),
        (EXCLUSIONS, "formal_non_nominal_probe",
         "NonDirectNominalFormal"),
        (VARIANT, "choose", "VariantOrRefinedNominal"),
        (PRIMITIVE, "choose", "PrimitiveOrAlias"),
        (EXCLUSIONS, "attribute_probe", "AttributesOrConversion"),
        (EXCLUSIONS, "generic_probe", "GenericOrContextual"),
        (EXCLUSIONS, "handle_probe", "HandleOrPermission"),
        (EXCLUSIONS, "contract_probe", "ContractUnsupported"),
        (EXCLUSIONS, "dependency_probe", "PALOrDependencyConflict"),
        (SOURCE_HIDDEN, "source_hidden_probe", "SourceHiddenOrIncomplete"),
    )
    exclusion_payloads = {}
    for source, callee, reason in exclusion_cases:
        key = source.resolve()
        if key not in exclusion_payloads:
            _, _, exclusion_payloads[key] = require_parity(
                tokac, source, include)
        payload = exclusion_payloads[key]
        batches = source_batches(payload, source, callee)
        require(len(batches) == 1 and
                batches[0]["exclusion"] == reason and
                batches[0]["candidates"] == [] and
                batches[0]["disposition"] is None and
                batches[0]["parent_unchanged"] is True and
                batches[0]["differing_parent_fields"] == [] and
                payload["pure_batch_count"] == 0 and
                payload["excluded_count_by_reason"][reason] >= 1,
                f"{source}:{callee} did not close {reason}")

    require_exclusion(tokac, ALIAS, "choose", "PrimitiveOrAlias", include)
    require_exclusion(tokac, ACTUAL_ALIAS, "choose_actual_alias",
                      "PrimitiveOrAlias", include)

    infrastructure_errors = (
        "InvalidCallSiteIdentity",
        "InvalidNominalShapeId",
        "DuplicateCandidateIdentity",
        "DuplicateLegacyOrdinal",
        "NonContiguousLegacyOrdinal",
        "MalformedBatch",
    )
    for injected in infrastructure_errors:
        result, payload = audit(
            tokac, UNIQUE, extra=(*include, INJECT_ERROR + injected))
        failures = source_batches(payload, UNIQUE, "choose")
        require(result.returncode != 0 and
                "internal D.4a" in result.stderr and
                payload["pure_batch_count"] == 0 and
                payload["infrastructure_error_count"] == 1 and
                payload["infrastructure_error_count_by_reason"][injected] == 1 and
                len(failures) == 1 and
                failures[0]["disposition"] is None and
                failures[0]["parent_unchanged"] is True and
                failures[0]["differing_parent_fields"] == [],
                f"{injected} did not fail closed with preserved parent state")

    d3 = run([str(tokac), "--m1b-d3-direct-call-observation=json",
              "--check-only", *include, str(UNIQUE)])
    d3_forced = run([str(tokac), "--m1b-d3-direct-call-observation=json",
                     FORCE, "--check-only", *include, str(UNIQUE)])
    require((d3.returncode, d3.stdout, d3.stderr) ==
            (d3_forced.returncode, d3_forced.stdout, d3_forced.stderr),
            "D.4a changed the qualified D.3a receipt")

    evidence = run([str(tokac), "--cede-obligations=json", "--check-only",
                    *include, str(UNIQUE)])
    evidence_forced = run([str(tokac), "--cede-obligations=json", FORCE,
                           "--check-only", *include, str(UNIQUE)])
    require((evidence.returncode, evidence.stdout, evidence.stderr) ==
            (evidence_forced.returncode, evidence_forced.stdout,
             evidence_forced.stderr),
            "D.4a changed Evidence v1")

    for conflict in ("--diagnostics-json",
                     "--m1b-d3-direct-call-observation=json"):
        result = run([str(tokac), FLAG, conflict, "--check-only",
                      *include, str(UNIQUE)])
        require(result.returncode != 0 and result.stdout == "" and
                "cannot be combined" in result.stderr,
                f"D.4a output conflict with {conflict} leaked stdout")

    pure_source = ROOT / "src/Sema/PureNominalOverloadProbe.cpp"
    pure_text = pure_source.read_text()
    forbidden = ("toka/Sema.h", "toka/AST.h", "DiagnosticEngine",
                 "SemanticEvidence", "PALChecker", "std::cout", "getenv(",
                 "CodeGen", "SemanticModelPatch")
    require(not any(token in pure_text for token in forbidden),
            "pure D.4a TU gained a forbidden dependency")

    print("D.4a pure nominal overload probe tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"D.4a pure nominal overload probe tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
