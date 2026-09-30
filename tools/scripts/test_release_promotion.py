#!/usr/bin/env python3
"""Positive and fail-closed promotion evidence checks without GitHub access."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import verify_release_promotion as verifier


ROOT = Path(__file__).resolve().parents[2]
SHA = "a" * 40
TAG = "v0.11.0"
TARGETS = verifier.TARGETS


def write_json(path, document):
    path.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")


def fixture(root):
    assets = root / "assets"
    assets.mkdir()
    archives = ["toka-%s-%s.tar.gz" % (TAG, target) for target in TARGETS]
    for name in archives:
        (assets / name).write_bytes(name.encode("utf-8"))
    qualified = root / "qualification-archives"
    qualified.mkdir()
    for target, name in zip(TARGETS, archives):
        artifact = qualified / ("release-archive-" + target)
        artifact.mkdir()
        shutil.copy2(assets / name, artifact / name)
    (assets / "SHA256SUMS").write_text("".join(
        "%s  %s\n" % (hashlib.sha256((assets / name).read_bytes()).hexdigest(), name)
        for name in sorted(archives)), encoding="utf-8")
    documents = {
        "draft": {"tagName": TAG, "isDraft": True, "isPrerelease": False,
                  "assets": [{"name": name} for name in archives + ["SHA256SUMS"]]},
        "qualification_run": {"id": 11, "status": "completed", "conclusion": "success",
                              "head_sha": SHA, "event": "push",
                              "path": ".github/workflows/release.yml"},
        "replay_run": {"id": 12, "status": "completed", "conclusion": "success",
                       "head_sha": "b" * 40, "event": "workflow_dispatch",
                       "path": ".github/workflows/qualified_artifact_replay.yml"},
        "summary": {"schema": "toka.release-qualification-summary", "version": 1,
                    "result": "pass", "candidate_revision": SHA,
                    "version_label": TAG, "errors": [], "expected_targets": list(TARGETS),
                    "reports": [{"target": target, "result": "pass"} for target in TARGETS],
                    "taskhandle_conformance": [
                        {"target": target, "result": "pass"} for target in TARGETS]},
        "receipt": {"schema": "toka.qualified-artifact-replay", "version": 1,
                    "result": "pass", "target": "macos-x64", "candidate_revision": SHA,
                    "version_label": TAG, "qualification_run_id": 11,
                    "asset_source": "qualified_run",
                    "archive_sha256": hashlib.sha256(
                        (assets / ("toka-%s-macos-x64.tar.gz" % TAG)).read_bytes()).hexdigest()},
    }
    for name, document in documents.items():
        write_json(root / (name + ".json"), document)
    args = argparse.Namespace(
        tag_name=TAG, candidate_sha=SHA, qualification_run_id=11,
        replay_run_id=12, draft_json=root / "draft.json",
        qualification_run_json=root / "qualification_run.json",
        replay_run_json=root / "replay_run.json", qualification_summary=root / "summary.json",
        replay_receipt=root / "receipt.json", assets_dir=assets,
        qualified_archives_dir=qualified,
        output=root / "verification.json")
    return args, documents


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    with tempfile.TemporaryDirectory(prefix="toka-release-promotion-test-") as temporary:
        root = Path(temporary)
        args, documents = fixture(root)
        require(not verifier.validate(args), "valid bound promotion evidence was rejected")
        command = [sys.executable, str(ROOT / "tools/scripts/verify_release_promotion.py"),
                   "--tag-name", TAG, "--candidate-sha", SHA,
                   "--qualification-run-id", "11", "--replay-run-id", "12",
                   "--draft-json", str(args.draft_json),
                   "--qualification-run-json", str(args.qualification_run_json),
                   "--replay-run-json", str(args.replay_run_json),
                   "--qualification-summary", str(args.qualification_summary),
                   "--replay-receipt", str(args.replay_receipt),
                   "--assets-dir", str(args.assets_dir),
                   "--qualified-archives-dir", str(args.qualified_archives_dir),
                   "--output", str(args.output)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        verified = json.loads(args.output.read_text())
        require(result.returncode == 0 and
                verified["result"] == "pass" and
                verified["archive_source"] == "qualified_run" and
                set(verified["archives"]) == set(TARGETS) and
                all(row["draft_sha256"] == row["qualification_sha256"]
                    for row in verified["archives"].values()),
                "valid promotion CLI invocation failed: " + result.stderr)

        for target in TARGETS:
            (args.qualified_archives_dir / ("release-archive-" + target)).rename(
                args.qualified_archives_dir / ("candidate-archive-" + target))
        require(verifier.validate(args),
                "candidate archive family was accepted for a tag-push run")
        candidate_run = dict(documents["qualification_run"], event="workflow_dispatch")
        candidate_receipt = dict(documents["receipt"], asset_source="candidate_run")
        write_json(args.qualification_run_json, candidate_run)
        write_json(args.replay_receipt, candidate_receipt)
        candidate_result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        candidate_verified = json.loads(args.output.read_text())
        require(candidate_result.returncode == 0 and
                candidate_verified["archive_source"] == "candidate_run" and
                set(candidate_verified["archives"]) == set(TARGETS),
                "valid candidate-run archive family was rejected")
        for target in TARGETS:
            (args.qualified_archives_dir / ("candidate-archive-" + target)).rename(
                args.qualified_archives_dir / ("release-archive-" + target))
        require(verifier.validate(args),
                "tag archive family was accepted for a candidate-dispatch run")
        write_json(args.qualification_run_json, documents["qualification_run"])
        write_json(args.replay_receipt, documents["receipt"])

        for tag in ("v1.0.0-rc.13", "v0.11.01", "v0.10.0", "v0.11.0-rc.1"):
            args.tag_name = tag
            require(verifier.validate(args), "invalid tag was accepted: " + tag)
        args.tag_name = TAG
        for name, key, value in (
            ("draft", "isDraft", False),
            ("draft", "isPrerelease", True),
            ("draft", "isLatest", True),
            ("draft", "tagName", "v0.11.1"),
            ("qualification_run", "id", 99),
            ("qualification_run", "head_sha", "b" * 40),
            ("qualification_run", "conclusion", "failure"),
            ("qualification_run", "event", "pull_request"),
            ("qualification_run", "path", ".github/workflows/other.yml"),
            ("replay_run", "id", 13),
            ("replay_run", "status", "in_progress"),
            ("replay_run", "event", "push"),
            ("replay_run", "path", ".github/workflows/other.yml"),
            ("summary", "candidate_revision", "b" * 40),
            ("summary", "version_label", "v0.11.1"),
            ("summary", "errors", ["blocked"]),
            ("receipt", "candidate_revision", "b" * 40),
            ("receipt", "version_label", "v0.11.1"),
            ("receipt", "qualification_run_id", 13),
            ("receipt", "asset_source", "candidate_run"),
            ("receipt", "archive_sha256", "0" * 64),
        ):
            changed = dict(documents[name], **{key: value})
            write_json(root / (name + ".json"), changed)
            require(verifier.validate(args), "%s %s drift was accepted" % (name, key))
            write_json(root / (name + ".json"), documents[name])

        summary = dict(documents["summary"])
        summary["reports"] = summary["reports"][:-1]
        write_json(args.qualification_summary, summary)
        require(verifier.validate(args), "missing target report was accepted")
        write_json(args.qualification_summary, documents["summary"])

        draft = dict(documents["draft"])
        draft["assets"] = draft["assets"][:-1]
        write_json(args.draft_json, draft)
        require(verifier.validate(args), "missing draft asset was accepted")
        write_json(args.draft_json, documents["draft"])

        sums = args.assets_dir / "SHA256SUMS"
        original_sums = sums.read_text(encoding="utf-8")
        sums.write_text("0" * 64 + original_sums[64:], encoding="utf-8")
        require(verifier.validate(args), "wrong archive checksum was accepted")
        sums.write_text(original_sums, encoding="utf-8")

        for target in TARGETS:
            archive = args.assets_dir / ("toka-%s-%s.tar.gz" % (TAG, target))
            original_archive = archive.read_bytes()
            archive.write_bytes(original_archive + b"-replaced")
            sums.write_text("".join(
                "%s  %s\n" % (hashlib.sha256(path.read_bytes()).hexdigest(), path.name)
                for path in sorted(args.assets_dir.glob("toka-*.tar.gz"))),
                encoding="utf-8")
            if target == "macos-x64":
                changed_receipt = dict(documents["receipt"],
                    archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
                write_json(args.replay_receipt, changed_receipt)
            self_check = subprocess.run([
                sys.executable, str(ROOT / "tools/scripts/verify_release_assets.py"),
                "--assets-dir", str(args.assets_dir), "--version-label", TAG,
                "--checksums-output", str(sums), "--require-checksums",
            ], cwd=ROOT, capture_output=True, text=True)
            require(self_check.returncode == 0,
                    "recomputed draft checksum was not internally valid: " + target)
            rejected = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            rejection = json.loads(args.output.read_text())
            require(rejected.returncode != 0 and
                    "draft archive differs from qualification: " + target in
                    rejection["errors"] and
                    set(rejection["archives"]) == set(TARGETS),
                    "replaced draft archive was accepted: " + target)
            archive.write_bytes(original_archive)
            sums.write_text(original_sums, encoding="utf-8")
            write_json(args.replay_receipt, documents["receipt"])
            require(not verifier.validate(args),
                    "valid promotion control did not recover after " + target)

        missing_qualified = args.qualified_archives_dir / \
            "release-archive-linux-arm64" / ("toka-%s-linux-arm64.tar.gz" % TAG)
        missing_qualified.unlink()
        require(verifier.validate(args), "missing qualified archive was accepted")
        shutil.copy2(args.assets_dir / missing_qualified.name, missing_qualified)
        require(not verifier.validate(args), "qualified archive restoration failed")

        args.replay_receipt.unlink()
        failed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        require(failed.returncode != 0 and
                json.loads(args.output.read_text())["result"] == "fail",
                "missing replay receipt was accepted")

    print(json.dumps({
        "schema": "toka.release-promotion-contract-test",
        "result": "pass",
        "archive_source_controls": ["qualified_run", "candidate_run"],
        "replaced_draft_archives_rejected": list(TARGETS),
        "qualified_archive_sha256": {
            target: verified["archives"][target]["qualification_sha256"]
            for target in TARGETS},
    }, sort_keys=True))


if __name__ == "__main__":
    main()
