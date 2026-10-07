#!/usr/bin/env python3

"""Fail closed when qualification, draft creation, or promotion can drift."""

import json
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/scripts"))
from release_gate import parse_counts
from classify_ci_changes import requires_heavy
WORKFLOW = ROOT / ".github/workflows/release.yml"
PROMOTION = ROOT / ".github/workflows/promote_release.yml"
QUALIFIED_REPLAY = ROOT / ".github/workflows/qualified_artifact_replay.yml"
INTEL_REPLAY = ROOT / ".github/workflows/rc8_macos_x64_draft_replay.yml"
INTEL_REPLAY_V2 = ROOT / ".github/workflows/rc8_macos_x64_qualified_artifact_replay.yml"
RC9_INTEL_REPLAY = ROOT / ".github/workflows/rc9_macos_x64_qualified_artifact_replay.yml"
QUALIFICATION = ROOT / "tools/scripts/verify_release_qualification.py"
ASSETS = ROOT / "tools/scripts/verify_release_assets.py"
RELEASE_GATE = ROOT / "tools/scripts/release_gate.py"
HANDLE_AUDIT = ROOT / "tools/scripts/audit_handle_grammar.py"
INSTALLER = ROOT / "tools/install.sh"
ACTIVE_CANDIDATE = "v0.11.0"
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


def cancellation_receipt(revision):
    profile = json.loads((ROOT / "spec/restricted_cancellation_profile.v1.json").read_text())
    evidence = [dict(item, kind="source", result="pass") for item in profile["source_evidence"]]
    evidence += [dict(item, kind="native", result="pass") for item in profile["native_evidence"]]
    return {"schema": "toka.restricted-cancellation-profile-conformance", "version": 1,
            "candidate_revision": revision, "base_revision": revision, "is_dirty": False,
            "result": "candidate-pass", "compiler": {"tokac_sha256": "d" * 64,
            "runtime_object_sha256": "e" * 64}, "evidence": evidence,
            "profile": {"schema": profile["schema"], "version": profile["version"],
            "path": "spec/restricted_cancellation_profile.v1.json",
            "canonical_sha256": hashlib.sha256(json.dumps(profile, sort_keys=True,
                separators=(",", ":")).encode()).hexdigest()}}


def exercise_verifiers():
    with tempfile.TemporaryDirectory(prefix="toka-release-workflow-") as temp:
        root = Path(temp)
        invalid_gate = subprocess.run([
            sys.executable, str(RELEASE_GATE), "--target", "linux-x64",
            "--output", str(root / "invalid-gate.json"),
            "--version", "v0.11.01",
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(invalid_gate.returncode != 0 and
                "canonical v0.11.x tag" in invalid_gate.stderr and
                not (root / "invalid-gate.json").exists(),
                "release gate admitted a noncanonical label")
        invalid_package = subprocess.run([
            "bash", "tools/scripts/package_release.sh", "v0.11.01",
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(invalid_package.returncode != 0 and
                "canonical v0.11.x tag" in invalid_package.stderr,
                "release packager admitted a noncanonical label")
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
            (evidence / ("toka-restricted-cancellation-%s.json" % target)).write_text(
                json.dumps(cancellation_receipt(revision)), encoding="utf-8")
        summary = root / "summary.json"
        run([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
             "--revision", revision, "--version-label", label, "--output", str(summary)])
        summary_document = json.loads(summary.read_text(encoding="utf-8"))
        require(summary_document["result"] == "pass", "valid qualification reports were rejected")
        receipt_path = evidence / "toka-restricted-cancellation-linux-x64.json"
        for mutation in ("missing", "wrong-revision", "dirty", "incomplete", "skipped", "wrong-profile", "missing-runtime"):
            invalid = cancellation_receipt(revision)
            if mutation == "missing":
                receipt_path.unlink()
            else:
                if mutation == "wrong-revision": invalid["candidate_revision"] = "c" * 40
                elif mutation == "dirty": invalid["is_dirty"] = True
                elif mutation == "incomplete": invalid["result"] = "incomplete"
                elif mutation == "skipped": invalid["evidence"][0]["result"] = "skipped (unsupported environment)"
                elif mutation == "wrong-profile": invalid["profile"]["canonical_sha256"] = "0" * 64
                elif mutation == "missing-runtime": invalid["compiler"]["runtime_object_sha256"] = ""
                receipt_path.write_text(json.dumps(invalid), encoding="utf-8")
            run_expect_failure([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
                                "--revision", revision, "--version-label", label,
                                "--output", str(root / (mutation + "-cancellation-summary.json"))])
            receipt_path.write_text(json.dumps(cancellation_receipt(revision)), encoding="utf-8")
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
        (evidence / "release-gate-linux-x64.json").write_text(
            json.dumps(report("linux-x64", revision, "v0.11.1")), encoding="utf-8")
        run_expect_failure([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
                            "--revision", revision, "--version-label", label,
                            "--output", str(root / "wrong-label-summary.json")])
        (evidence / "release-gate-linux-x64.json").unlink()
        run_expect_failure([sys.executable, str(QUALIFICATION), "--evidence-dir", str(evidence),
                            "--revision", revision, "--version-label", label,
                            "--output", str(root / "missing-report-summary.json")])

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
        run_expect_failure([sys.executable, str(ASSETS), "--assets-dir", str(assets),
                            "--version-label", "v0.11.1", "--checksums-output", str(checksums),
                            "--require-checksums"])
        correct_manifest = checksums.read_text(encoding="utf-8")
        checksums.write_text("0" * 64 + correct_manifest[64:], encoding="utf-8")
        run_expect_failure([sys.executable, str(ASSETS), "--assets-dir", str(assets),
                            "--version-label", label, "--checksums-output", str(checksums),
                            "--require-checksums"])
        checksums.write_text(correct_manifest, encoding="utf-8")
        (assets / ("toka-%s-macos-x64.tar.gz" % label)).unlink()
        run_expect_failure([sys.executable, str(ASSETS), "--assets-dir", str(assets),
                            "--version-label", label, "--checksums-output", str(checksums),
                            "--require-checksums"])
        (assets / ("toka-%s-macos-x64.tar.gz" % label)).write_bytes(b"macos-x64")
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
    qualified_replay = QUALIFIED_REPLAY.read_text(encoding="utf-8")
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
    require('"tools/scripts/test_restricted_cancellation_profile.py"' in release_gate and
            '"--conformance-output", str(build_dir / ("toka-restricted-cancellation-%s.json" % args.target))' in release_gate and
            "build/toka-restricted-cancellation-${{ matrix.name }}.json" in gate,
            "release gate must execute and upload candidate-bound restricted cancellation evidence")
    for workflow_name, workflow_text in (
        ("release", text), ("promotion", promotion)
    ):
        for block in shell_run_blocks(workflow_text):
            require("${{ inputs.tag_name" not in block and
                    "${{ inputs.candidate_sha" not in block and
                    "${{ inputs.qualification_run_id" not in block and
                    "${{ inputs.replay_run_id" not in block and
                    "${{ github.ref_name" not in block and
                    "${{ steps.version.outputs.label" not in block and
                    "${{ steps.candidate.outputs" not in block,
                    workflow_name + " workflow interpolates context into shell")
    active_pattern = r"^v0\.(11|12|13)\.(0|[1-9][0-9]*)$"
    require(text.count(active_pattern) == 3 and
            promotion.count(active_pattern) == 1 and
            qualified_replay.count(r"^v0\.11\.(0|[1-9][0-9]*)$") == 1,
            "active workflows do not validate the same canonical v0.11.x tag")
    for label in ("v0.11.0", "v0.11.1", "v0.11.123", "v0.12.0", "v0.12.123", "v0.13.0", "v0.13.123"):
        require(re.fullmatch(active_pattern, label) is not None,
                "valid active release label was rejected: " + label)
    for label in ("v0.11.00", "v0.11.01", "v0.10.0", "v1.0.0-rc.13",
                  "v0.11.0-rc.1", "v0.11.0x"):
        require(re.fullmatch(active_pattern, label) is None,
                "invalid active release label was admitted: " + label)
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
    for name, block in (("matrix gate", gate), ("qualification summary", summary)):
        require("Verify exact source identity" in block and
                "fetch-depth: 0" in block and
                '[[ "$CANDIDATE_REF" =~ ^[0-9a-f]{40}$ ]]' in block and
                'git rev-parse "$GITHUB_REF^{tag}"' in block and
                'git rev-parse "$GITHUB_REF^{commit}"' in block,
                name + " does not bind a branch SHA or annotated tag")
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
    require("needs: [release-gate, platform-plan]" in summary and "if: always()" in summary,
            "qualification summary must inspect all matrix evidence")
    require("verify_release_qualification.py" in summary and
            "--revision" in summary and "--version-label" in summary,
            "summary must verify exact revision and label")
    require("needs: qualification-summary" in draft and
            "needs.qualification-summary.result == 'success'" in draft and
            "github.event_name == 'push'" in draft and
            "startsWith(github.ref, 'refs/tags/v0.11.')" in draft,
            "only a passing tag push may create a draft")
    require("verify_release_assets.py" in draft and "SHA256SUMS" in draft,
            "draft creation must verify exact archive names and checksums")
    require("softprops/action-gh-release@v3" in draft and "draft: true" in draft and
            "prerelease: false" in draft and "make_latest: false" in draft,
            "tag workflow must create an unpublished full-release draft")
    require("environment: release-publication" in promotion and
            "actions: read" in promotion and "contents: write" in promotion and
            "qualified-artifact-replay-${{ inputs.tag_name }}-" in promotion and "'macos-x64'" in promotion,
            "promotion must protect publication and download a replay receipt")
    require("archive_source=qualified_run" in promotion and
            "archive_source=candidate_run" in promotion and
            "pattern: release-archive-*" in promotion and
            "pattern: candidate-archive-*" in promotion and
            '--qualified-archives-dir "$archive_dir"' in promotion and
            promotion.count("run-id: ${{ inputs.qualification_run_id }}") >= 3,
            "promotion must download the selected four qualified archives")
    require("refs/tags/$TAG_NAME^{tag}" in qualified_replay and
            "refs/tags/$TAG_NAME^{commit}" in qualified_replay and
            "QUALIFICATION_RUN_ID" in qualified_replay and
            "ARCHIVE_SHA256" in qualified_replay and
            "gh api" in qualified_replay and "shasum -a 256" in qualified_replay and
            "qualified-artifact-replay-receipt.json" in qualified_replay,
            "qualified archive replay lost its candidate/run/archive binding")
    require("refs/tags/$TAG_NAME^{tag}" in promotion and
            "refs/tags/$TAG_NAME^{commit}" in promotion and
            "verify_release_qualification.py" in promotion and
            "verify_release_assets.py" in promotion and "--require-checksums" in promotion and
            "verify_release_promotion.py" in promotion and
            "release-promotion-verification-${{ inputs.tag_name }}" in promotion and
            "--draft=false --prerelease=false --latest" in promotion and
            "releases/latest" in promotion,
            "promotion must bind the candidate and publish only a verified full release")
    exercise_verifiers()
    run([sys.executable, str(ROOT / "tools/scripts/test_qualified_draft.py")])
    print("Release workflow qualification/draft/promotion gate PASSED")


if __name__ == "__main__":
    main()
