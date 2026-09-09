#!/usr/bin/env python3
"""Directed binding-transfer checks; not a complete slice qualification yet."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_binding_transfer"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-binding-gate-") as temp:
        for source, expected in {
                "initialization_rollback.tk": "E04606",
                "assignment_rollback.tk": "E04572",
                "overlap.tk": "E04615",
                "no_source.tk": "E04661",
                "copy_invalidation.tk": "E0438",
                "callable_unknown.tk": "E04661",
                "callable_raw_unknown.tk": "E04661",
                "callable_escape.tk": "E0455",
                "callable_rollback.tk": "E04572",
                "callable_environment_rollback.tk": "E04661",
                "callable_consuming_copy_assignment.tk": "E04661",
                "callable_borrowed_target_rejected.tk": "E04661",
                "callable_overlap.tk": "E04615",
                "callable_repeated_transfer.tk": "E0438",
                "callable_parameter_escape.tk": "E0454",
                "callable_ordinary_parameter_rejected.tk": "E0473"}.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--non-call-transfer-shadow=json", "--check-only")
            require(normal.returncode != 0 and expected in normal.stderr,
                    source + ": " + normal.stderr)
            require(normal.returncode == shadow.returncode and normal.stderr == shadow.stderr,
                    source + " normal/shadow mismatch")
            if source not in ("copy_invalidation.tk", "callable_repeated_transfer.tk"):
                require("E0438" not in normal.stderr and "E0410" not in normal.stderr,
                        source + " leaked source invalidation")
            if expected == "E04661":
                require(normal.stderr.count("error[E04661]") == 1,
                        source + " lost environment facts after rejection")
            for flags, suffix in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                output = Path(temp) / (source + suffix)
                result = run(source, *flags, "-o", str(output))
                require(result.returncode != 0 and not output.exists(),
                        source + " emitted a rejected artifact")

        source = "exact_once.tk"
        normal = run(source, "--check-only")
        shadow = run(source, "--non-call-transfer-shadow=json", "--check-only")
        require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                normal.stderr + shadow.stderr)
        records = [r for r in json.loads(shadow.stdout)["records"]
                   if r["location"]["file"].endswith(source) and
                   r["boundary"] in ("assignment", "initialization")]
        plans = [r["plan"] for r in records if r["plan"]["outcome"] == "Admitted"]
        for production, disposition in (("CopyValue", "KeepLive"),
                                        ("CopyValue", "InvalidateSubtree"),
                                        ("MoveOwned", "InvalidateSubtree"),
                                        ("MoveOwned", "InvalidateRoot"),
                                        ("TransferShared", "InvalidateBinding"),
                                        ("ConsumeTemporary", "NoSourcePlace")):
            require(any(p["value_production"] == production and p["source"] == disposition
                        for p in plans), "Missing binding row: " + production + "/" + disposition)
        artifact = Path(temp) / "exact-once"
        built = run(source, "-o", str(artifact))
        require(built.returncode == 0 and artifact.is_file(), built.stderr)
        executed = subprocess.run([str(artifact)], cwd=temp, capture_output=True,
                                  text=True, timeout=15)
        require(executed.returncode == 0, "Binding cleanup failed: " + str(executed.returncode))
        for source in ("callable_exact_once.tk", "callable_copy_assignment.tk",
                       "callable_borrow.tk", "callable_parameter_bound.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--non-call-transfer-shadow=json", "--check-only")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    normal.stderr + shadow.stderr)
            records = [r for r in json.loads(shadow.stdout)["records"]
                       if r["location"]["file"].endswith(source) and
                       r["boundary"] in ("assignment", "initialization") and
                       "fn(" in r["plan"]["actual_type"]]
            coordinates = [(r["boundary"], r["location"]["line"], r["location"]["column"])
                           for r in records]
            require(len(coordinates) == len(set(coordinates)),
                    source + " published both preliminary and final plans")
            require(records and all(r["plan"]["outcome"] == "Admitted" for r in records),
                    source + " contains rejected callable initialization records")
            if source in ("callable_exact_once.tk", "callable_copy_assignment.tk"):
                for record in records:
                    plan = record["plan"]
                    consuming = plan["actual_type"].startswith("cede ")
                    dynamic = "dyn fn" in plan["actual_type"]
                    require(plan["copy_proof"] == ("ProvenNonCopy" if consuming else "ProvenCopy"),
                            "Callable consuming mode lost: " + repr(plan))
                    if dynamic:
                        expected = "SharedLiabilityIncremented" if plan["source"] == "KeepLive" else "DestinationAssumesLiability"
                        require(plan["drop"] == expected and plan["liability_identity"],
                                "Missing validated environment liability: " + repr(plan))
                    else:
                        require(plan["drop"] == "NoLiability" and not plan["liability_identity"],
                                "Thin fn acquired an environment cleanup")
            elif source == "callable_borrow.tk":
                require(all(r["plan"]["dependency_roots"] and
                            any("binding:value" in p for p in r["plan"]["dependency_roots"])
                            for r in records), "Borrowed capture lost its actual referent")
            else:
                bounded = [r for r in records if any("binding:callback" in p
                           for p in r["plan"]["dependency_roots"])]
                require(len(bounded) == 2 and all(
                    r["dependency"] == "Structural" and
                    r["plan"]["referent_path"] == "" and
                    r["plan"]["source"] == "KeepLive" and
                    r["plan"]["drop"] == "SharedLiabilityIncremented"
                    for r in bounded),
                    "Parameter contract was lost or claimed to be an independent environment")
                all_records = [r for r in json.loads(shadow.stdout)["records"]
                               if r["location"]["file"].endswith(source)]
                require(any(r["boundary"] == "initialization" and
                            r["plan"]["obligation_before"] == "Outstanding" and
                            r["plan"]["source_obligation_action"] == "DischargeToStorage" and
                            r["plan"]["source"].startswith("Invalidate")
                            for r in all_records),
                        "Synthesized callable cede parameter lost its obligation")
            artifact = Path(temp) / source.removesuffix(".tk")
            built = run(source, "-o", str(artifact))
            require(built.returncode == 0 and artifact.is_file(), built.stderr)
            executed = subprocess.run([str(artifact)], cwd=temp, capture_output=True,
                                      text=True, timeout=15)
            require(executed.returncode == 0,
                    source + " runtime failed: " + str(executed.returncode))
        for source, diagnostic in {
                "callable_parameter_not_writable.tk": "E04592",
                "callable_factory_escape.tk": "E0455",
                "callable_factory_invalid.tk": "E04606",
                "callable_factory_recursive.tk": "E04658",
                "callable_factory_invalid_dependency.tk": "E04606",
                "callable_factory_scope_isolation.tk": "E0402",
                "callable_factory_invalid_rollback.tk": "E04606",
                "callable_factory_unsafe_isolation.tk": "E04662"}.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and
                    normal.stderr == shadow.stderr and diagnostic in normal.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            if source == "callable_factory_invalid.tk":
                require("E04661" in normal.stderr, "invalid factory published an environment summary")
            if source == "callable_factory_invalid_dependency.tk":
                require(normal.stderr.count("error[E04606]") == 1 and
                        normal.stderr.count("error[E04661]") == 2,
                        "invalid cache dependency was rechecked or granted authority")
            if source == "callable_factory_invalid_rollback.tk":
                require("E0438" not in normal.stderr and "E0410" not in normal.stderr,
                        "on-demand factory validation leaked caller invalidation")
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                artifact = Path(temp) / (source + suffix)
                rejected = run(source, flag, "-o", str(artifact))
                require(rejected.returncode == 1 and not artifact.exists(),
                        source + " produced an artifact or crashed")
        for source in ("callable_parameter_writable.tk", "callable_factory_borrowed.tk",
                       "callable_factory_order_before.tk", "callable_factory_order_after.tk",
                       "callable_factory_chain.tk", "callable_factory_generic_cache.tk",
                       "callable_factory_generic_cold.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            if source == "callable_factory_borrowed.tk":
                lines = (FIXTURES / source).read_text().splitlines()
                target_lines = {index + 1 for index, line in enumerate(lines)
                                if "auto callback =" in line or "auto branch =" in line}
                records = [r for r in json.loads(shadow.stdout)["records"]
                           if r["location"]["file"].endswith(source) and
                           r["boundary"] == "initialization" and r["location"]["line"] in target_lines]
                require(len(records) == 2 and all(r["plan"]["outcome"] == "Admitted" and
                        r["plan"]["dependency_roots"] and
                        r["plan"]["drop"] == "DestinationAssumesLiability" for r in records),
                        "factory/mixed returns lost their borrowed environment dependencies")
            if source in ("callable_factory_order_before.tk", "callable_factory_order_after.tk"):
                lines = (FIXTURES / source).read_text().splitlines()
                token_line = next(i + 1 for i, line in enumerate(lines) if "auto token =" in line)
                body_records = [r for r in json.loads(shadow.stdout)["records"]
                                if r["location"]["file"].endswith(source) and
                                r["location"]["line"] == token_line and r["boundary"] == "initialization"]
                require(len(body_records) == 1, "factory body replayed on cache hit/module walk")
            artifact = Path(temp) / source.removesuffix(".tk")
            built = run(source, "-o", str(artifact))
            require(built.returncode == 0 and artifact.exists(), source + ": " + built.stderr)
            ran = subprocess.run([str(artifact)], cwd=temp, capture_output=True, text=True, timeout=15)
            require(ran.returncode == 0, source + ": runtime " + str(ran.returncode) + ran.stderr)
        source = "callable_factory_definition_journal.tk"
        normal = run(source, "--check-only")
        shadow = run(source, "--check-only", "--call-transfer-shadow=json")
        require(normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and
                normal.stderr.count("error[E0408]") == 1, "definition journal changed diagnostics")
        transactions = json.loads(shadow.stdout)["transactions"]
        leaf = [t for t in transactions if t["callee"] == "leaf" and
                t["location"]["file"].endswith(source)]
        require(len(leaf) == 1 and leaf[0]["commit_allowed"],
                "completed ordinary definition disappeared with its generic caller journal")
        source = "lib/core/callable_assignment_fault.tk"
        for flags, suffix in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
            ordinary = Path(temp) / ("disposition-positive" + suffix)
            result = run(source, *flags, "-o", str(ordinary))
            require(result.returncode == 0 and ordinary.is_file(), result.stderr)
            for fault in ("missing", "mismatch"):
                artifact = Path(temp) / ("disposition-" + fault + suffix)
                result = run(source, *flags, "--stage1-callable-assignment-fault=" + fault,
                             "-o", str(artifact))
                require(result.returncode != 0 and "error[E0701]" in result.stderr and
                        "missing or inconsistent Sema callable assignment disposition" in result.stderr and
                        not artifact.exists(), "Disposition fault did not fail closed: " + result.stderr)
    print("binding directed core: 21 fixtures plus disposition positive/fault matrix passed")
    print("callable declaration/return increment: 7 runtime/parity, 8 rejection/parity, 16 no-artifact checks, definition journal preservation passed")


if __name__ == "__main__":
    main()
