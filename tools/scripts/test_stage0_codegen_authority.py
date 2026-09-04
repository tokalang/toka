#!/usr/bin/env python3

"""Qualify audit-only Stage-0 Sema plan authority at CodeGen edges."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/codegen_authority/lib/core"
ALL_ROUTES = FIXTURES / "pass_all_routes.tk"
NONCALL_ROUTES = FIXTURES / "pass_noncall_routes.tk"
CALL_ROUTES = FIXTURES / "pass_call_routes.tk"
EXTERN_ROUTE = FIXTURES / "pass_extern_route.tk"
EXTERN_EDGE = FIXTURES / "pass_extern_edge.tk"
GENERIC_BODY = FIXTURES / "pass_generic_body.tk"
GENERIC_BODY_EDGE = FIXTURES / "pass_generic_body_edge.tk"
GENERIC_CANDIDATE = FIXTURES / "pass_generic_candidate_warm.tk"
REJECTED = FIXTURES / "rejected_plan.tk"
DYNAMIC_TRAIT = FIXTURES / "pass_dynamic_trait_route.tk"
REJECTED_INDIRECT_FN = FIXTURES / "rejected_indirect_fn_route.tk"
REJECTED_INDIRECT_DYN_FN = FIXTURES / "rejected_indirect_dyn_fn_route.tk"
REJECTED_OVERLOAD_INIT = FIXTURES / "rejected_overload_initialization.tk"
REJECTED_ARRAY_GROUP = FIXTURES / "rejected_array_aggregate.tk"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def compile_source(tokac, source, output, authority=False, fault=None,
                   link=False):
    command = [str(tokac)]
    if fault:
        command.append("--stage0-codegen-fault=" + fault)
    elif authority:
        command.append("--stage0-codegen-authority")
    if not link:
        command.append("-c")
    command.extend((str(source), "-o", str(output)))
    return subprocess.run(
        command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    with tempfile.TemporaryDirectory(prefix="toka-stage0-codegen-") as temp:
        work = Path(temp)
        for name, source, link in (
                ("noncall", NONCALL_ROUTES, True),
                ("generic", GENERIC_BODY_EDGE, True),
                ("extern", EXTERN_EDGE, False)):
            normal_output = work / (name + "-normal")
            authority_output = work / (name + "-authority")
            if not link:
                normal_output = normal_output.with_suffix(".o")
                authority_output = authority_output.with_suffix(".o")
            normal = compile_source(tokac, source, normal_output, link=link)
            authority = compile_source(
                tokac, source, authority_output, authority=True, link=link)
            require(normal.returncode == 0, normal.stderr)
            require(authority.returncode == normal.returncode and
                    authority.stdout == normal.stdout and
                    authority.stderr == normal.stderr and
                    authority_output.is_file(),
                    name + " authority mode changed a validated program")
            if link:
                execution = subprocess.run(
                    [str(authority_output)], cwd=work,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, timeout=10)
                require(execution.returncode == 0 and not execution.stderr,
                        name + " authority artifact failed at runtime")

        targets = {
            "call:ordinary": CALL_ROUTES,
            "call:method": CALL_ROUTES,
            "call:static": CALL_ROUTES,
            "call:callable": CALL_ROUTES,
            "call:extern": EXTERN_EDGE,
            "call:dynamic-trait-method": DYNAMIC_TRAIT,
            "call:indirect-fn": REJECTED_INDIRECT_FN,
            "call:indirect-dyn-fn": REJECTED_INDIRECT_DYN_FN,
            "return": NONCALL_ROUTES,
            "standalone": NONCALL_ROUTES,
            "assignment": NONCALL_ROUTES,
            "initialization": NONCALL_ROUTES,
            "aggregate": NONCALL_ROUTES,
            "match_binding": NONCALL_ROUTES,
            "closure_capture": NONCALL_ROUTES,
            "generic-body": GENERIC_BODY_EDGE,
        }
        for target, source in targets.items():
            for kind in ("missing", "mismatch"):
                output = work / (kind + "-" + target.replace(":", "-") +
                                 ".o")
                failed = compile_source(
                    tokac, source, output, fault=kind + ":" + target)
                require(failed.returncode != 0 and
                        "error[E0701]" in failed.stderr and
                        "Stage-0 CodeGen authority" in failed.stderr and
                        not output.exists(),
                        kind + " fault did not fail closed for " + target)

        rejected_cases = (
            (REJECTED, None),
            (REJECTED_INDIRECT_FN, None),
            (REJECTED_INDIRECT_DYN_FN, None),
            (DYNAMIC_TRAIT, None),
            (GENERIC_CANDIDATE, None),
            (REJECTED_OVERLOAD_INIT, "initialization"),
            (REJECTED_ARRAY_GROUP, "aggregate"),
            (ALL_ROUTES, None),
            (CALL_ROUTES, None),
            (GENERIC_BODY, None),
            (EXTERN_ROUTE, None),
        )
        for index, (source, expected_boundary) in enumerate(rejected_cases):
            rejected_normal = work / ("rejected-normal-" + str(index) + ".o")
            rejected_authority = work / (
                "rejected-authority-" + str(index) + ".o")
            normal = compile_source(tokac, source, rejected_normal)
            rejected = compile_source(
                tokac, source, rejected_authority, authority=True)
            require(normal.returncode == 0 and rejected_normal.is_file(),
                    "rejected-plan fixture no longer reflects normal behavior")
            require(rejected.returncode != 0 and
                    "Stage-0 CodeGen authority rejected" in rejected.stderr and
                    (expected_boundary is None or
                     ("for '" + expected_boundary + "'") in
                     rejected.stderr) and
                    not rejected_authority.exists(),
                    "CodeGen accepted a Sema-rejected plan")

        conflict = subprocess.run(
            [str(tokac), "--stage0-codegen-authority", "--check-only",
             str(ALL_ROUTES)], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=30)
        require(conflict.returncode != 0 and not conflict.stdout and
                "requires artifact emission" in conflict.stderr,
                "CodeGen authority was allowed to skip artifact validation")

    print("stage0 codegen authority: pass (16 boundaries, 32 faults)")


if __name__ == "__main__":
    main()
