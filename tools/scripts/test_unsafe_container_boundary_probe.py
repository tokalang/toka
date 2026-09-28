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
    "direct_self_owner_alive",
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
                for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (name + suffix)
                    rejected = compile(source, flag, "-o", str(output))
                    require(rejected.returncode == 1 and "E0455" in rejected.stderr and
                            not output.exists(), name + ": rejected artifact was produced")
                print("PASS escape rejection " + name, flush=True)

        if args.mode == "diagnose":
            print("Four-case unannotated Vec rejection complete")
            return

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
                                 ("assignment_unknown_stays_unknown", "E0455"),
                                 ("unsafe_from_raw_safe_reject", "E0623"),
                                 ("unsafe_set_len_safe_reject", "E0623"),
                                 ("unsafe_storage_safe_reject", "E0623"),
                                 ("private_vec_fields_reject", "E0418"),
                                 ("private_vec_fields_unsafe_reject", "E0418"),
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
            output = work / (name + ".o")
            rejected = compile(source, "-c", "-o", str(output))
            require(rejected.returncode == 1 and not output.exists(),
                    name + ": rejected object was produced")
            print("PASS existing rejection " + name, flush=True)

    print("Unsafe container boundary probe: " + args.mode + " complete")


if __name__ == "__main__":
    main()
