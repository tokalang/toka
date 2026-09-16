#!/usr/bin/env python3
"""Member-reference semantics: original PAL cases, authority, lifetime and IR."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tests/semantics/member_reference_entry'
POSITIVES = ('permissions', 'call_reference', 'reference_field', 'wrappers',
             'managed_owners', 'live_sibling')
NEGATIVES = {
    'readonly_upgrade': ('E04661', 'auto &bad#'),
    'readonly_parameter': ('E04661', 'auto &bad#'),
    'wrapper_upgrade': ('E04661', 'auto &bad#'),
    'rejection_rollback': ('E04661', 'auto &bad#'),
    'frozen_upgrade': ('E04573', 'bad = 8'),
    'readonly_base': ('E0441', 'auto &bad#'),
    'parent_conflict': ('E0441', 'auto &bad'),
    'child_conflict': ('E0441', 'auto &bad'),
    'call_duplicate': ('E0441', 'pair<i32>'),
    'call_existing_loan': ('E0441', 'replace<i32>'),
    'descriptor_escape': ('E0455', 'return holder.&view'),
    'reference_escape': ('E0455', 'return holder.&value'),
    'uninitialized': ('E0410', 'auto &bad'),
    'moved_source': ('E0410', 'auto &bad'),
}
ORIGINALS = {
    'pal_member_mut_borrow_duplicate': ('E0441', 'auto &right#'),
    'pal_member_mut_borrow_payload_read': ('E0441', 'auto value = info.accesses'),
    'pal_member_mut_borrow_payload_write': ('E0441', 'info.accesses = 1'),
    'readonly_member_ref_decl_upgrade_from_plain_cast': ('E04573', 'auto &mut_view#'),
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = args.build_dir.resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-member-reference-') as tmp:
        work = args.keep_dir or Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        results = []
        def compile(source, flags):
            return subprocess.run([str(compiler), '--workspace-node', 'toka-tests-v1',
                '--workspace-root', str(ROOT), str(source), *flags], cwd=ROOT,
                env=env, capture_output=True, text=True, timeout=90)
        def parity(source):
            normal = compile(source, ['--check-only'])
            evidence = None
            for flag in ('--call-transfer-shadow=json', '--non-call-transfer-shadow=json'):
                shadow = compile(source, ['--check-only', flag])
                assert (normal.returncode, normal.stderr) == (shadow.returncode, shadow.stderr), (source, flag, shadow.stderr)
                if flag.startswith('--non-call'):
                    evidence = json.loads(shadow.stdout)
            return normal, evidence
        def validate(result, source, code, marker):
            assert result.returncode == 1, (source, result.returncode, result.stderr)
            text = re.sub(r'\x1b\[[0-9;]*m', '', result.stderr)
            errors = re.findall(r'^error\[(E\d+)\]: ([^\n]*)\n\s*--> ([^\n]+):(\d+):(\d+)', text, re.M)
            line = next(i for i, s in enumerate(source.read_text().splitlines(), 1) if marker in s)
            assert errors and errors[0][0] == code and errors[0][2] == str(source) and int(errors[0][3]) == line, (source, text)
            if code == 'E04661': assert 'AccessCapabilityMismatch' in errors[0][1], (source, text)
            if source.stem == 'rejection_rollback':
                assert len(errors) == 1, text
        for folder, cases in ((FIXTURES, NEGATIVES), (ROOT / 'tests/fail', ORIGINALS)):
            for name, (code, marker) in cases.items():
                source = folder / (name + '.tk')
                normal, evidence = parity(source)
                validate(normal, source, code, marker)
                for suffix, flags in (('o', ['-c']), ('ll', ['--emit-llvm'])):
                    artifact = work / (name + '.' + suffix)
                    assert not artifact.exists(), artifact
                    rejected = compile(source, [*flags, '-o', str(artifact)])
                    validate(rejected, source, code, marker)
                    assert not artifact.exists(), artifact
                results.append(dict(case=name, outcome='rejected', code=code, parity=True, semantic_no_artifact_modes=2))
        positives = [FIXTURES / (n + '.tk') for n in POSITIVES]
        positives.append(ROOT / 'tests/semantics/rc13_negative_purposes/blockers/member_reference.tk')
        for source in positives:
            normal, evidence = parity(source)
            assert normal.returncode == 0, (source, normal.stderr)
            references = [r for r in evidence['records'] if r['location']['file'] == str(source)
                          and r['boundary'] == 'initialization'
                          and r['plan']['source_view'] == 'ReferenceConstruction']
            if source.stem in ('permissions', 'managed_owners', 'live_sibling', 'member_reference'):
                assert references, source
                for r in references:
                    plan = r['plan']
                    assert plan['outcome'] == 'Admitted' and plan['actual_type'].startswith('&'), r
                    assert plan['exact_path'] == '' and '/field:' in plan['referent_path'], r
                    assert plan['dependency_roots'] and plan['drop'] == 'NoLiability', r
                    assert plan['value_production'] == 'CopyIdentity' and plan['source'] == 'NoSourcePlace', r
            if source.stem == 'permissions':
                assert any(not r['source_flow_ceiling']['payload_write'] for r in references), references
                assert any(r['source_flow_ceiling']['payload_write'] for r in references), references
            binary = work / source.stem
            built = compile(source, ['-o', str(binary)])
            assert built.returncode == 0, (source, built.stderr)
            ran = subprocess.run([str(binary)], cwd=work, env=env, capture_output=True, text=True, timeout=30)
            assert ran.returncode == 0, (source, ran.returncode, ran.stdout, ran.stderr)
            results.append(dict(case=source.stem, outcome='runtime-passed', parity=True, reference_records=references))
        for suffix, flags in (('o', ['-c']), ('ll', ['--emit-llvm'])):
            artifact = work / ('valid-control.' + suffix)
            built = compile(FIXTURES / 'permissions.tk', [*flags, '-o', str(artifact)])
            assert built.returncode == 0 and artifact.is_file() and artifact.stat().st_size, built.stderr
            if suffix == 'll': assert '\ndefine ' in artifact.read_text()
        (work / 'results.json').write_text(json.dumps(results, indent=2))
        print('7 runtime controls; 4 unchanged original negatives + 14 adversarial negatives; 36 semantic no-artifact checks; 2 valid artifacts; both shadow modes')

if __name__ == '__main__':
    main()
