#!/usr/bin/env python3
"""Positive and fail-closed promotion evidence checks without GitHub access."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import verify_release_promotion as verifier


ROOT = Path(__file__).resolve().parents[2]
SHA = "a" * 40
TAG = "v0.10.0"
TARGETS = verifier.TARGETS


def write_json(path, document):
    path.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")


def fixture(root):
    assets = root / "assets"
    assets.mkdir()
    archives = ["toka-%s-%s.tar.gz" % (TAG, target) for target in TARGETS]
    for name in archives:
        (assets / name).write_bytes(name.encode("utf-8"))
    (assets / "SHA256SUMS").write_text("".join(
        "%s  %s\n" % (hashlib.sha256((assets / name).read_bytes()).hexdigest(), name)
        for name in sorted(archives)), encoding="utf-8")
    documents = {
        "draft": {"tagName": TAG, "isDraft": True, "isPrerelease": False,
                  "assets": [{"name": name} for name in archives + ["SHA256SUMS"]]},
        "qualification_run": {"id": 11, "status": "completed", "conclusion": "success",
                              "head_sha": SHA,
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
                   "--assets-dir", str(args.assets_dir), "--output", str(args.output)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        require(result.returncode == 0 and
                json.loads(args.output.read_text())["result"] == "pass",
                "valid promotion CLI invocation failed: " + result.stderr)

        for tag in ("v1.0.0-rc.13", "v0.10.01", "v0.11.0", "v0.10.0-rc.1"):
            args.tag_name = tag
            require(verifier.validate(args), "invalid tag was accepted: " + tag)
        args.tag_name = TAG
        for name, key, value in (
            ("draft", "isDraft", False),
            ("draft", "isPrerelease", True),
            ("draft", "isLatest", True),
            ("draft", "tagName", "v0.10.1"),
            ("qualification_run", "id", 99),
            ("qualification_run", "head_sha", "b" * 40),
            ("qualification_run", "conclusion", "failure"),
            ("qualification_run", "path", ".github/workflows/other.yml"),
            ("replay_run", "id", 13),
            ("replay_run", "status", "in_progress"),
            ("replay_run", "event", "push"),
            ("replay_run", "path", ".github/workflows/other.yml"),
            ("summary", "candidate_revision", "b" * 40),
            ("summary", "version_label", "v0.10.1"),
            ("summary", "errors", ["blocked"]),
            ("receipt", "candidate_revision", "b" * 40),
            ("receipt", "version_label", "v0.10.1"),
            ("receipt", "qualification_run_id", 13),
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

        args.replay_receipt.unlink()
        failed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        require(failed.returncode != 0 and
                json.loads(args.output.read_text())["result"] == "fail",
                "missing replay receipt was accepted")

    print("Release promotion evidence contract PASSED")


if __name__ == "__main__":
    main()
