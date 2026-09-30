#!/usr/bin/env python3
"""Isolated Vec dependency probe; never execute escaping binaries."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/semantics/unsafe_container_boundary"
ESCAPES = (
    "whole_vec_escape", "pop_option_escape", "remove_option_escape",
    "owned_next_escape", "hidden_shape_escape", "hidden_enum_escape",
    "unwrap_view_escape", "shadowed_local_escape", "alias_source_escape",
    "alias_receiver_escape", "branch_sources_escape",
    "insert_escape", "set_escape", "clear_retains_dependency",
    "take_escape", "resize_hidden_borrow_escape",
    "unsafe_does_not_erase_view_escape",
    "qualified_push_escape", "qualified_remove_escape",
    "assignment_escape", "assignment_branch_escape",
    "assignment_field_escape", "assignment_alias_field_escape",
    "assignment_shadowed_source_escape", "assignment_hidden_enum_escape",
    "direct_self_effect_escape", "direct_external_result_escape",
    "assignment_view_rebase_local_escape",
)
PAL_ESCAPES = (
    ("owner_cede_while_vec_live", "E04660", "ActiveDerivedBorrow"),
    ("aliased_owner_cede_while_vec_live", "E04660", "ActiveDerivedBorrow"),
    ("hidden_enum_owner_cede_while_vec_live", "E04660", "ActiveDerivedBorrow"),
    ("mixed_owned_field_owner_cede_escape", "E04660", "ActiveDerivedBorrow"),
    ("owner_scope_exit_while_vec_live", "E0440", "owner.buf"),
    ("same_scope_drop_reads_view", "E0440", "owner.buf"),
    ("same_scope_drop_reads_view_fallthrough", "E0440", "owner.buf"),
    ("same_scope_drop_reads_view_early_return", "E0440", "owner.buf"),
    ("early_return_before_retirement", "E0440", "owner.buf"),
    ("early_break_before_retirement", "E0440", "owner.buf"),
    ("early_continue_before_retirement", "E0440", "owner.buf"),
    ("for_break_before_retirement", "E0440", "owner.buf"),
    ("propagation_before_retirement", "E0440", "owner.buf"),
    ("pass_before_retirement", "E0440", "owner.buf"),
    ("early_return_view_descriptor_bad", "E0440", "owner.buf"),
    ("closure_return_before_retirement", "E0440", "owner.buf"),
    ("closure_propagation_before_retirement", "E0440", "owner.buf"),
    ("break_outer_holder_stays_live", "E0441", "owner.buf"),
    ("continue_outer_holder_stays_live", "E0441", "owner.buf"),
    ("owner_mutation_while_vec_live", "E0441", "owner.buf"),
    ("with_capacity_owner_mutation_escape", "E0441", "owner.buf"),
    ("moved_holder_owner_mutation_escape", "E0441", "owner.buf"),
    ("moved_holder_short_owner_escape", "E0440", "owner.buf"),
    ("one_of_two_holders_removed_escape", "E0441", "owner.buf"),
    ("branch_holder_owner_mutation_escape", "E0441", "owner.buf"),
    ("extracted_view_owner_cede_escape", "E04660", "ActiveDerivedBorrow"),
)
RUNTIME = (
    "local_view_alive", "owned_string_return", "owned_token_exact_drop",
    "extracted_view_outlives_vec", "owned_pop_outlives_vec",
    "shadowed_parameter_alive", "production_nested_vec_lifecycle",
    "unsafe_from_raw_empty_control", "unsafe_public_raw_control",
    "qualified_owner_alive", "assignment_replaces_old_dependency",
    "assignment_owned_exact_once", "assignment_shadowed_target_alive",
    "qualified_shadowed_target_alive",
    "assignment_owned_pattern_alive",
    "assignment_view_rebase_parameter",
    "direct_self_owner_alive",
    "last_holder_scope_release", "last_holder_discard_release",
    "moved_holder_release", "extracted_view_scope_release",
    "short_owner_holder_discard",
    "replaced_holder_releases_old_owner",
    "same_scope_safe_drop_order",
    "same_scope_descriptor_only_cleanup",
    "safe_drop_order_control", "safe_drop_order_early_return",
    "same_scope_holder_discard_before_owner",
    "early_return_safe_order_control", "early_return_retired_branch_control",
    "early_loop_safe_order_control", "propagation_safe_order_control",
    "pass_safe_order_control",
    "early_return_view_descriptor_good",
    "safe_closure_before_retirement", "safe_closure_after_retirement",
    "closure_propagation_safe_order", "nested_closure_cleanup_boundary",
    "safe_terminated_branch_sources", "safe_terminated_match_sources",
    "safe_terminated_branch_sources_control",
    "dual_continuing_branch_sources", "dual_continuing_match_sources",
    "shadowed_terminated_branch_sources",
    "guard_terminated_branch_sources", "loop_zero_iteration_sources",
    "for_zero_iteration_sources",
    "safe_terminated_loop_pal", "safe_terminated_loop_as_if_control",
    "safe_terminated_loop_pal_control",
    "safe_break_retired_holder", "safe_break_retired_holder_control",
    "safe_continue_retired_holder", "safe_continue_retired_holder_control",
    "for_break_retired_holder", "for_continue_retired_holder",
    "break_shadowed_holder_scope",
    "safe_guard_binding_terminated_sources",
    "safe_guard_binding_terminated_sources_control",
    "guard_binding_success_value",
    "unrelated_mutations_owner_alive",
    "with_capacity_empty_then_static",
    "all_holders_removed_release",
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--mode", choices=("diagnose", "production"),
                        default="production")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    compiler = build / "bin/tokac"
    require(compiler.is_file(), "tokac is missing")

    with tempfile.TemporaryDirectory(prefix="toka-unsafe-container-boundary-") as directory:
        work = Path(directory)
        overlay_lib = work / "overlay/lib"
        (overlay_lib / "std").mkdir(parents=True)
        vec_path = overlay_lib / "std/vec.tk"
        if args.mode == "diagnose":
            baseline = subprocess.run(
                ["git", "show", "a1fdc191b4be453654f458d465dd9f3fb1735696:lib/std/vec.tk"],
                cwd=ROOT, capture_output=True, text=True, timeout=15)
            require(baseline.returncode == 0, "baseline Vec source unavailable")
            vec_path.write_text(baseline.stdout)
            with (CASES / "pop_remove.patch").open("rb") as patch:
                applied = subprocess.run(["patch", "-p1"], cwd=work / "overlay",
                                         stdin=patch, capture_output=True,
                                         timeout=15)
            require(applied.returncode == 0,
                    "isolated pop patch failed: " + applied.stderr.decode())
        env = dict(os.environ, TOKA_LIB=os.pathsep.join(
            (str(overlay_lib), str(ROOT / "lib"), str(build / "lib"))))

        def compile(source, *flags, isolated=True):
            isolated = isolated and args.mode != "production"
            command = [str(compiler)]
            if isolated:
                command += ["-I", str(overlay_lib)]
            command += [*flags, str(source)]
            return subprocess.run(command, cwd=overlay_lib if isolated else ROOT,
                                  env=env if isolated else dict(os.environ, TOKA_LIB=os.pathsep.join(
                                      (str(ROOT / "lib"), str(build / "lib")))),
                                  capture_output=True, text=True, timeout=60)

        if args.mode == "diagnose":
            for name in ESCAPES[:4]:
                source = CASES / (name + ".tk")
                baseline = compile(source, "--check-only", isolated=False)
                require(baseline.returncode == 1 and "E0455" in baseline.stderr,
                        name + ": production escape was accepted")
                for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / ("production-" + name + suffix)
                    rejected = compile(source, flag, "-o", str(output), isolated=False)
                    require(rejected.returncode == 1 and not output.exists(),
                            name + ": production guard produced an artifact")

        cases = ESCAPES[:4] if args.mode == "diagnose" else ESCAPES
        for name in cases:
            source = CASES / (name + ".tk")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode and normal.stderr == shadow.stderr,
                    name + ": normal/shadow disagree")
            if args.mode == "diagnose":
                require(normal.returncode == 1 and
                        "ReceiverWriteDependencyUndeclared" in normal.stderr,
                        name + ": unannotated baseline Vec was accepted: " + normal.stderr)
                for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / ("unannotated-" + name + suffix)
                    rejected = compile(source, flag, "-o", str(output))
                    require(rejected.returncode == 1 and not output.exists(),
                            name + ": unannotated Vec produced an artifact")
                print("PASS unannotated receiver write rejected " + name,
                      flush=True)
            else:
                require(normal.returncode == 1 and "E0455" in normal.stderr,
                        name + ": did not reach local-owner lifetime rejection: " + normal.stderr)
                if name == "assignment_view_rebase_local_escape":
                    require("local variable 'owner" in normal.stderr,
                            "view assignment kept its local alias as the source")
                for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (name + suffix)
                    rejected = compile(source, flag, "-o", str(output))
                    require(rejected.returncode == 1 and "E0455" in rejected.stderr and
                            not output.exists(), name + ": rejected artifact was produced")
                print("PASS escape rejection " + name, flush=True)

        if args.mode == "diagnose":
            print("Four-case unannotated Vec rejection complete")
            return

        for name, error_code, source_name in PAL_ESCAPES:
            source = CASES / (name + ".tk")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and
                    normal.stderr == shadow.stderr and
                    error_code in normal.stderr and source_name in normal.stderr and
                    str(source) in normal.stderr,
                    name + ": did not reach the owner-lifetime PAL gate: " + normal.stderr)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                rejected = compile(source, flag, "-o", str(output))
                require(rejected.returncode == 1 and error_code in rejected.stderr and
                        not output.exists(),
                        name + ": rejected carrier lifetime produced an artifact")
            print("PASS carrier PAL rejection " + name, flush=True)

        for name in RUNTIME + ("reference_growth",):
            source = (CASES / (name + ".tk") if name != "reference_growth" else
                      ROOT / "tests/semantics/reference_domains/vec_growth.tk")
            output = work / name
            built = compile(source, "-o", str(output))
            require(built.returncode == 0 and output.is_file(),
                    name + ": " + built.stderr)
            ran = subprocess.run([str(output)], capture_output=True, text=True, timeout=20)
            require(ran.returncode == 0, name + ": runtime " + ran.stderr)
            print("PASS runtime " + name, flush=True)

        for name, error_code in (("reference_escape", "E0455"),
                                 ("borrowed_element_storage_escape", "E0455"),
                                 ("unwrap_borrowed_element_storage_escape", "E0455"),
                                 ("active_borrow_mutation", "E0441"),
                                 ("rejected_call_rollback", "E04557"),
                                 ("qualified_rejected_call_rollback", "E04557"),
                                 ("direct_rejected_call_rollback", "E04569"),
                                 ("assignment_rejected_rollback", "E0408"),
                                 ("rejected_carrier_assignment_restores_loan", "E0408"),
                                 ("rejected_carrier_call_restores_loan", "E04571"),
                                 ("assignment_unknown_stays_unknown", "E04661"),
                                 ("assignment_unknown_rejection_preserves_source", "E04661"),
                                 ("branch_unknown_stays_unknown", "E04661"),
                                 ("unsafe_from_raw_safe_reject", "E0623"),
                                 ("unsafe_set_len_safe_reject", "E0623"),
                                 ("unsafe_storage_safe_reject", "E0623"),
                                 ("private_vec_fields_reject", "E0418"),
                                 ("private_vec_fields_unsafe_reject", "E0418"),
                                 ("dual_continuing_missing_source", "E0454"),
                                 ("dual_continuing_match_missing_source", "E0454"),
                                 ("private_vec_constructor_reject", "E0418")):
            source = CASES / (name + ".tk")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and
                    normal.stderr == shadow.stderr and error_code in normal.stderr,
                    name + ": existing safety gate changed")
            if name in ("rejected_call_rollback",
                        "qualified_rejected_call_rollback",
                        "direct_rejected_call_rollback",
                        "assignment_rejected_rollback"):
                require("error[E0455]" not in normal.stderr,
                        "rejected call polluted receiver dependencies")
            if name == "assignment_rejected_rollback":
                require("error[E0438]" not in normal.stderr,
                        "rejected assignment consumed its source")
            if name == "rejected_carrier_call_restores_loan":
                require("error[E0441]" in normal.stderr and "owner.buf" in normal.stderr,
                        "rejected call released the live carrier loan")
            if name == "rejected_carrier_assignment_restores_loan":
                require("error[E0441]" in normal.stderr and "owner.buf" in normal.stderr,
                        "rejected assignment released the live carrier loan")
            if name in ("assignment_unknown_stays_unknown",
                        "assignment_unknown_rejection_preserves_source",
                        "branch_unknown_stays_unknown"):
                require("ExternalValueDependenciesUnknown" in normal.stderr and
                        "error[E0438]" not in normal.stderr,
                        "unknown assignment escaped or consumed its source")
            output = work / (name + ".o")
            rejected = compile(source, "-c", "-o", str(output))
            require(rejected.returncode == 1 and not output.exists(),
                    name + ": rejected object was produced")
            if name in ("dual_continuing_missing_source",
                        "dual_continuing_match_missing_source"):
                ir = work / (name + ".ll")
                rejected_ir = compile(source, "--emit-llvm", "-o", str(ir))
                require(rejected_ir.returncode == 1 and not ir.exists(),
                        name + ": rejected IR was produced")
            print("PASS existing rejection " + name, flush=True)

    print("Unsafe container boundary probe: " + args.mode + " complete")


if __name__ == "__main__":
    main()
