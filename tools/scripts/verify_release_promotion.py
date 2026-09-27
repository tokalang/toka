#!/usr/bin/env python3
"""Bind a draft release, qualification run, replay receipt and assets."""

import argparse
import hashlib
import json
from pathlib import Path
import re


TARGETS = ("linux-arm64", "linux-x64", "macos-arm64", "macos-x64")
TAG = re.compile(r"v0\.10\.(?:0|[1-9][0-9]*)\Z")
SHA = re.compile(r"[0-9a-f]{40}\Z")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")


def read_json(path, errors):
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        errors.append("cannot read %s: %s" % (path.name, error))
        return {}
    if not isinstance(document, dict):
        errors.append("%s must contain a JSON object" % path.name)
        return {}
    return document


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_errors(document, run_id, revision, workflow, require_candidate_head=True):
    errors = []
    if document.get("id") != run_id or document.get("status") != "completed" or \
            document.get("conclusion") != "success" or \
            (require_candidate_head and document.get("head_sha") != revision):
        errors.append("%s run does not bind the successful candidate" % workflow)
    path = document.get("path")
    if not isinstance(path, str) or path.split("@", 1)[0] != \
            ".github/workflows/%s.yml" % workflow:
        errors.append("%s run has the wrong workflow" % workflow)
    if workflow == "qualified_artifact_replay" and \
            document.get("event") != "workflow_dispatch":
        errors.append("replay run was not manually dispatched")
    return errors


def validate(args):
    errors = []
    if not TAG.fullmatch(args.tag_name):
        errors.append("tag is not a canonical v0.10.x release")
    if not SHA.fullmatch(args.candidate_sha):
        errors.append("candidate SHA is invalid")
    if args.qualification_run_id <= 0 or args.replay_run_id <= 0:
        errors.append("run IDs must be positive")

    draft = read_json(args.draft_json, errors)
    qualification_run = read_json(args.qualification_run_json, errors)
    replay_run = read_json(args.replay_run_json, errors)
    summary = read_json(args.qualification_summary, errors)
    receipt = read_json(args.replay_receipt, errors)

    if draft.get("tagName") != args.tag_name or \
            draft.get("isDraft") is not True or draft.get("isPrerelease") is not False:
        errors.append("release is not an unpublished full-release draft")
    if draft.get("isLatest") is True:
        errors.append("draft was marked Latest")
    expected_archives = tuple("toka-%s-%s.tar.gz" % (args.tag_name, target)
                              for target in TARGETS)
    expected_assets = set(expected_archives) | {"SHA256SUMS"}
    asset_rows = draft.get("assets")
    draft_names = [row.get("name") for row in asset_rows
                   if isinstance(row, dict) and isinstance(row.get("name"), str)] \
        if isinstance(asset_rows, list) else []
    if len(draft_names) != len(expected_assets) or set(draft_names) != expected_assets:
        errors.append("draft assets do not match the exact four-target set")

    errors.extend(run_errors(qualification_run, args.qualification_run_id,
                             args.candidate_sha, "release"))
    errors.extend(run_errors(replay_run, args.replay_run_id,
                             args.candidate_sha, "qualified_artifact_replay",
                             require_candidate_head=False))

    expected_targets = summary.get("expected_targets")
    if summary.get("schema") != "toka.release-qualification-summary" or \
            summary.get("version") != 1 or summary.get("result") != "pass" or \
            summary.get("errors") != [] or \
            summary.get("candidate_revision") != args.candidate_sha or \
            summary.get("version_label") != args.tag_name or \
            not isinstance(expected_targets, list) or \
            len(expected_targets) != len(TARGETS) or \
            any(not isinstance(target, str) for target in expected_targets) or \
            set(expected_targets) != set(TARGETS):
        errors.append("four-target qualification summary does not match")
    reports = summary.get("reports")
    if not isinstance(reports, list) or len(reports) != 4 or \
            {row.get("target") for row in reports if isinstance(row, dict)} != set(TARGETS) or \
            any(not isinstance(row, dict) or row.get("result") != "pass" for row in reports):
        errors.append("qualification reports are incomplete")
    conformances = summary.get("taskhandle_conformance")
    if not isinstance(conformances, list) or len(conformances) != 4 or \
            {row.get("target") for row in conformances if isinstance(row, dict)} != set(TARGETS) or \
            any(not isinstance(row, dict) or row.get("result") != "pass"
                for row in conformances):
        errors.append("TaskHandle conformance receipts are incomplete")

    if receipt.get("schema") != "toka.qualified-artifact-replay" or \
            receipt.get("version") != 1 or receipt.get("result") != "pass" or \
            receipt.get("target") != "macos-x64" or \
            receipt.get("candidate_revision") != args.candidate_sha or \
            receipt.get("version_label") != args.tag_name or \
            receipt.get("qualification_run_id") != args.qualification_run_id or \
            receipt.get("asset_source") not in ("qualified_run", "candidate_run"):
        errors.append("replay receipt does not bind the candidate and qualification")

    try:
        actual_names = {path.name for path in args.assets_dir.iterdir() if path.is_file()}
    except OSError as error:
        errors.append("cannot read release assets: %s" % error)
        actual_names = set()
    if actual_names != expected_assets:
        errors.append("downloaded assets do not match the exact four-target set")
    if actual_names == expected_assets:
        manifest = "".join("%s  %s\n" %
                           (sha256(args.assets_dir / name), name)
                           for name in sorted(expected_archives))
        if (args.assets_dir / "SHA256SUMS").read_text(encoding="utf-8") != manifest:
            errors.append("SHA256SUMS does not match downloaded archives")
        archive_digest = sha256(args.assets_dir /
                                ("toka-%s-macos-x64.tar.gz" % args.tag_name))
        recorded_digest = receipt.get("archive_sha256")
        if not isinstance(recorded_digest, str) or \
                not DIGEST.fullmatch(recorded_digest) or \
                recorded_digest != archive_digest:
            errors.append("replay receipt does not match the macOS x64 archive")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag-name", required=True)
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--qualification-run-id", required=True, type=int)
    parser.add_argument("--replay-run-id", required=True, type=int)
    for name in ("draft-json", "qualification-run-json", "replay-run-json",
                 "qualification-summary", "replay-receipt", "assets-dir", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    errors = validate(args)
    result = {"schema": "toka.release-promotion-verification", "version": 1,
              "tag_name": args.tag_name, "candidate_revision": args.candidate_sha,
              "qualification_run_id": args.qualification_run_id,
              "replay_run_id": args.replay_run_id,
              "result": "fail" if errors else "pass", "errors": errors}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
