#!/usr/bin/env python3

"""Fail closed when qualification, draft creation, or promotion can drift."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/release.yml"
PROMOTION = ROOT / ".github/workflows/promote_release.yml"
QUALIFICATION = ROOT / "tools/scripts/verify_release_qualification.py"
ASSETS = ROOT / "tools/scripts/verify_release_assets.py"
TARGETS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64")
STAGES = (
    "build", "pass", "fail", "warn", "semantic_replay", "cache_invalidation",
    "tooling", "incremental", "native_build_reference", "qslite", "async",
    "sanitizer", "package_smoke",
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def job_block(text, name, next_name=None):
    marker = "  %s:\n" % name
    start = text.find(marker)
    require(start >= 0, "release workflow is missing job: " + name)
    end = len(text)
    if next_name:
        next_marker = "  %s:\n" % next_name
        end = text.find(next_marker, start + len(marker))
        require(end >= 0, "release workflow job ordering changed: " + next_name)
    return text[start:end]


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    require(result.returncode == 0, "command failed:\n%s%s" % (result.stdout, result.stderr))


def run_expect_failure(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    require(result.returncode != 0, "invalid input unexpectedly passed: " + " ".join(command))


def report(target, revision, label):
    return {
        "schema": "toka.release-gate", "version": 2, "result": "pass",
        "target": target, "revision": revision, "version_label": label,
        "source_dirty": False,
        "stages": [{"name": stage, "result": "pass"} for stage in STAGES],
    }


def exercise_verifiers():
    with tempfile.TemporaryDirectory(prefix="toka-release-workflow-") as temp:
        root = Path(temp)
        evidence = root / "evidence"
        evidence.mkdir()
        revision = "a" * 40
        label = "v1.0.0-rc.3"
        for target in TARGETS:
            (evidence / ("release-gate-%s.json" % target)).write_text(
                json.dumps(report(target, revision, label)), encoding="utf-8")
            (evidence / ("taskhandle-lifecycle-conformance-%s.json" % target)).write_text(
                json.dumps({
                    "schema": "toka.taskhandle-lifecycle-conformance", "version": 1,
                    "candidate_revision": revision, "result": "pass",
                    "contract": {"schema": "toka.taskhandle-lifecycle", "version": 2,
                                 "path": "spec/taskhandle_lifecycle.v2.json",
                                 "canonical_sha256": "b" * 64},
                    "evidence": [{"result": "pass"}],
                }), encoding="utf-8")
        summary = root / "summary.json"
        run([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
             "--revision", revision, "--version-label", label, "--output", str(summary)])
        summary_document = json.loads(summary.read_text(encoding="utf-8"))
        require(summary_document["result"] == "pass", "valid qualification reports were rejected")
        (evidence / "release-gate-linux-x64.json").write_text(
            json.dumps(report("linux-x64", "c" * 40, label)), encoding="utf-8")
        run_expect_failure([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
                            "--revision", revision, "--version-label", label,
                            "--output", str(root / "invalid-summary.json")])

        assets = root / "assets"
        assets.mkdir()
        for target in TARGETS:
            (assets / ("toka-%s-%s.tar.gz" % (label, target))).write_bytes(target.encode("utf-8"))
        checksums = assets / "SHA256SUMS"
        run([sys.executable, str(ASSETS), "--assets-dir", str(assets),
             "--version-label", label, "--checksums-output", str(checksums)])
        run([sys.executable, str(ASSETS), "--assets-dir", str(assets),
             "--version-label", label, "--checksums-output", str(checksums),
             "--require-checksums"])
        (assets / "unexpected.txt").write_text("not a release asset\n", encoding="utf-8")
        run_expect_failure([sys.executable, str(ASSETS), "--assets-dir", str(assets),
                            "--version-label", label, "--checksums-output", str(checksums),
                            "--require-checksums"])


def main():
    text = WORKFLOW.read_text(encoding="utf-8")
    promotion = PROMOTION.read_text(encoding="utf-8")
    gate = job_block(text, "release-gate", "qualification-summary")
    summary = job_block(text, "qualification-summary", "create-draft-release")
    draft = job_block(text, "create-draft-release")

    require("publish_release" not in text,
            "manual qualification must not have an automatic publish switch")
    require("softprops/action-gh-release" not in gate,
            "matrix gate must not publish a release directly")
    require("contents: read" in gate,
            "matrix gate must not receive release-write permission")
    require("name: release-gate-${{ matrix.name }}" in gate and
            "taskhandle-lifecycle-conformance-${{ matrix.name }}.json" in gate,
            "each matrix member must upload named gate evidence")
    require("name: release-archive-${{ matrix.name }}" in gate and
            "startsWith(github.ref, 'refs/tags/v')" in gate,
            "only a successful tag gate may upload release archives")
    require("needs: release-gate" in summary and "if: always()" in summary,
            "qualification summary must inspect all matrix evidence")
    require("verify_release_qualification.py" in summary and
            "--revision" in summary and "--version-label" in summary,
            "summary must verify exact revision and label")
    require("needs: qualification-summary" in draft and
            "needs.qualification-summary.result == 'success'" in draft,
            "draft creation must wait for a passing summary")
    require("verify_release_assets.py" in draft and "SHA256SUMS" in draft,
            "draft creation must verify exact archive names and checksums")
    require("softprops/action-gh-release@v3" in draft and "draft: true" in draft and
            "prerelease: true" in draft,
            "tag workflow must create a draft pre-release, not publish it")
    require("environment: release-publication" in promotion and
            "first_hour_receipt" in promotion,
            "promotion must require protected approval and a first-hour receipt")
    require("gh release view" in promotion and "--json assets" in promotion and
            "SHA256SUMS" in promotion and "--draft=false" in promotion and
            "--require-checksums" in promotion,
            "promotion must verify a draft and its downloaded assets before publication")
    exercise_verifiers()
    print("Release workflow qualification/draft/promotion gate PASSED")


if __name__ == "__main__":
    main()
