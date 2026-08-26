#!/usr/bin/env python3

"""Inventory RC9 M0 ownership effects without defining language semantics."""

import json
from pathlib import Path
import re
import argparse


ROOT = Path(__file__).resolve().parents[2]


def matches(paths, pattern, excluded=()):
    expression = re.compile(pattern)
    entries = []
    for relative in paths:
        path = ROOT / relative
        for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1):
            if expression.search(line) and not any(token in line for token in excluded):
                entries.append({
                    "file": relative,
                    "line": line_number,
                    "text": line.strip(),
                })
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--details", action="store_true",
        help="include every discovered source location in the JSON receipt",
    )
    args = parser.parse_args()

    codegen = sorted(
        str(path.relative_to(ROOT))
        for path in (ROOT / "src/CodeGen").glob("*.cpp")
    )
    sema = sorted(
        str(path.relative_to(ROOT))
        for path in (ROOT / "src/Sema").glob("*.cpp")
    )

    dimensions = {
        "transfer_decision": matches(
            sema + ["include/toka/AST.h"],
            r"AggregateTransferKind|(?:->|\.)markMoved\(",
        ),
        "source_invalidation": matches(
            codegen + sema,
            r"suppressDropForPartialMove|PALCheckerState\.markMoved|"
            r"ConstantPointerNull|ConstantAggregateZero",
        ),
        "drop_liability": matches(
            codegen + ["include/toka/CodeGen.h"],
            r"suppressDropForMove\(|emitAcquire\(|emitRelease\(|"
            r"DropFlag|DropMask",
            excluded=("void suppressDropForMove", "void emitAcquire",
                      "void emitRelease"),
        ),
    }

    suppress_calls = matches(
        codegen, r"suppressDropForMove\(",
        excluded=("void CodeGen::suppressDropForMove",),
    )
    aggregate_plan = matches(
        codegen + sema + ["include/toka/AST.h"],
        r"AggregateTransferKind|applyAggregateTransfer|"
        r"qualifyAggregateTransfer",
    )

    errors = []
    if len(suppress_calls) != 10:
        errors.append(
            "RC8 baseline expected 10 suppressDropForMove call sites, found %d"
            % len(suppress_calls)
        )
    if not aggregate_plan:
        errors.append("aggregate transfer reference path is missing")
    for name, entries in dimensions.items():
        if not entries:
            errors.append("ownership dimension has no evidence: " + name)

    receipt = {
        "schema": "toka.rc9-m0.ownership-audit",
        "version": 1,
        "status": "pass" if not errors else "fail",
        "normative": False,
        "baseline": "997713f4828b43a5b82aa3363d99a37e9e6f2417",
        "suppress_drop_call_count": len(suppress_calls),
        "suppress_drop_calls": suppress_calls,
        "aggregate_plan_evidence": aggregate_plan,
        "dimensions": dimensions,
        "errors": errors,
    }
    if not args.details:
        receipt = {
            "schema": receipt["schema"],
            "version": receipt["version"],
            "status": receipt["status"],
            "normative": receipt["normative"],
            "baseline": receipt["baseline"],
            "suppress_drop_call_count": receipt["suppress_drop_call_count"],
            "aggregate_plan_evidence_count": len(aggregate_plan),
            "dimension_counts": {
                name: len(entries) for name, entries in dimensions.items()
            },
            "errors": errors,
        }
    print(json.dumps(receipt, sort_keys=True, separators=(",", ":")))
    raise SystemExit(0 if not errors else 1)


if __name__ == "__main__":
    main()
