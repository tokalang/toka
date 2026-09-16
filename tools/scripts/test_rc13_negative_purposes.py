#!/usr/bin/env python3
"""Check repaired negative purposes, not just nonzero compiler status.

Blocked cases remain in the full FAIL suite; they are not green negatives here.
No snapshot generation or diagnostic blessing is performed by this test.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tests/semantics/rc13_negative_purposes'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = args.build_dir.resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    rows = json.loads((FIXTURES / 'manifest.json').read_text())['cases']
    assert len(rows) == 31 and len({r['case'] for r in rows}) == 31
    with tempfile.TemporaryDirectory(prefix='toka-negative-purposes-') as temp:
        work = args.keep_dir or Path(temp)
        work.mkdir(parents=True, exist_ok=True)
        results = []
        output_modes = (('o', ['-c']), ('ll', ['--emit-llvm']))
        def compile(source, flags):
            return subprocess.run([str(compiler), '--workspace-node', 'toka-tests-v1',
                '--workspace-root', str(ROOT), str(source), *flags], cwd=ROOT,
                env=env, capture_output=True, text=True, timeout=90)
        def check_parity(source):
            normal = compile(source, ['--check-only'])
            for flag in ('--call-transfer-shadow=json', '--non-call-transfer-shadow=json'):
                shadow = compile(source, ['--check-only', flag])
                assert (normal.returncode, normal.stderr) == (shadow.returncode, shadow.stderr), (source, flag)
            return normal
        def check_target(result, source, row):
            assert result.returncode == 1, (source, result.returncode, result.stderr)
            stderr = re.sub(r'\x1b\[[0-9;]*m', '', result.stderr)
            errors = re.findall(r'^error\[(E\d+)\]: ([^\n]*)\n\s*--> ([^\n]+):(\d+):(\d+)', stderr, re.M)
            assert errors and errors[0][0] == row['target_codes'][0] and errors[0][1] == row['first_message'], (source, stderr)
            sites = {(c, f.removeprefix(str(ROOT) + '/'), int(line))
                     for c, message, f, line, col in errors}
            for site in row['target_sites']:
                assert (site['code'], site['file'], site['line']) in sites, (source, site, stderr)
        for row in rows:
            if row['status'] == 'blocked':
                results.append(dict(case=row['case'], status='blocked-not-counted', reason=row['reason']))
                continue
            source = ROOT / 'tests/fail' / row['case']
            normal = check_parity(source)
            check_target(normal, source, row)
            for suffix, flags in output_modes:
                artifact = work / (source.stem + '.' + suffix)
                assert not artifact.exists(), artifact
                rejected = compile(source, [*flags, '-o', str(artifact)])
                check_target(rejected, source, row)
                assert not artifact.exists(), (source, suffix, rejected.stderr)
            results.append(dict(case=row['case'], status='target-verified', parity=True,
                                artifacts=False, target_checked_modes=['check-only', 'object', 'llvm-ir']))
        artifact_source = FIXTURES / 'generic_and_ownership.tk'
        for suffix, flags in output_modes:
            artifact = work / ('positive-artifact.' + suffix)
            assert not artifact.exists(), artifact
            built = compile(artifact_source, [*flags, '-o', str(artifact)])
            assert built.returncode == 0, (artifact_source, suffix, built.stderr)
            assert artifact.is_file() and artifact.stat().st_size > 0, artifact
            if suffix == 'll':
                ir = artifact.read_text()
                assert 'target triple' in ir and '\ndefine ' in ir, artifact
            results.append(dict(case=artifact_source.name, status='positive-artifact',
                                mode=suffix, flags=flags, bytes=artifact.stat().st_size))
        positives = sorted(FIXTURES.glob('*.tk'))
        for source in positives:
            checked = check_parity(source)
            assert checked.returncode == 0, (source, checked.stderr)
            binary = work / source.stem
            built = compile(source, ['-o', str(binary)])
            assert built.returncode == 0, (source, built.stderr)
            ran = subprocess.run([str(binary)], cwd=work, env=env, capture_output=True, text=True, timeout=40)
            assert ran.returncode == 0, (source, ran.returncode, ran.stdout, ran.stderr)
            results.append(dict(case=source.name, status='positive-runtime', parity=True))
        (work / 'results.json').write_text(json.dumps(results, indent=2))
        repaired = sum(r['status'] != 'blocked' for r in rows)
        print(f'{repaired} repaired purposes; {len(positives)} runtime controls; {2 * repaired} semantic no-artifact checks; 2 positive artifacts; {31 - repaired} blocked cases NOT counted')

if __name__ == '__main__':
    main()
