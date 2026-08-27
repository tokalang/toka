#!/usr/bin/env python3

"""Qualify the audit-only RC9 M1 call-transfer shadow planner."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(tokac, source, expected_error=None, check_only=True):
    command = [str(tokac), "--call-transfer-shadow=json"]
    if check_only:
        command.append("--check-only")
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
    require(payload.get("version") == 2, source + " emitted the wrong version")
    require(payload.get("status") == "audit-only",
            source + " did not identify audit-only output")
    records = payload.get("records")
    require(isinstance(records, list), source + " omitted records")
    required = {
        "callee", "route", "parameter", "argument_index", "formal_index",
        "value_category", "spelling", "transfer", "source", "dependency",
        "place_eligibility", "drop", "execution_boundary",
        "source_root_id", "source_path", "source_identity",
        "referent_path", "referent_identity", "dependency_paths",
        "cleanup_mask", "formal_ceded", "formal_init",
        "legacy_caller_rule_applied", "legacy_cede_exempt",
        "legacy_missing_cede", "async", "location", "contract_location",
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
        if record["value_category"] in ("Place", "InitStorage"):
            require(record["source_root_id"] > 0 and
                    bool(record["source_identity"]),
                    source + " place plan omitted structured source identity")
        if record["value_category"] in ("Temporary", "Indeterminate"):
            require(record["source_root_id"] == 0,
                    source + " non-place plan retained a source root")
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
        if all((record.get("location", {}).get("line") if key == "location_line"
                else record.get(key)) == value
               for key, value in expected.items()):
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
    require(records == run(tokac, source, check_only=False),
            "shadow mode without --check-only changed output")
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="CopyValue",
        source="KeepLive", source_path="plain", value_category="Place",
        drop="NoLiability", formal_ceded=True, formal_index=1,
        legacy_caller_rule_applied=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    ))
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="implicit", transfer="ConsumeTemporary",
        source="NoSourcePlace", source_path="", value_category="Temporary",
        drop="NoLiability", formal_ceded=True,
        legacy_cede_exempt=True, legacy_missing_cede=False,
    ))
    receipts.append(find(
        records, source, callee="consume_pair", route="ordinary",
        parameter="value", spelling="implicit", transfer="CopyValue",
        source="KeepLive", source_path="pair", value_category="Place",
        formal_ceded=True, legacy_cede_exempt=True,
    ))
    receipts.append(find(
        records, source, callee="consume_resource", route="ordinary",
        parameter="value", spelling="explicit",
        transfer="ConsumeTemporary", source="NoSourcePlace",
        value_category="Temporary", drop="DestinationAssumesLiability",
        formal_ceded=True,
    ))

    source = "tests/semantics/call_transfer_shadow_m1/copy_explicit_invalidates.tk"
    records = run(tokac, source, expected_error="E0438")
    receipts.append(find(
        records, source, callee="consume", route="ordinary",
        parameter="value", spelling="explicit", transfer="CopyValue",
        source="InvalidatePlace", source_path="plain", value_category="Place",
        formal_ceded=True,
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
        legacy_caller_rule_applied=True,
        place_eligibility="PendingValidation",
    ))

    source = "tests/conformance/diagnostics/callable_cede_parameter_requires_explicit_transfer.tk"
    records = run(tokac, source, expected_error="E04570")
    receipts.append(find(
        records, source, callee="consumer", route="callable",
        parameter="token", spelling="implicit", transfer="MoveOwned",
        source="InvalidatePlace", source_path="source", formal_ceded=True,
        formal_index=2, legacy_cede_exempt=False, legacy_missing_cede=True,
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
        execution_boundary="StartHandoff", **{"async": True},
    ))

    source = "tests/conformance/diagnostics/cede_argument_to_borrowed_parameter_rejected.tk"
    records = run(tokac, source, expected_error="E04640")
    receipts.append(find(
        records, source, callee="borrow", route="ordinary",
        parameter="value", spelling="explicit", transfer="Reject",
        source="NoStateChange", source_path="value", formal_ceded=False,
        place_eligibility="NotApplicable", drop="NoStateChange",
    ))

    source = "tests/pass/g03_unsafe_null_privilege.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="libc_free", route="extern",
        parameter="ptr", spelling="implicit", transfer="CopyIdentity",
        source="KeepLive", dependency="RawUnsafe", formal_ceded=False,
    ))

    source = "tests/pass/g08_dyn_closure.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="cb", route="indirect-dyn-fn",
        parameter="arg1", argument_index=1, formal_index=1,
        value_category="Place", transfer="CopyValue", source="KeepLive",
        legacy_caller_rule_applied=False,
    ))
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

    source = "tests/semantics/call_transfer_shadow_m1/dynamic_trait_method.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="Transform::apply",
        route="dynamic-trait-method", parameter="value", argument_index=1,
        formal_index=2, value_category="Place", transfer="CopyValue",
        source="KeepLive", legacy_caller_rule_applied=False,
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

    source = "tests/pass/g16_init_parameter_test.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="initialize", route="ordinary",
        parameter="out", value_category="InitStorage",
        transfer="InitStorage", formal_init=True, formal_index=1,
    ))

    source = "tests/semantics/call_transfer_shadow_m1/borrowed_view_paths.tk"
    records = run(tokac, source)
    receipts.append(find(
        records, source, callee="consume_view", route="ordinary",
        parameter="value", value_category="Place", transfer="CopyIdentity",
        source="InvalidatePlace", dependency="Borrowed", source_path="view",
        dependency_paths=["owner.buf"],
    ))

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

    replay_case = ROOT / "tests/semantics/tki_replay/cases/own_cede_003_generic_methods"
    with tempfile.TemporaryDirectory(prefix="toka-call-transfer-shadow-") as temp:
        work = Path(temp)
        for name in ("lib.tk", "fail_generic_missing_cede.tk"):
            shutil.copy2(replay_case / name, work / name)
        provider = subprocess.run(
            [str(tokac), "-c", work / "lib.tk", "-o", work / "lib.o"],
            cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30,
        )
        require(provider.returncode == 0 and (work / "lib.tki").is_file(),
                "source-less shadow provider compilation failed")

        def replay_record():
            process = subprocess.run(
                [str(tokac), "--call-transfer-shadow=json",
                 work / "fail_generic_missing_cede.tk"],
                cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=30,
            )
            require(process.returncode != 0 and "E04570" in process.stderr,
                    "source-less shadow consumer changed legacy diagnostics")
            payload = json.loads(process.stdout)
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
            return record

        source_record = replay_record()
        (work / "lib.tk").rename(work / "lib.tk.source-hidden")
        hidden_record = replay_record()
        require(source_record == hidden_record,
                "source and source-less shadow plans differ")
        receipts.append(hidden_record)

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

    diagnostics_mixed = subprocess.run(
        [str(tokac), "--call-transfer-shadow=json", "--diagnostics-json",
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
        ("tests/conformance/diagnostics/static_cede_parameter_requires_explicit_transfer.tk", "E04570"),
        ("tests/conformance/diagnostics/cede_argument_to_borrowed_parameter_rejected.tk", "E04640"),
        ("tests/conformance/diagnostics/aggregate_owned_value_requires_cede.tk", "E04652"),
    ]
    for normal_source, expected_error in normal_cases:
        normal = subprocess.run(
            [str(tokac), "--check-only", normal_source], cwd=ROOT,
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

    print(json.dumps({
        "schema": "toka.rc9-m1-call-transfer-shadow-audit",
        "version": 2,
        "result": "pass",
        "cases": len(receipts),
        "normal_cases": len(normal_cases),
        "routes": sorted({record["route"] for record in receipts}),
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
