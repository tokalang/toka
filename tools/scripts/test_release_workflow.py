#!/usr/bin/env python3

"""Fail closed when qualification, draft creation, or promotion can drift."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/scripts"))
from release_gate import parse_counts
from classify_ci_changes import requires_heavy
WORKFLOW = ROOT / ".github/workflows/release.yml"
PROMOTION = ROOT / ".github/workflows/promote_release.yml"
INTEL_REPLAY = ROOT / ".github/workflows/rc8_macos_x64_draft_replay.yml"
INTEL_REPLAY_V2 = ROOT / ".github/workflows/rc8_macos_x64_qualified_artifact_replay.yml"
RC9_INTEL_REPLAY = ROOT / ".github/workflows/rc9_macos_x64_qualified_artifact_replay.yml"
QUALIFICATION = ROOT / "tools/scripts/verify_release_qualification.py"
ASSETS = ROOT / "tools/scripts/verify_release_assets.py"
RELEASE_GATE = ROOT / "tools/scripts/release_gate.py"
HANDLE_AUDIT = ROOT / "tools/scripts/audit_handle_grammar.py"
INSTALLER = ROOT / "tools/install.sh"
ACTIVE_CANDIDATE = "v1.0.0-rc.9"
ACTIVE_RELEASE_NOTES = ROOT / ("docs/release_notes_%s.md" % ACTIVE_CANDIDATE)
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


def shell_run_blocks(text):
    lines = text.splitlines()
    blocks = []
    index = 0
    while index < len(lines):
        stripped = lines[index].lstrip()
        if stripped != "run: |":
            index += 1
            continue
        run_indent = len(lines[index]) - len(stripped)
        body = []
        index += 1
        while index < len(lines):
            line = lines[index]
            if line.strip() and len(line) - len(line.lstrip()) <= run_indent:
                break
            body.append(line)
            index += 1
        blocks.append("\n".join(body))
    return blocks


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    require(result.returncode == 0, "command failed:\n%s%s" % (result.stdout, result.stderr))


def run_expect_failure(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    require(result.returncode != 0, "invalid input unexpectedly passed: " + " ".join(command))


def report(target, revision, label):
    stages = []
    for stage in STAGES:
        counts = {}
        if stage == "build":
            counts = {"ctest": {"passed": 15, "failed": 0, "total": 15}}
        elif stage == "pass":
            counts = {
                "pass_suite": {"passed": 412, "failed": 0},
                "conformance": {"passed": 298, "failed": 0},
            }
        stages.append({"name": stage, "result": "pass", "counts": counts})
    return {
        "schema": "toka.release-gate", "version": 2, "result": "pass",
        "target": target, "revision": revision, "version_label": label,
        "source_dirty": False,
        "stages": stages,
    }


def exercise_verifiers():
    with tempfile.TemporaryDirectory(prefix="toka-release-workflow-") as temp:
        root = Path(temp)
        evidence = root / "evidence"
        evidence.mkdir()
        revision = "a" * 40
        label = ACTIVE_CANDIDATE
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
        reduced = report("linux-x64", revision, label)
        reduced["stages"][1]["counts"]["conformance"]["passed"] = 297
        (evidence / "release-gate-linux-x64.json").write_text(
            json.dumps(reduced), encoding="utf-8")
        run_expect_failure([sys.executable, str(QUALIFICATION),
                            "--evidence-dir", str(evidence),
                            "--revision", revision, "--version-label", label,
                            "--output", str(root / "reduced-summary.json")])
        malformed = report("linux-x64", revision, label)
        malformed["stages"][0]["counts"]["ctest"]["passed"] = None
        (evidence / "release-gate-linux-x64.json").write_text(
            json.dumps(malformed), encoding="utf-8")
        run_expect_failure([sys.executable, str(QUALIFICATION),
                            "--evidence-dir", str(evidence),
                            "--revision", revision, "--version-label", label,
                            "--output", str(root / "malformed-summary.json")])
        (evidence / "release-gate-linux-x64.json").write_text(
            json.dumps(report("linux-x64", revision, label)), encoding="utf-8")
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
    build_linux = parse_counts(
        "build", "100% tests passed, 0 tests failed out of 15\n")
    require(build_linux == {
        "ctest": {"passed": 15, "failed": 0, "total": 15}},
        "release gate did not parse Linux CTest evidence")
    build_macos = parse_counts(
        "build", "100% tests passed out of 15\n")
    require(build_macos == {
        "ctest": {"passed": 15, "failed": 0, "total": 15}},
        "release gate did not parse macOS CTest evidence")
    build_failure = parse_counts(
        "build", "80% tests passed, 3 tests failed out of 15\n")
    require(build_failure == {
        "ctest": {"passed": 12, "failed": 3, "total": 15}},
        "release gate did not parse failed CTest evidence")
    build_malformed = parse_counts(
        "build", "80% tests passed out of 15\n")
    require(build_malformed == {},
        "release gate must fail-closed on malformed compact CTest output")
    pass_counts = parse_counts(
        "pass",
        "Summary:\n  Passed: 412\n  Failed: 0\n"
        "--- Conformance Suite Results: 298 Passed, 0 Failed ---\n",
    )
    require(pass_counts == {
        "pass_suite": {"passed": 412, "failed": 0},
        "conformance": {"passed": 298, "failed": 0},
    }, "release gate did not separate pass and Conformance evidence")

    text = WORKFLOW.read_text(encoding="utf-8")
    installer = INSTALLER.read_text(encoding="utf-8")
    pull_request_gate = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    windows_gate = (ROOT / ".github/workflows/windows-dogfood.yml").read_text(
        encoding="utf-8",
    )
    promotion = PROMOTION.read_text(encoding="utf-8")
    intel_replay = INTEL_REPLAY.read_text(encoding="utf-8")
    intel_replay_v2 = INTEL_REPLAY_V2.read_text(encoding="utf-8")
    rc9_intel_replay = RC9_INTEL_REPLAY.read_text(encoding="utf-8")
    release_gate = RELEASE_GATE.read_text(encoding="utf-8")
    handle_audit = HANDLE_AUDIT.read_text(encoding="utf-8")
    gate = job_block(text, "release-gate", "qualification-summary")
    summary = job_block(text, "qualification-summary", "create-draft-release")
    draft = job_block(text, "create-draft-release")

    require("publish_release" not in text,
            "manual qualification must not have an automatic publish switch")
    require("macos-15-intel" in intel_replay and
            "v1.0.0-rc.8" in intel_replay and
            "--require-checksums" in intel_replay and
            "contents: read" in intel_replay and
            "candidate_sha:" in intel_replay and
            "ref: v1.0.0-rc.8" in intel_replay and
            "refs/tags/v1.0.0-rc.8^{tag}" in intel_replay and
            "refs/tags/v1.0.0-rc.8^{commit}" in intel_replay and
            "docs/release_audits/v1.0.0-rc.8.md" not in intel_replay,
            "RC8 Intel replay workflow is missing the exact draft replay contract")
    require("macos-15-intel" in intel_replay_v2 and
            "actions: read" in intel_replay_v2 and
            "contents: read" in intel_replay_v2 and
            "contents: write" not in intel_replay_v2 and
            "qualification_run_id:" in intel_replay_v2 and
            "archive_sha256:" in intel_replay_v2 and
            "release-archive-macos-x64" in intel_replay_v2 and
            "actions/download-artifact@v4" in intel_replay_v2 and
            "github-token:" in intel_replay_v2 and
            "run-id:" in intel_replay_v2 and
            "gh release download" not in intel_replay_v2 and
            "shasum -a 256" in intel_replay_v2 and
            "refs/tags/v1.0.0-rc.8^{commit}" in intel_replay_v2 and
            "toka doctor" in intel_replay_v2 and
            "TOKA_OFFLINE=1 toka fetch" in intel_replay_v2 and
            "toka preview" in intel_replay_v2 and
            "rc8-macos-x64-qualified-artifact-replay" in intel_replay_v2 and
            "softprops/action-gh-release" not in intel_replay_v2,
            "RC8 qualified-artifact Intel replay is not fail-closed/read-only")
    require("macos-15-intel" in rc9_intel_replay and
            "actions: read" in rc9_intel_replay and
            "contents: read" in rc9_intel_replay and
            "contents: write" not in rc9_intel_replay and
            "qualification_run_id:" in rc9_intel_replay and
            "archive_sha256:" in rc9_intel_replay and
            "release-archive-macos-x64" in rc9_intel_replay and
            "actions/download-artifact@v4" in rc9_intel_replay and
            "github-token:" in rc9_intel_replay and
            "run-id:" in rc9_intel_replay and
            "gh release download" not in rc9_intel_replay and
            "shasum -a 256" in rc9_intel_replay and
            "refs/tags/v1.0.0-rc.9^{commit}" in rc9_intel_replay and
            "toka doctor" in rc9_intel_replay and
            "TOKA_OFFLINE=1 toka fetch" in rc9_intel_replay and
            "toka preview" in rc9_intel_replay and
            "rc9-macos-x64-qualified-artifact-replay" in rc9_intel_replay and
            "softprops/action-gh-release" not in rc9_intel_replay,
            "RC9 qualified-artifact Intel replay is not fail-closed/read-only")
    require('["python3", "tools/run_conformance.py", "--build-dir", build_dir]' in
            handle_audit,
            "Handle audit does not bind Conformance to its configured cold build")
    require('"tools/run_conformance.py",' in release_gate and
            '"--build-dir", str(build_dir)' in release_gate,
            "release gate does not pass its configured build directory to Conformance")
    require('"tools/scripts/audit_handle_grammar.py"' in release_gate and
            '"--quick", "--tokac", env["TOKAC"]' in release_gate and
            '"--build-dir", str(build_dir)' in release_gate,
            "release gate does not enforce the Handle/Place quick security gate")
    for workflow_name, workflow_text in (
        ("release", text), ("promotion", promotion)
    ):
        for block in shell_run_blocks(workflow_text):
            require("${{ inputs.tag_name" not in block and
                    "${{ inputs.first_hour_receipt" not in block and
                    "${{ github.ref_name" not in block and
                    "${{ steps.version.outputs.label" not in block and
                    "${{ steps.candidate.outputs" not in block,
                    workflow_name + " workflow interpolates context into shell")
    require("canonical RC tag" in text and "canonical RC tag" in promotion,
            "release workflows do not validate canonical RC tag names")
    require("SHA256SUMS" in installer and "EXPECTED_SHA256" in installer and
            "ACTUAL_SHA256" in installer and
            ("sha256sum" in installer and "shasum" in installer),
            "installer does not fail closed on the published archive checksum")
    require(not requires_heavy(("README.md", "docs/installation.md")) and
            requires_heavy(("README.md", "src/Sema/Sema.cpp")) and
            requires_heavy(()),
            "CI change classifier does not fail closed")
    require("documentation-only:" in pull_request_gate and
            "compiler-and-sdk:" in pull_request_gate and
            "pr-gate:" in pull_request_gate and
            "needs.change-scope.outputs.heavy" in pull_request_gate,
            "pull-request CI does not route documentation away from platform builds")
    require("change-scope:" in windows_gate and "windows-gate:" in windows_gate and
            "needs.change-scope.outputs.heavy" in windows_gate,
            "Windows CI does not skip documentation-only changes safely")
    require(ACTIVE_RELEASE_NOTES.is_file(),
            "active candidate is missing tag-release notes: " + str(ACTIVE_RELEASE_NOTES))
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
    require("Upload unpublished candidate archive" in gate and
            "github.event_name == 'workflow_dispatch'" in gate and
            "candidate-archive-${{ matrix.name }}" in gate,
            "manual qualification does not retain unpublished candidate archives")
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
