#!/usr/bin/env python3

"""Qualify the audit-only RC9 M1 call-transfer shadow planner."""

import argparse
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(tokac, source, expected_error=None):
    process = subprocess.run(
        [str(tokac), "--call-transfer-shadow=json", "--check-only", source],
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
    require(payload.get("version") == 1, source + " emitted the wrong version")
    require(payload.get("status") == "audit-only",
            source + " did not identify audit-only output")
    records = payload.get("records")
    require(isinstance(records, list), source + " omitted records")
    required = {
        "callee", "route", "parameter", "argument_index", "spelling",
        "transfer", "source", "dependency", "place_eligibility",
        "source_path", "formal_ceded", "legacy_cede_exempt",
        "legacy_missing_cede", "async", "start_boundary", "location",
        "contract_location",
    }
    seen = set()
    for record in records:
        require(set(record) == required,
                source + " shadow record fields changed")
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":"))
        require(encoded not in seen, source + " emitted duplicate records")
        seen.add(encoded)
    return records


def find(records, source_file, **expected):
    matches = []
    for record in records:
        location = record.get("location", {}).get("file", "")
        if not location.endswith(source_file):
            continue
        if all(record.get(key) == value for key, value in expected.items()):
            matches.append(record)
    require(len(matches) == 1,
            "%s expected one %s record, found %d"
            % (source_file, json.dumps(expected, sort_keys=True), len(matches)))
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
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="CopyValue",
        source="KeepLive", source_path="plain", formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    ))
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="ConsumeTemporary",
        source="NoSourcePlace", source_path="", formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    ))

    source = "tests/semantics/call_transfer_shadow_m1/copy_explicit_invalidates.tk"
    records = run(tokac, source, expected_error="E0438")
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="explicit", transfer="CopyValue",
        source="InvalidatePlace", source_path="plain", formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
        place_eligibility="PendingValidation",
    ))

    source = "tests/conformance/diagnostics/static_cede_parameter_requires_explicit_transfer.tk"
    records = run(tokac, source, expected_error="E04570")
    receipts.append(find(
        records, source, callee="Token::consume", route="static",
        parameter="token", spelling="implicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="source", formal_ceded=True,
        legacy_cede_exempt=False, legacy_missing_cede=True,
        place_eligibility="PendingValidation",
    ))

    source = "tests/conformance/diagnostics/callable_cede_parameter_requires_explicit_transfer.tk"
    records = run(tokac, source, expected_error="E04570")
    receipts.append(find(
        records, source, callee="consumer", route="callable",
        parameter="token", spelling="implicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="source", formal_ceded=True,
        legacy_cede_exempt=False, legacy_missing_cede=True,
    ))

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
    records = run(tokac, source, expected_error="E04570")
    receipts.append(find(
        records, source, callee="forward_parcel", route="ordinary",
        parameter="parcel", spelling="implicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="parcel", formal_ceded=True,
        legacy_cede_exempt=False, legacy_missing_cede=True,
    ))

    source = "tests/semantics/tki_replay/cases/async_start_001_cede_handoff/pass_start_cede.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume_async", route="ordinary",
        parameter="payload", spelling="explicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="payload", formal_ceded=True,
        start_boundary=True, **{"async": True},
    ))

    source = "tests/conformance/diagnostics/cede_argument_to_borrowed_parameter_rejected.tk"
    records = run(tokac, source, expected_error="E04640")
    receipts.append(find(
        records, source, callee="borrow", route="ordinary",
        parameter="value", spelling="explicit", transfer="Reject",
        source="NoStateChange", source_path="value", formal_ceded=False,
        place_eligibility="NotApplicable",
    ))

    source = "tests/pass/g03_unsafe_null_privilege.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="libc_free", route="extern",
        parameter="ptr", spelling="implicit", transfer="CopyIdentity",
        source="KeepLive", dependency="RawUnsafe", formal_ceded=False,
    ))

    mixed = subprocess.run(
        [str(tokac), "--call-transfer-shadow=json",
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

    print(json.dumps({
        "schema": "toka.rc9-m1-call-transfer-shadow-audit",
        "version": 1,
        "result": "pass",
        "cases": len(receipts),
        "routes": sorted({record["route"] for record in receipts}),
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
