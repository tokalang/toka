#!/usr/bin/env python3

"""Verify that four release-gate reports qualify one exact candidate."""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import re
try:
    import release_platform_policy as platforms
except ModuleNotFoundError:
    from tools.scripts import release_platform_policy as platforms


TARGETS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64")
STAGES = (
    "build", "pass", "fail", "warn", "semantic_replay", "cache_invalidation",
    "tooling", "incremental", "native_build_reference", "qslite", "async",
    "sanitizer", "package_smoke",
)

MIN_CTEST_PASSED = 15
MIN_CONFORMANCE_PASSED = 298
PROFILE_PATH = Path(__file__).resolve().parents[2] / "spec/restricted_cancellation_profile.v1.json"


def integer_count(counts, key):
    value = counts.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    return value


def count_errors(stage, target):
    name = stage.get("name")
    counts = stage.get("counts")
    if not isinstance(counts, dict):
        return ["%s: stage %s counts are missing" % (target, name)]
    if name == "build":
        ctest = counts.get("ctest")
        if not isinstance(ctest, dict):
            return ["%s: build CTest counts are missing" % target]
        failed = integer_count(ctest, "failed")
        passed = integer_count(ctest, "passed")
        if failed != 0 or passed is None or passed < MIN_CTEST_PASSED:
            return ["%s: build CTest baseline is not satisfied" % target]
    if name == "pass":
        suite = counts.get("pass_suite")
        conformance = counts.get("conformance")
        errors = []
        if not isinstance(suite, dict) or \
                integer_count(suite, "failed") != 0:
            errors.append("%s: pass-suite counts are missing or failed" % target)
        conformance_failed = integer_count(conformance, "failed") \
            if isinstance(conformance, dict) else None
        conformance_passed = integer_count(conformance, "passed") \
            if isinstance(conformance, dict) else None
        if conformance_failed != 0 or conformance_passed is None or \
                conformance_passed < MIN_CONFORMANCE_PASSED:
            errors.append("%s: conformance baseline is not satisfied" % target)
        return errors
    return []


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
        errors.extend(count_errors(stage, target))
        if version_label.startswith('v0.13.') and stage.get('name')=='package_smoke':
            control=stage.get('counts',{}).get('candidate_013',{})
            if type(control.get('version')) is not int or control.get('version') != 1 or control.get('schema')!='toka.0.13-candidate-controls' or control.get('result')!='pass' or control.get('candidate_revision')!=revision or control.get('version_label')!=version_label or control.get('build_testing') is not False or control.get('groups')!=['A1','B1','B1-boundaries','B1-relative','D1-D2']:
                errors.append(target+': required 0.13 installed candidate controls missing or mismatched')
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


def restricted_cancellation_errors(document, target, revision, profile):
    errors = []
    if document.get("schema") != "toka.restricted-cancellation-profile-conformance" or document.get("version") != 1:
        errors.append("%s: unsupported restricted cancellation conformance schema" % target)
    if document.get("candidate_revision") != revision or document.get("base_revision") != revision:
        errors.append("%s: restricted cancellation revision does not match candidate" % target)
    if document.get("is_dirty") is not False or document.get("result") != "candidate-pass":
        errors.append("%s: restricted cancellation evidence is dirty or incomplete" % target)
    binding = document.get("profile", {})
    digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if binding.get("schema") != profile["schema"] or binding.get("version") != profile["version"] or \
            binding.get("path") != "spec/restricted_cancellation_profile.v1.json" or binding.get("canonical_sha256") != digest:
        errors.append("%s: restricted cancellation profile binding is invalid" % target)
    compiler = document.get("compiler", {})
    if any(not isinstance(compiler.get(key), str) or not re.fullmatch(r"[0-9a-f]{64}", compiler[key])
           for key in ("tokac_sha256", "runtime_object_sha256")):
        errors.append("%s: restricted cancellation compiler/runtime binding is missing" % target)
    evidence = document.get("evidence", [])
    required = {("source", item["path"]): set(item["guarantee_ids"]) for item in profile["source_evidence"]}
    required.update({("native", item["target"]): set(item["guarantee_ids"]) for item in profile["native_evidence"]})
    seen = set()
    guarantees = set()
    for item in evidence:
        if item.get("kind") not in ("source", "native"):
            continue
        key = (item["kind"], item.get("path") if item["kind"] == "source" else item.get("target"))
        if key in seen or item.get("result") != "pass":
            errors.append("%s: restricted cancellation evidence is duplicated or incomplete" % target)
        if set(item.get("guarantee_ids", [])) != required.get(key):
            errors.append("%s: restricted cancellation guarantee mapping is invalid" % target)
        seen.add(key)
        guarantees.update(item.get("guarantee_ids", []))
    if seen != set(required) or guarantees != {item["id"] for item in profile["guarantees"]}:
        errors.append("%s: restricted cancellation required coverage is incomplete" % target)
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--version-label", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument('--source-run-id', type=int)
    parser.add_argument('--source-run-attempt', type=int)
    parser.add_argument('--optional-status', type=Path)
    args = parser.parse_args()
    targets = platforms.core_targets(args.version_label)

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
        if args.version_label.startswith('v0.13.'):
            reports[-1]['candidate_013']=next((stage.get('counts',{}).get('candidate_013') for stage in report.get('stages',[]) if stage.get('name')=='package_smoke'),None)

    missing = sorted(set(targets) - set(seen))
    unexpected = sorted(set(seen) - set(targets))
    if missing:
        errors.append("missing target reports: " + ", ".join(missing))
    if unexpected:
        errors.append("unexpected target reports: " + ", ".join(unexpected))
    if len(seen) != len(targets):
        errors.append("expected exactly %d target reports" % len(targets))

    conformance_digests = set()
    for target in targets:
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

    profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    restricted_conformances = []
    for target in targets:
        paths = list(args.evidence_dir.rglob("toka-restricted-cancellation-%s.json" % target))
        if len(paths) != 1:
            errors.append("expected one restricted cancellation conformance record for %s" % target)
            continue
        try:
            document = json.loads(paths[0].read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            errors.append("%s: cannot read restricted cancellation conformance: %s" % (target, error))
            continue
        errors.extend(restricted_cancellation_errors(document, target, args.revision, profile))
        restricted_conformances.append({"path": str(paths[0]), "target": target,
                                       "profile_sha256": document.get("profile", {}).get("canonical_sha256"),
                                       "result": document.get("result")})

    summary = {
        "schema": "toka.release-qualification-summary",
        "version": 1,
        "candidate_revision": args.revision,
        "version_label": args.version_label,
        "expected_targets": list(targets),
        "reports": reports,
        "taskhandle_conformance": conformances,
        "restricted_cancellation_conformance": restricted_conformances,
        "errors": errors,
        "result": "pass" if not errors else "fail",
    }
    if platforms.modern(args.version_label):
        try:
            optional = json.loads(args.optional_status.read_text()) if args.optional_status else platforms.not_run(args.revision,args.version_label)
        except (OSError,ValueError) as error:
            optional = None
            errors.append('cannot read optional target status: '+str(error))
        summary.update(version=2,policy_id=platforms.policy_id(args.version_label),expected_core_targets=list(targets),
                       optional_targets={platforms.OPTIONAL:optional},source_run_id=args.source_run_id,source_run_attempt=args.source_run_attempt)
        errors.extend(platforms.summary_errors(dict(summary,result='pass',errors=[]),args.revision,args.version_label))
        summary['result']='pass' if not errors else 'fail'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
