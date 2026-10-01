#!/usr/bin/env python3
"""Prepare or verify a draft from an immutable successful candidate run."""

import argparse
import json
from pathlib import Path
import shutil

from verify_release_promotion import TARGETS, TAG, SHA, run_errors, sha256


def validate(args):
    if not TAG.fullmatch(args.tag_name) or not SHA.fullmatch(args.candidate_sha) or \
            args.qualification_run_id <= 0:
        raise ValueError('invalid release tag or candidate SHA')
    run = json.loads(args.qualification_run_json.read_text())
    errors = run_errors(run, args.qualification_run_id, args.candidate_sha, 'release')
    if run.get('event') != 'workflow_dispatch' or \
            run.get('repository', {}).get('full_name') != args.repository or \
            run.get('head_repository', {}).get('full_name') != args.repository:
        errors.append('qualification must be a candidate dispatch in the same repository')
    summary = json.loads(args.qualification_summary.read_text())
    if summary.get('schema') != 'toka.release-qualification-summary' or \
            summary.get('version') != 1 or summary.get('result') != 'pass' or \
            summary.get('errors') != [] or \
            summary.get('candidate_revision') != args.candidate_sha or \
            summary.get('version_label') != args.tag_name or \
            sorted(summary.get('expected_targets', [])) != sorted(TARGETS):
        errors.append('qualification summary does not bind the four-target candidate')
    for key, result in [('reports', 'pass'), ('taskhandle_conformance', 'pass'),
                        ('restricted_cancellation_conformance', 'candidate-pass')]:
        rows = summary.get(key)
        if not isinstance(rows, list) or len(rows) != 4 or \
                any(not isinstance(row, dict) or row.get('result') != result for row in rows) or \
                {row.get('target') for row in rows if isinstance(row, dict)} != set(TARGETS):
            errors.append('incomplete qualification evidence: ' + key)
    directories = list(args.qualified_archives_dir.iterdir())
    if {p.name for p in directories} != {'candidate-archive-' + t for t in TARGETS}:
        errors.append('candidate artifacts must be the exact four-target set')
    archives = {}
    for target in TARGETS:
        directory = args.qualified_archives_dir / ('candidate-archive-' + target)
        name = 'toka-%s-%s.tar.gz' % (args.tag_name, target)
        archive = directory / name
        if directory.is_symlink() or not directory.is_dir() or \
                archive.is_symlink() or not archive.is_file() or \
                {p.name for p in directory.iterdir()} != {name}:
            errors.append('missing or ambiguous candidate archive: ' + target)
        else:
            archives[name] = sha256(archive)
    if errors:
        raise ValueError('; '.join(errors))
    if args.draft_json is not None:
        draft = json.loads(args.draft_json.read_text())
        names = [row.get('name') for row in draft.get('assets', [])]
        expected = set(archives) | {'SHA256SUMS'}
        if draft.get('tagName') != args.tag_name or draft.get('isDraft') is not True or \
                draft.get('isPrerelease') is not False or draft.get('isLatest') is True or \
                len(names) != len(expected) or set(names) != expected:
            raise ValueError('draft state or asset set does not match')
        if args.draft_assets_dir is None or \
                {p.name for p in args.draft_assets_dir.iterdir()} != expected:
            raise ValueError('downloaded draft assets are incomplete or unexpected')
        for name, digest in archives.items():
            path = args.draft_assets_dir / name
            if path.is_symlink() or not path.is_file() or sha256(path) != digest:
                raise ValueError('draft differs from qualified bytes: ' + name)
        manifest = ''.join('%s  %s\n' % (archives[n], n) for n in sorted(archives))
        checksum = args.draft_assets_dir / 'SHA256SUMS'
        if checksum.is_symlink() or not checksum.is_file() or checksum.read_text() != manifest:
            raise ValueError('draft checksums do not match qualified bytes')
    return archives


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag-name', required=True)
    parser.add_argument('--candidate-sha', required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--qualification-run-id', required=True, type=int)
    for name in ('qualification-run-json', 'qualification-summary',
                 'qualified-archives-dir', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--assets-dir', type=Path)
    parser.add_argument('--draft-json', type=Path)
    parser.add_argument('--draft-assets-dir', type=Path)
    args = parser.parse_args()
    if (args.draft_json is None) != (args.draft_assets_dir is None):
        parser.error('draft JSON and downloaded assets must be supplied together')
    try:
        archives = validate(args)
        if args.assets_dir is not None:
            args.assets_dir.mkdir(parents=True, exist_ok=False)
            for target in TARGETS:
                name = 'toka-%s-%s.tar.gz' % (args.tag_name, target)
                source = args.qualified_archives_dir / ('candidate-archive-' + target) / name
                shutil.copyfile(source, args.assets_dir / name)
                if sha256(args.assets_dir / name) != archives[name]:
                    raise ValueError('staged archive differs: ' + name)
            (args.assets_dir / 'SHA256SUMS').write_text(''.join(
                '%s  %s\n' % (archives[name], name) for name in sorted(archives)))
        result = {'schema': 'toka.qualified-draft-verification', 'version': 1,
                  'candidate_revision': args.candidate_sha, 'version_label': args.tag_name,
                  'qualification_run_id': args.qualification_run_id,
                  'archive_source': 'candidate_run', 'result': 'pass', 'archives': archives,
                  'phase': 'draft-readback' if args.draft_json else 'prepare'}
    except (ValueError, OSError, TypeError) as error:
        result = {'result': 'fail', 'errors': [str(error)]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(json.dumps(result, sort_keys=True))
    raise SystemExit(0 if result['result'] == 'pass' else 1)


if __name__ == '__main__':
    main()
