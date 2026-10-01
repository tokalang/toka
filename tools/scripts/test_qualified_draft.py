#!/usr/bin/env python3
"""Reject mismatched provenance or altered bytes before and after draft creation."""

import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap

from test_release_workflow import shell_run_blocks
import verify_qualified_draft as verifier

ROOT = Path(__file__).resolve().parents[2]
SHA = 'a' * 40
TAG = 'v0.11.0'


def main():
    cases = 0
    with tempfile.TemporaryDirectory(prefix='toka-qualified-draft-') as temp:
        root = Path(temp)
        archives = root / 'archives'
        archives.mkdir()
        for target in verifier.TARGETS:
            directory = archives / ('candidate-archive-' + target)
            directory.mkdir()
            (directory / ('toka-%s-%s.tar.gz' % (TAG, target))).write_bytes(target.encode())
        run = {'id': 11, 'status': 'completed', 'conclusion': 'success', 'head_sha': SHA,
               'event': 'workflow_dispatch', 'path': '.github/workflows/release.yml',
               'repository': {'full_name': 'tokalang/toka'},
               'head_repository': {'full_name': 'tokalang/toka'}}
        summary = {'schema': 'toka.release-qualification-summary', 'version': 1,
                   'result': 'pass', 'errors': [], 'candidate_revision': SHA,
                   'version_label': TAG, 'expected_targets': list(verifier.TARGETS)}
        for key, result in [('reports', 'pass'), ('taskhandle_conformance', 'pass'),
                            ('restricted_cancellation_conformance', 'candidate-pass')]:
            summary[key] = [{'target': t, 'result': result} for t in verifier.TARGETS]
        args = argparse.Namespace(tag_name=TAG, candidate_sha=SHA, repository='tokalang/toka',
            qualification_run_id=11, qualification_run_json=root / 'run.json',
            qualification_summary=root / 'summary.json', qualified_archives_dir=archives,
            draft_json=None, draft_assets_dir=None)

        def write():
            args.qualification_run_json.write_text(json.dumps(run))
            args.qualification_summary.write_text(json.dumps(summary))

        def rejected():
            nonlocal cases
            write()
            try:
                verifier.validate(args)
            except (ValueError, OSError):
                cases += 1
            else:
                raise RuntimeError('invalid draft evidence accepted')

        write()
        digests = verifier.validate(args)
        for key, value in [('tag_name', 'v0.10.0'), ('candidate_sha', 'a' * 39), ('qualification_run_id', 0)]:
            previous = getattr(args, key); setattr(args, key, value); rejected(); setattr(args, key, previous)
        for key, value in [('id', 12), ('status', 'in_progress'), ('conclusion', 'failure'),
                           ('head_sha', 'b' * 40), ('event', 'push'),
                           ('path', '.github/workflows/ci.yml'),
                           ('repository', {'full_name': 'other/toka'}),
                           ('head_repository', {'full_name': 'fork/toka'})]:
            previous = run[key]; run[key] = value; rejected(); run[key] = previous
        for key, value in [('candidate_revision', 'b' * 40), ('version_label', 'v0.11.1'),
                           ('result', 'fail'), ('errors', ['failed']), ('expected_targets', [])]:
            previous = summary[key]; summary[key] = value; rejected(); summary[key] = previous
        for key in ['reports', 'taskhandle_conformance', 'restricted_cancellation_conformance']:
            original = copy.deepcopy(summary[key]); summary[key].pop(); rejected()
            summary[key] = copy.deepcopy(original); summary[key][0]['result'] = 'fail'; rejected()
            summary[key] = copy.deepcopy(original); summary[key][0]['target'] = summary[key][1]['target']; rejected()
            summary[key] = original
        write()
        directory = archives / 'candidate-archive-linux-x64'
        extra = directory / 'extra.tar.gz'; extra.write_bytes(b'extra'); rejected(); extra.unlink()
        directory.rename(archives / 'release-archive-linux-x64'); rejected()
        (archives / 'release-archive-linux-x64').rename(directory)
        archive = next(directory.iterdir()); original = archive.read_bytes(); archive.unlink()
        source = root / 'external'; source.write_bytes(original); archive.symlink_to(source)
        rejected(); archive.unlink(); archive.write_bytes(original)
        write()
        assets = root / 'draft-assets'; assets.mkdir()
        for directory in archives.iterdir():
            for archive in directory.iterdir():
                shutil.copyfile(archive, assets / archive.name)
        manifest = ''.join('%s  %s\n' % (digests[n], n) for n in sorted(digests))
        (assets / 'SHA256SUMS').write_text(manifest)
        draft = {'tagName': TAG, 'isDraft': True, 'isPrerelease': False,
                 'assets': [{'name': n} for n in sorted(digests) + ['SHA256SUMS']]}
        args.draft_json = root / 'draft.json'; args.draft_assets_dir = assets
        def write_draft():
            args.draft_json.write_text(json.dumps(draft))
        write_draft(); assert verifier.validate(args) == digests
        for key, value in [('tagName', 'v0.11.1'), ('isDraft', False), ('isPrerelease', True)]:
            previous = draft[key]; draft[key] = value; write_draft(); rejected(); draft[key] = previous
        write_draft()
        for name in digests:
            archive = assets / name; original = archive.read_bytes(); archive.write_bytes(b'altered')
            rejected(); archive.write_bytes(original)
        (assets / 'SHA256SUMS').write_text('wrong'); rejected(); (assets / 'SHA256SUMS').write_text(manifest)
        draft['assets'].append(draft['assets'][0]); write_draft(); rejected()
        draft['assets'].pop(); write_draft()
        write(); assert verifier.validate(args) == digests
        # Exercise staging through the actual CLI, without remote mutations.
        command = ['python3', str(ROOT / 'tools/scripts/verify_qualified_draft.py'),
                   '--tag-name', TAG, '--candidate-sha', SHA, '--repository', 'tokalang/toka',
                   '--qualification-run-id', '11', '--qualification-run-json', str(args.qualification_run_json),
                   '--qualification-summary', str(args.qualification_summary),
                   '--qualified-archives-dir', str(archives), '--assets-dir', str(root / 'staged'),
                   '--output', str(root / 'receipt.json')]
        subprocess.run(command, check=True, capture_output=True)
        assert (root / 'staged/SHA256SUMS').read_text() == manifest
        assert json.loads((root / 'receipt.json').read_text())['archives'] == digests
        # A second staging attempt must not replace any existing bytes.
        assert subprocess.run(command, capture_output=True).returncode != 0
        cases += 1

    text = (ROOT / '.github/workflows/create_qualified_draft.yml').read_text()
    for shell in shell_run_blocks(text):
        subprocess.run(['bash', '-n'], input=textwrap.dedent(shell), text=True, check=True)
    assert 'GH_TOKEN: ${{ github.token }}' in text and 'git push' not in '\n'.join(shell_run_blocks(text))
    assert 'persist-credentials: false' in text and 'cancel-in-progress: false' in text
    assert 'git/tags' in text and 'git/refs' in text and '--verify-tag --draft' in text
    assert '--prerelease=false --latest=false' in text and '--draft-assets-dir draft-assets' in text
    assert 'candidate-archive-*' in text and 'release-gate-*' in text
    assert 'qualified-source/tools/scripts/verify_release_qualification.py' in text
    assert text.index('--output draft-preparation.json') < text.index('git/tags')
    assert 'workflow_dispatch:' in text and '\n  push:' not in text
    assert 'release_gate.py' not in text and 'cmake' not in text
    assert text.index('Refuse existing release') < text.index('Create or verify immutable annotated tag')
    release_shell = next(s for s in shell_run_blocks(text) if 'existing-release' in s)
    with tempfile.TemporaryDirectory(prefix='toka-release-api-test-') as temp:
        root = Path(temp)
        fake = root / 'gh'
        fake.write_text('''#!/usr/bin/env python3
import os, sys
assert sys.argv[1] == 'api'
endpoint = next(arg for arg in sys.argv[2:] if arg.startswith('repos/'))
if '/releases/tags/' in endpoint:
    # The tag endpoint returned 404 for the real unpublished v0.11.0 draft.
    print('gh: Not Found (HTTP 404)', file=sys.stderr)
    sys.exit(1)
assert endpoint == 'repos/tokalang/toka/releases?per_page=100'
assert '--paginate' in sys.argv and '--slurp' in sys.argv
sys.stdout.write(os.environ['RELEASE_API_RESPONSE'])
if os.environ['RELEASE_API_ERROR']:
    print(os.environ['RELEASE_API_ERROR'], file=sys.stderr)
sys.exit(int(os.environ['RELEASE_API_STATUS']))
''')
        fake.chmod(0o755)
        draft = {'tag_name': TAG, 'draft': True}
        published = {'tag_name': TAG, 'draft': False}
        other = {'tag_name': 'v0.10.0', 'draft': False}
        scenarios = [
            ('empty', [[]], True),
            ('other-release', [[other]], True),
            ('other-draft', [[dict(other, draft=True)]], True),
            ('existing-draft', [[draft]], False),
            ('existing-publication', [[published]], False),
            ('later-page-draft', [[other], [draft]], False),
            ('later-page-publication', [[other], [published]], False),
            ('no-pages', [], False),
            ('invalid-envelope', {}, False),
            ('invalid-page', [{}], False),
            ('invalid-release', [[None]], False),
            ('missing-tag', [[{'draft': True}]], False),
        ]
        env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                   TAG_NAME=TAG, GITHUB_REPOSITORY='tokalang/toka')
        cases_to_run = [(name, json.dumps(pages), '', 0, accepted)
                        for name, pages, accepted in scenarios]
        cases_to_run.append(('invalid-json', '{', '', 0, False))
        for status in (401, 403, 404, 429, 500):
            cases_to_run.append(('http-%d' % status, '',
                                 'gh: API failure (HTTP %d)' % status, 1, False))
        cases_to_run.append(('partial-pagination-failure', json.dumps([[other]]),
                             'gh: connection reset', 1, False))
        for name, response, error, status, accepted in cases_to_run:
            env.update(RELEASE_API_RESPONSE=response, RELEASE_API_ERROR=error,
                       RELEASE_API_STATUS=str(status))
            result = subprocess.run(['bash', '-c', textwrap.dedent(release_shell)],
                                    cwd=root, env=env, capture_output=True, text=True)
            assert (result.returncode == 0) == accepted, (name, result.stderr)
            cases += 1
    tag_shell = next(s for s in shell_run_blocks(text) if 'git/ref/tags/' in s)
    with tempfile.TemporaryDirectory(prefix='toka-tag-api-test-') as temp:
        root = Path(temp)
        fake = root / 'gh'
        fake.write_text('''#!/usr/bin/env python3
import json, os, sys
scenario = os.environ['TAG_SCENARIO']
endpoint = sys.argv[sys.argv.index('--input') - 1] if '--input' in sys.argv else sys.argv[2]
obj = {'sha': 'c' * 40, 'tag': os.environ['TAG_NAME'],
       'object': {'type': 'commit', 'sha': os.environ['CANDIDATE_SHA']}}
ref = {'ref': 'refs/tags/' + os.environ['TAG_NAME'],
       'object': {'type': 'tag', 'sha': obj['sha']}}
if '/git/ref/tags/' in endpoint:
    if scenario in ('new', 'denied'):
        print('gh: Not Found (HTTP 404)' if scenario == 'new' else 'gh: Forbidden (HTTP 403)', file=sys.stderr)
        sys.exit(1)
    if scenario == 'lightweight': ref['object']['type'] = 'commit'
    print(json.dumps(ref))
elif endpoint.endswith('/git/refs'):
    request = json.load(open('ref-request.json'))
    assert request['ref'] == ref['ref'] and request['sha'] == obj['sha']
    print(json.dumps(ref))
else:
    if scenario == 'wrong': obj['object']['sha'] = 'b' * 40
    if scenario == 'nested': obj['object']['type'] = 'tag'
    if '--input' in sys.argv:
        request = json.load(open('tag-request.json'))
        assert request['tag'] == obj['tag'] and request['object'] == obj['object']['sha'] and request['type'] == 'commit'
    print(json.dumps(obj))
''')
        fake.chmod(0o755)
        env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                   TAG_NAME=TAG, CANDIDATE_SHA=SHA, QUALIFICATION_RUN_ID='11',
                   GITHUB_REPOSITORY='tokalang/toka')
        for scenario in ['new', 'existing', 'lightweight', 'wrong', 'nested', 'denied']:
            env['TAG_SCENARIO'] = scenario
            result = subprocess.run(['bash', '-c', textwrap.dedent(tag_shell)],
                                    cwd=root, env=env, capture_output=True, text=True)
            assert (result.returncode == 0) == (scenario in ['new', 'existing']), (scenario, result.stderr)
            cases += 1
    print('Qualified draft checks PASSED: positive preparation/readback and %d controls' % cases)


if __name__ == '__main__':
    main()
