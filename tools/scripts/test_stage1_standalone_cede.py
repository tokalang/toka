#!/usr/bin/env python3
"""Qualify standalone cede admission, rejection atomicity and statement cleanup."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_standalone_cede"
if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin/tokac"

    def invoke(source, *flags):
        return subprocess.run([str(tokac), *flags, str(FIXTURES / source)],
                              cwd=ROOT, capture_output=True, text=True, timeout=45)

    def parity(source, success):
        normal = invoke(source, "--check-only")
        shadow = invoke(source, "--non-call-transfer-shadow=json", "--check-only")
        require(normal.returncode == shadow.returncode and normal.stderr == shadow.stderr,
                source + " normal/shadow parity failed: " + repr((normal.returncode,
                        normal.stderr, shadow.returncode, shadow.stderr)))
        require((normal.returncode == 0) == success, source + ": " + normal.stderr)
        records = [record for record in json.loads(shadow.stdout)["records"]
                   if record["boundary"] == "standalone" and
                   record["location"]["file"].endswith(source)]
        return normal, records

    rejected = {
        "literal_rejected.tk": "ExplicitCedeRequiresSource",
        "temporary_rejected.tk": "ExplicitCedeRequiresSource",
        "nested_rejection_atomic.tk": "ExplicitCedeRequiresSource",
        "nested_cede_rejected.tk": "ExplicitCedeRequiresSource",
        "ordinary_parameter_rejected.tk": "SourceTransferUnauthorized",
        "unique_payload_rejected.tk": "DereferencedOwningPayload",
        "shared_payload_rejected.tk": "DereferencedOwningPayload",
        "alias_rejected.tk": "ActiveDerivedBorrow",
        "borrow_rejected.tk": "ActiveDerivedBorrow",
        "unknown_rejected.tk": "IncompleteFacts",
    }
    with tempfile.TemporaryDirectory(prefix="toka-standalone-cede-") as temp:
        for source, reason in rejected.items():
            normal, records = parity(source, False)
            require(normal.stderr.count("error[E04660]") == 1 and
                    reason in normal.stderr and "E0438" not in normal.stderr and
                    "E0410" not in normal.stderr,
                    source + " did not reject atomically: " + normal.stderr)
            require(len(records) == 1 and records[0]["plan"]["outcome"] == "Rejected" and
                    records[0]["plan"]["rejection"] == reason and
                    records[0]["plan"]["source"] == "NoStateChange" and
                    not records[0]["group_plan_admitted"],
                    source + " granted a rejected standalone plan")
            for flags, suffix in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                output = Path(temp) / (source + suffix)
                built = invoke(source, *flags, "-o", str(output))
                require(built.returncode != 0 and not output.exists(),
                        source + " emitted an artifact on rejection")

        normal, _ = parity("validation_rollback.tk", False)
        require("error[E04606]" in normal.stderr and "E0438" not in normal.stderr and
                "E0410" not in normal.stderr,
                "Normal validation failure leaked cede invalidation")
        normal, _ = parity("copy_use_after_discard.tk", False)
        require("error[E0438]" in normal.stderr,
                "Copy source remained live after explicit cede")

        for mode in ("fn", "dyn", "consuming_fn", "consuming_dyn"):
            source = mode + "_use_after_discard.tk"
            normal, records = parity(source, False)
            records.sort(key=lambda record: record["location"]["line"])
            require(normal.stderr.count("error[E04660]") == 1 and
                    "SourceNotLive" in normal.stderr and len(records) == 2,
                    source + " did not reject repeated discard: " + normal.stderr)
            first, second = [record["plan"] for record in records]
            require(first["outcome"] == "Admitted" and
                    first["source"] == "InvalidateBinding" and
                    second["outcome"] == "Rejected" and
                    second["rejection"] == "SourceNotLive" and
                    second["source"] == "NoStateChange" and
                    second["drop"] == "None",
                    source + " lost callable binding liveness")
            output = Path(temp) / (mode + ".o")
            built = invoke(source, "-c", "-o", str(output))
            require(built.returncode != 0 and not output.exists(),
                    source + " emitted a repeated-discard artifact")

        for source in ("exact_once.tk", "consuming_callable.tk", "callable_values.tk"):
            _, records = parity(source, True)
            artifact = Path(temp) / source.removesuffix(".tk")
            built = invoke(source, "-o", str(artifact))
            require(built.returncode == 0 and artifact.is_file(), built.stderr)
            executed = subprocess.run([str(artifact)], cwd=temp, capture_output=True,
                                      text=True, timeout=15)
            require(executed.returncode == 0,
                    source + " cleanup failed: " + repr((executed.returncode, executed.stderr)))
            if source == "exact_once.tk":
                plans = [record["plan"] for record in records]
                for production, source_action, drop in (
                        ("CopyValue", "InvalidateSubtree", "NoLiability"),
                        ("MoveOwned", "InvalidateSubtree", "NoLiability"),
                        ("MoveOwned", "InvalidateSubtree", "StatementEndAssumesLiability"),
                        ("MoveOwned", "InvalidateRoot", "StatementEndAssumesLiability"),
                        ("TransferShared", "InvalidateBinding", "StatementEndAssumesLiability")):
                    require(any(plan["value_production"] == production and
                                plan["source"] == source_action and plan["drop"] == drop
                                and plan["destination"] == "StatementEndDiscard"
                                and plan["outcome"] == "Admitted" for plan in plans),
                            "Missing standalone row: " + repr((production, source_action, drop)))
                require(any(plan["source_obligation_action"] == "DischargeToStatementDiscard"
                            for plan in plans), "Standalone cede did not discharge a parameter obligation")
            elif source == "consuming_callable.tk":
                require(not records,
                        "Consuming invocation was emitted as a standalone source discard")
            else:
                require(len(records) == 6, "Missing standalone callable value receipts")
                for record in records:
                    plan = record["plan"]
                    consuming = plan["actual_type"].startswith("cede ")
                    dynamic = "dyn fn" in plan["actual_type"]
                    require(plan["outcome"] == "Admitted" and
                            plan["source_view"] == "CallableIdentity" and
                            plan["source"] == "InvalidateBinding" and
                            plan["copy_proof"] == ("ProvenNonCopy" if consuming else "ProvenCopy") and
                            plan["value_production"] == ("MoveOwned" if consuming else "CopyIdentity") and
                            plan["drop"] == ("StatementEndAssumesLiability" if dynamic else "NoLiability") and
                            bool(plan["liability_identity"]) == dynamic and
                            plan["source_obligation_action"] ==
                                ("DischargeToStatementDiscard" if consuming else "None"),
                            "Incorrect standalone callable plan: " + repr(plan))
    print("stage1 standalone cede: pass (rejection atomicity, exact-once, callable isolation)")


if __name__ == "__main__":
    main()
