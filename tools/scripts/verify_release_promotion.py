#!/usr/bin/env python3
"""Bind a draft release, qualification run, replay receipt and assets."""

import argparse
import hashlib
import json
from pathlib import Path
import re
try:
    import release_platform_policy as platforms
except ModuleNotFoundError:
    from tools.scripts import release_platform_policy as platforms


TARGETS = ("linux-arm64", "linux-x64", "macos-arm64", "macos-x64")
TAG = re.compile(r"v0\.(?:11|12|13)\.(?:0|[1-9][0-9]*)\Z")
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


def validate(args, observed=None):
    errors = []
    if observed is None:
        observed = {}
    observed["archives"] = {}
    if platforms.modern(args.tag_name):
        from verify_release_policy_chain import validate_promotion
        if not SHA.fullmatch(args.candidate_sha):return ['invalid candidate SHA']
        return validate_promotion(args,observed)
    if not TAG.fullmatch(args.tag_name):
        errors.append("tag is not a canonical v0.11.x release")
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
    qualification_event = qualification_run.get("event")
    archive_source = {"push": "qualified_run",
                      "workflow_dispatch": "candidate_run"}.get(qualification_event)
    if archive_source is None:
        errors.append("qualification run is neither a tag push nor a candidate dispatch")
    else:
        observed["archive_source"] = archive_source

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
    if archive_source is not None and receipt.get("asset_source") != archive_source:
        errors.append("replay receipt names the wrong qualification archive source")

    draft_hashes = {}
    try:
        actual_names = {path.name for path in args.assets_dir.iterdir() if path.is_file()}
    except OSError as error:
        errors.append("cannot read release assets: %s" % error)
        actual_names = set()
    if actual_names != expected_assets:
        errors.append("downloaded assets do not match the exact four-target set")
    if actual_names == expected_assets:
        draft_hashes = {name: sha256(args.assets_dir / name)
                        for name in expected_archives}
        manifest = "".join("%s  %s\n" %
                           (draft_hashes[name], name)
                           for name in sorted(expected_archives))
        if (args.assets_dir / "SHA256SUMS").read_text(encoding="utf-8") != manifest:
            errors.append("SHA256SUMS does not match downloaded archives")
        archive_digest = draft_hashes["toka-%s-macos-x64.tar.gz" % args.tag_name]
        recorded_digest = receipt.get("archive_sha256")
        if not isinstance(recorded_digest, str) or \
                not DIGEST.fullmatch(recorded_digest) or \
                recorded_digest != archive_digest:
            errors.append("replay receipt does not match the macOS x64 archive")

    if archive_source is not None:
        prefix = "release-archive-" if archive_source == "qualified_run" \
            else "candidate-archive-"
        expected_directories = {prefix + target for target in TARGETS}
        try:
            directories = list(args.qualified_archives_dir.iterdir())
        except OSError as error:
            errors.append("cannot read qualification archives: %s" % error)
            directories = []
        if {path.name for path in directories} != expected_directories or \
                len(directories) != len(TARGETS):
            errors.append("qualification artifacts are not the exact four %s archives" %
                          archive_source)
        for target in TARGETS:
            name = "toka-%s-%s.tar.gz" % (args.tag_name, target)
            directory = args.qualified_archives_dir / (prefix + target)
            archive = directory / name
            if directory.is_symlink() or not directory.is_dir() or \
                    not archive.is_file() or archive.is_symlink() or \
                    {path.name for path in directory.iterdir()} != {name}:
                errors.append("qualification archive is missing or ambiguous: " + target)
                continue
            qualified_digest = sha256(archive)
            draft_digest = draft_hashes.get(name)
            observed["archives"][target] = {
                "artifact_name": prefix + target,
                "archive_name": name,
                "draft_sha256": draft_digest,
                "qualification_sha256": qualified_digest,
            }
            if draft_digest != qualified_digest:
                errors.append("draft archive differs from qualification: " + target)
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag-name", required=True)
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--qualification-run-id", required=True, type=int)
    parser.add_argument("--replay-run-id", required=True, type=int)
    for name in ("draft-json", "qualification-run-json", "replay-run-json",
                 "qualification-summary", "replay-receipt", "assets-dir",
                 "qualified-archives-dir", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument('--qualification-artifacts-json',type=Path)
    parser.add_argument('--artifact-zips-dir',type=Path)
    parser.add_argument('--optional-run-json',type=Path)
    args = parser.parse_args()
    observed = {}
    errors = validate(args, observed)
    result = {"schema": "toka.release-promotion-verification", "version": 1,
              "tag_name": args.tag_name, "candidate_revision": args.candidate_sha,
              "qualification_run_id": args.qualification_run_id,
              "replay_run_id": args.replay_run_id,
              "result": "fail" if errors else "pass", "errors": errors,
              "archive_source": observed.get("archive_source"),
              "archives": observed["archives"]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
