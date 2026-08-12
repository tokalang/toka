#!/usr/bin/env python3

"""Verify that four release-gate reports qualify one exact candidate."""

import argparse
import json
from pathlib import Path
import sys


TARGETS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64")
STAGES = (
    "build", "pass", "fail", "warn", "semantic_replay", "cache_invalidation",
    "tooling", "incremental", "native_build_reference", "qslite", "async",
    "sanitizer", "package_smoke",
)


def report_errors(report, revision, version_label):
    errors = []
    target = report.get("target", "<missing>")
    if report.get("schema") != "toka.release-gate" or report.get("version") != 2:
        errors.append("%s: unsupported release-gate schema" % target)
    if report.get("revision") != revision:
        errors.append("%s: revision does not match candidate" % target)
    if report.get("version_label") != version_label:
        errors.append("%s: version label does not match candidate" % target)
    if report.get("source_dirty") is not False:
        errors.append("%s: source_dirty is not false" % target)
    if report.get("result") != "pass":
        errors.append("%s: release gate result is not pass" % target)
    stages = report.get("stages")
    if not isinstance(stages, list):
        return errors + ["%s: stages are missing" % target]
    names = tuple(stage.get("name") for stage in stages)
    if names != STAGES:
        errors.append("%s: stage set or ordering changed" % target)
    for stage in stages:
        if stage.get("result") != "pass":
            errors.append("%s: stage %s is not pass" % (target, stage.get("name", "<missing>")))
    return errors


def conformance_errors(document, target, revision):
    errors = []
    if document.get("schema") != "toka.taskhandle-lifecycle-conformance" or document.get("version") != 1:
        errors.append("%s: unsupported TaskHandle conformance schema" % target)
    if document.get("candidate_revision") != revision:
        errors.append("%s: TaskHandle conformance revision does not match candidate" % target)
    if document.get("result") != "pass":
        errors.append("%s: TaskHandle conformance result is not pass" % target)
    contract = document.get("contract")
    if not isinstance(contract, dict) or contract.get("schema") != "toka.taskhandle-lifecycle" or \
            contract.get("version") != 2 or contract.get("path") != "spec/taskhandle_lifecycle.v2.json" or \
            not isinstance(contract.get("canonical_sha256"), str) or len(contract["canonical_sha256"]) != 64:
        errors.append("%s: TaskHandle conformance contract binding is invalid" % target)
    evidence = document.get("evidence")
    if not isinstance(evidence, list) or not evidence or any(item.get("result") != "pass" for item in evidence):
        errors.append("%s: TaskHandle conformance evidence is incomplete" % target)
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--version-label", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    errors = []
    reports = []
    conformances = []
    seen = {}
    for path in sorted(args.evidence_dir.rglob("release-gate-*.json")):
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            errors.append("%s: cannot read report: %s" % (path, error))
            continue
        target = report.get("target")
        if target in seen:
            errors.append("duplicate report for target: %s" % target)
            continue
        seen[target] = path
        reports.append({"path": str(path), "target": target,
                        "result": report.get("result")})
        errors.extend(report_errors(report, args.revision, args.version_label))

    missing = sorted(set(TARGETS) - set(seen))
    unexpected = sorted(set(seen) - set(TARGETS))
    if missing:
        errors.append("missing target reports: " + ", ".join(missing))
    if unexpected:
        errors.append("unexpected target reports: " + ", ".join(unexpected))
    if len(seen) != len(TARGETS):
        errors.append("expected exactly four target reports")

    conformance_digests = set()
    for target in TARGETS:
        name = "taskhandle-lifecycle-conformance-%s.json" % target
        paths = list(args.evidence_dir.rglob(name))
        if len(paths) != 1:
            errors.append("expected one TaskHandle conformance record for %s" % target)
            continue
        try:
            document = json.loads(paths[0].read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            errors.append("%s: cannot read TaskHandle conformance: %s" % (target, error))
            continue
        errors.extend(conformance_errors(document, target, args.revision))
        contract = document.get("contract", {})
        conformance_digests.add(contract.get("canonical_sha256"))
        conformances.append({"path": str(paths[0]), "target": target,
                             "contract_sha256": contract.get("canonical_sha256"),
                             "result": document.get("result")})
    if len(conformance_digests) != 1 or None in conformance_digests:
        errors.append("TaskHandle conformance records do not bind one contract digest")

    summary = {
        "schema": "toka.release-qualification-summary",
        "version": 1,
        "candidate_revision": args.revision,
        "version_label": args.version_label,
        "expected_targets": list(TARGETS),
        "reports": reports,
        "taskhandle_conformance": conformances,
        "errors": errors,
        "result": "pass" if not errors else "fail",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
