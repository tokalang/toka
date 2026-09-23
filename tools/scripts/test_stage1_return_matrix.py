#!/usr/bin/env python3

"""Qualify deterministic rows of the Stage-1 return-source matrix."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_build_return_buffers import qualify as qualify_build_return_buffers


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_return_matrix"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(tokac, source, *extra):
    return subprocess.run(
        [str(tokac), *extra, str(FIXTURES / source)], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        timeout=30)


def return_records(tokac, source):
    result = run(tokac, source, "--non-call-transfer-shadow=json",
                 "--check-only")
    require(result.returncode == 0, result.stderr)
    payload = json.loads(result.stdout)
    return [record for record in payload["records"]
            if record["boundary"] == "return" and
            record["location"]["file"].endswith(source)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    rejected = {
        "named_noncopy_requires_cede.tk": "error[E04654]",
        "temporary_explicit_cede_rejected.tk": "error[E04655]",
        "unique_redundant_cede_rejected.tk": "error[E04656]",
        "ordinary_parameter_transfer_rejected.tk": "error[E0473]",
        "removed_nested_result_cede.tk": "error[E04659]",
        "copy_alias_cede_rejected.tk": "error[E04646]",
        "anonymous_borrowed_local_escape.tk": "error[E0455]",
        "anonymous_borrowed_permission_elevation.tk": "error[E04573]",
        "reference_return_type_mismatch.tk": "error[E0408]",
        "reference_return_owning_target.tk": "error[E0454]",
        "explicit_cede_call_temporary.tk": "error[E04655]",
        "explicit_cede_copy_call_temporary.tk": "error[E04655]",
        "alias_result_cede.tk": "error[E04659]",
        "extern_result_cede.tk": "error[E04659]",
        "local_result_cede.tk": "error[E04659]",
        "reference_call_wrong_root.tk": "error[E0454]",
        "raw_view_local_escape.tk": "error[E0455]",
        "raw_view_unknown_address.tk": "error[E04658]",
        "raw_view_overwritten_rejected.tk": "error[E04658]",
        "static_storage_permission_rejected.tk": "error[E0450]",
        "token_source_wrong_dependency.tk": "error[E0454]",
        "token_source_local_escape.tk": "error[E0455]",
        "address_of_static_view_binding.tk": "error[E0455]",
        "address_of_static_holder.tk": "error[E0455]",
        "address_of_static_view_alias.tk": "error[E0455]",
        "address_of_static_view_forwarded.tk": "error[E0455]",
        "static_view_descriptor_in_record.tk": "error[E0455]",
        "address_of_static_holder_projection.tk": "error[E0455]",
        "static_view_descriptor_unwrap.tk": "error[E0455]",
        "rebound_reference_local.tk": "error[E0455]",
        "rebound_reference_wrong_dependency.tk": "error[E0454]",
        "rebound_reference_unknown.tk": "error[E0455]",
        "rebound_reference_branch_local.tk": "error[E0455]",
        "rebound_reference_active_borrow.tk": "error[E0442]",
    }
    for source, diagnostic in rejected.items():
        result = run(tokac, source, "--check-only")
        require(result.returncode != 0 and
                result.stderr.count(diagnostic) == 1 and
                "E0438" not in result.stderr and
                "E0410" not in result.stderr,
                source + " did not fail atomically with " + diagnostic)

    unknown = run(tokac, "rebound_reference_unknown.tk", "--check-only", "--diagnostics-json")
    # Preserve the pre-G lifetime gate: removing the erroneous permanent
    # initializer alias restores this earlier rejection. Unknown storage is
    # not thereby proved local; diagnostic precision is a separate issue.
    unknown_errors = [d for d in json.loads(unknown.stdout)["diagnostics"] if d["code"] == "E0455"]
    require(len(unknown_errors) == 1 and "E04661" not in unknown.stderr,
            "unknown rebind never reached return-source lifetime validation")

    with tempfile.TemporaryDirectory(prefix="toka-return-matrix-") as temp:
        declarations = {
            "unused_alias": "alias Callback = dyn fn() -> cede i32",
            "record_field": "shape Holder(callback: fn() -> cede i32)",
            "trait_result": "trait @Bad { fn value(self) -> cede i32 }",
            "extern_parameter": "extern fn accept(callback: dyn fn() -> cede i32) -> void",
        }
        for name, declaration in declarations.items():
            source = Path(temp) / (name + ".tk")
            source.write_text(declaration + "\nfn main() -> i32 { return 0 }\n")
            output = source.with_suffix(".o")
            failed = subprocess.run(
                [str(tokac), "-c", str(source), "-o", str(output)],
                cwd=ROOT, capture_output=True, text=True, timeout=30)
            require(failed.returncode != 0 and not output.exists() and
                    "error[E04659]" in failed.stderr,
                    name + " admitted a removed function result qualifier")
        for source, diagnostic in rejected.items():
            results = []
            for mode in ("normal", "shadow"):
                output = Path(temp) / (source + "." + mode + ".o")
                flags = [] if mode == "normal" else ["--non-call-transfer-shadow=json"]
                failed = run(tokac, source, *flags, "-c", "-o", str(output))
                require(failed.returncode != 0 and not output.exists() and
                        diagnostic in failed.stderr,
                        source + " produced an artifact despite rejection in " + mode)
                results.append(failed)
            require(results[0].returncode == results[1].returncode and
                    results[0].stderr == results[1].stderr,
                    source + " normal/shadow rejection parity changed")
        for source in (
                "named_copy_keep_live.tk",
                "named_copy_explicit_cede.tk",
                "named_noncopy_explicit_cede.tk",
                "unique_intrinsic_return.tk",
                "cede_parameter_forwarding.tk",
                "control_flow_result_binding.tk",
                "arena_raw_handle_return.tk",
                "copy_alias_keep_live.tk",
                "anonymous_borrowed_return.tk",
                "reference_return_valid.tk",
                "record_reference_fields.tk",
                "consuming_callable_return.tk",
                "static_storage_chain.tk",
                "raw_view_explicit_sources.tk",
                "token_source_chain.tk",
                "span_source_bounds.tk",
                "static_view_storage_positive.tk",
                "rebound_reference_parameter.tk",
                "rebound_reference_alias_copy.tk",
                "rebound_reference_branch_parameter.tk",
                "scalar_binary_member_return.tk"):
            normal = run(tokac, source, "--check-only")
            shadow = run(tokac, source, "--non-call-transfer-shadow=json", "--check-only")
            require(normal.returncode == shadow.returncode == 0 and
                    normal.stderr == shadow.stderr,
                    source + " normal/shadow success parity changed: " +
                    str((normal.returncode, normal.stderr,
                         shadow.returncode, shadow.stderr)))
            artifact = Path(temp) / source.removesuffix(".tk")
            built = run(tokac, source, "-o", str(artifact))
            require(built.returncode == 0 and artifact.is_file(), built.stderr)
            executed = subprocess.run(
                [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, timeout=10)
            require(executed.returncode == 0,
                    source + " failed at runtime: " + executed.stderr)

    alias_records = return_records(tokac, "copy_alias_keep_live.tk")
    scalar_records = return_records(tokac, "scalar_binary_member_return.tk")
    require(any(record["location"]["line"] == 8 and
                record["plan"]["actual_type"] == "i32" and
                record["plan"]["outcome"] == "Admitted" and
                record["source_category"] == "NoSourcePlace" and
                record["plan"]["value_production"] == "CopyValue" and
                record["plan"]["drop"] == "NoLiability"
                for record in scalar_records),
            "writable member permission leaked into scalar arithmetic result")
    require(any(record["plan"]["value_production"] == "CopyValue" and
                record["plan"]["source"] == "KeepLive" and
                record["plan"]["exact_path"] and
                record["plan"]["referent_path"]
                for record in alias_records),
            "Copy alias return did not preserve source and referent")

    raw_records = return_records(tokac, "arena_raw_handle_return.tk")
    require(any(record["plan"]["source_view"] == "RawHandle" and
                record["plan"]["value_production"] == "CopyIdentity" and
                record["plan"]["source"] == "KeepLive"
                for record in raw_records),
            "Arena raw handle return was not an identity copy")

    borrowed_records = return_records(tokac, "anonymous_borrowed_return.tk")
    require(any(record["source_category"] == "NoSourcePlace" and
                record["plan"]["value_production"] == "BorrowCapture" and
                record["plan"]["source"] == "NoSourcePlace" and
                record["plan"]["drop"] == "NoLiability" and
                len(record["plan"]["dependency_roots"]) == 2
                for record in borrowed_records),
            "Anonymous borrowed return lacks exact field dependencies")

    flow_records = return_records(tokac, "control_flow_result_binding.tk")
    moves = [record for record in flow_records
             if record["plan"]["value_production"] == "MoveOwned" and
             record["plan"]["source"].startswith("Invalidate") and
             record["plan"]["drop"] == "DestinationAssumesLiability"]
    require(len(moves) == 1,
            "match result binding did not transfer exactly once")
    require(any(record["plan"]["value_production"] == "CopyValue" and
                record["plan"]["source"] == "KeepLive"
                for record in flow_records),
            "for result binding was not returned as a Copy value")

    reference_records = return_records(tokac, "reference_call_roots.tk")
    outer = [record for record in reference_records
             if record["location"]["line"] == 3]
    require(len(outer) == 1 and
            outer[0]["plan"]["outcome"] == "Admitted" and
            "binding:b;" in outer[0]["plan"]["referent_path"] and
            "binding:a;" not in outer[0]["plan"]["referent_path"] and
            len(outer[0]["plan"]["dependency_roots"]) == 1 and
            "binding:b;" in outer[0]["plan"]["dependency_roots"][0],
            "return ceiling was substituted for the actual b referent")

    static_records = return_records(tokac, "static_storage_chain.tk")
    stored = [record for record in static_records if record.get("static_storage_origins")]
    require(len(stored) == 1 and
            not stored[0]["plan"]["dependency_roots"] and
            not stored[0]["plan"]["referent_path"] and
            stored[0]["plan"]["value_production"] == "CopyIdentity" and
            stored[0]["plan"]["drop"] == "NoLiability",
            "Static binding chain lost its literal witness or gained ownership")

    storage_records = return_records(tokac, "static_view_storage_positive.tk")
    address_returns = [record for record in storage_records
                       if ";function:borrow_" in record["group_identity"]]
    require(len(address_returns) == 2 and all(
                record["plan"]["referent_path"] and
                record["plan"]["dependency_roots"] and
                not record.get("static_storage_origins")
                for record in address_returns),
            "Descriptor storage borrow inherited the view's static character lifetime")
    projected_returns = [record for record in storage_records
                         if ";function:projected;" in record["group_identity"]]
    require(len(projected_returns) == 1 and
            "binding:holder;" in projected_returns[0]["plan"]["referent_path"] and
            "binding:reference;" not in projected_returns[0]["plan"]["referent_path"] and
            len(projected_returns[0]["plan"]["dependency_roots"]) == 1 and
            "binding:holder;" in projected_returns[0]["plan"]["dependency_roots"][0],
            "Reference projection evidence used the local alias instead of its target storage")
    for source in ("rebound_reference_parameter.tk", "rebound_reference_alias_copy.tk",
                   "rebound_reference_branch_parameter.tk"):
        records = [record for record in return_records(tokac, source)
                   if ";function:select;" in record["group_identity"]]
        require(len(records) == 1 and
                "binding:second;" in records[0]["plan"]["referent_path"] and
                "binding:first;" not in records[0]["plan"]["referent_path"] and
                len(records[0]["plan"]["dependency_roots"]) == 1 and
                "binding:second;" in records[0]["plan"]["dependency_roots"][0],
                source + " retained the old initializer instead of the current target")
    for source in ("address_of_static_view_binding.tk", "address_of_static_holder.tk",
                   "address_of_static_view_alias.tk", "address_of_static_view_forwarded.tk",
                   "static_view_descriptor_in_record.tk", "address_of_static_holder_projection.tk",
                   "static_view_descriptor_unwrap.tk"):
        observed = run(tokac, source, "--non-call-transfer-shadow=json", "--check-only")
        require(observed.returncode != 0 and "error[E0455]" in observed.stderr,
                source + " escaped its descriptor storage")
        records = [record for record in json.loads(observed.stdout)["records"]
                   if record["boundary"] == "return" and
                   record["location"]["file"].endswith(source)]
        require(records and all(not record.get("static_storage_origins") for record in records),
                source + " incorrectly proved static descriptor storage")

    view_records = return_records(tokac, "raw_view_explicit_sources.tk")
    for index in (1, 2):
        selected = [record for record in view_records
                    if record["plan"]["referent_path"].endswith("/index:" + str(index))]
        require(len(selected) == 1 and
                "binding:data;" in selected[0]["plan"]["referent_path"] and
                not selected[0].get("static_storage_origins") and
                selected[0]["plan"]["drop"] == "NoLiability",
                "raw-view return lost the actual data projection " + str(index))

    token_records = return_records(tokac, "token_source_chain.tk")
    token_return = [record for record in token_records
                    if ";function:next;" in record["group_identity"]]
    require(len(token_return) == 1 and
            token_return[0]["plan"]["outcome"] == "Admitted" and
            token_return[0]["plan"]["value_production"] == "MoveOwned" and
            token_return[0]["plan"]["source"] == "InvalidateSubtree" and
            token_return[0]["plan"]["drop"] == "NoLiability" and
            len(token_return[0]["plan"]["dependency_roots"]) == 1 and
            "binding:self;" in token_return[0]["plan"]["dependency_roots"][0],
            "Token factory lost its source dependency or manufactured Drop liability")

    qualify_build_return_buffers(args.build_dir)
    print("stage1 return matrix: pass")


if __name__ == "__main__":
    main()
