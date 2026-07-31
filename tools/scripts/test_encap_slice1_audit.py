#!/usr/bin/env python3
"""Regression evidence for the audit-only @encap epoch Slice 1 data model."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def run(*args: str, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(args, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (completed.returncode == 0) != expect_success:
        outcome = "unexpectedly succeeded" if completed.returncode == 0 else "failed"
        raise RuntimeError("command %s:\n$ %s\n%s" %
                           (outcome, " ".join(args), completed.stderr))
    return completed


def facts(source: Path) -> dict[str, object]:
    completed = run(str(TOKAC), "--encap-slice1-facts=json", "-I", str(ROOT / "lib"),
                    str(source))
    return json.loads(completed.stdout)


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    baseline = facts(ROOT / "tests" / "conformance" / "ownership" /
                     "handle_payload_permission.tk")
    assert baseline["schema"] == "toka.encap-slice1"
    assert baseline["version"] == 1
    for name in (
            "resource_contract_fact_count", "drop_plan_fact_count",
            "partial_move_plan_fact_count", "copy_proof_fact_count",
            "copy_witness_fact_count", "dup_provider_fact_count"):
        assert baseline[name] > 0
    assert baseline["policy_fact_count"] > 0

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        marker = root / "marker.tk"
        marker.write_text(
            "shape Marker<T>(value: T)\n"
            "impl<T> Marker<T>@Send {}\n"
            "fn main() -> i32 {\n"
            "    auto one = Marker<i32>(value = 1)\n"
            "    auto two = Marker<i32>(value = 2)\n"
            "    return 0\n"
            "}\n", encoding="utf-8")
        marker_facts = facts(marker)
        assert marker_facts["copy_proof_fact_count"] > baseline["copy_proof_fact_count"]
        assert marker_facts["generic_impl_instance_count"] > baseline[
            "generic_impl_instance_count"]

        dup = root / "dup.tk"
        dup.write_text(
            "pub trait @Dup {}\n"
            "shape Valid(raw: i32)\n"
            "impl Valid@encap { pub raw fn drop(self#) {} }\n"
            "impl Valid@Dup { pub fn dup(self) -> Self { return Valid(raw = self.raw) } }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        dup_facts = facts(dup)
        assert dup_facts["dup_invalid_candidate_count"] == 0

        invalid_dup = root / "invalid_dup.tk"
        invalid_dup.write_text(
            "pub trait @Dup {}\n"
            "shape Invalid()\n"
            "impl Invalid@Dup { pub fn dup(self#) -> i32 { return 0 } }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        rejected = run(str(TOKAC), "-I", str(ROOT / "lib"), str(invalid_dup),
                       expect_success=False)
        assert "E0406" in rejected.stderr

        for name, trait in (("dyn_encap.tk", "encap"), ("dyn_copy.tk", "Copy")):
            source = root / name
            source.write_text(
                "fn reject(value: dyn @%s) -> i32 { return 0 }\n"
                "fn main() -> i32 { return 0 }\n" % trait, encoding="utf-8")
            failed = run(str(TOKAC), "-I", str(ROOT / "lib"), str(source),
                         expect_success=False)
            assert "E0617" in failed.stderr

    print("encap Slice 1 data-model audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, json.JSONDecodeError) as error:
        print("encap Slice 1 data-model audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
