#!/usr/bin/env python3
"""Nominal identity is independent of physical layout and source availability."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tests/semantics/nominal_identity'
POSITIVES = ('constructors', 'nested_nominal', 'nested_alias', 'cleanup', 'imported',
             'borrowed_return', 'trait_control', 'own_trait', 'own_drop', 'writable_reference', 'noncopy_transfer')
NEGATIVES = {
    'mixed_identity': ('E0408', 'first = second'),
    'underlying_target': ('E0408', 'base = strong'),
    'nested_mismatch': ('E0408', 'box.value = Leaf'),
    'trait_not_inherited': ('E0606', 'return needs'),
    'permission': ('E04573', 'holder.value = 8'),
    'inner_readonly': ('E04573', 'holder.value = 8'),
    'opaque_field': ('E0417', 'return box.value.value'),
    'borrowed_escape': ('E0455', 'return Strong'),
    'rejected_transfer': ('E0408', 'target = cede source'),
    'noncopy_layout': ('E04661', 'auto result = value'),
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = args.build_dir.resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-nominal-identity-') as temp:
        work = args.keep_dir or Path(temp)
        work.mkdir(parents=True, exist_ok=True)
        results = []
        def compile(source, flags=(), cwd=ROOT):
            return subprocess.run([str(compiler), '--workspace-node', 'nominal-identity',
                '--workspace-root', str(cwd), '-I', str(cwd), str(source), *map(str, flags)],
                cwd=cwd, env=env, capture_output=True, text=True, timeout=90)
        def parity(source, cwd=ROOT):
            normal = compile(source, ['--check-only'], cwd)
            for flag in ('--call-transfer-shadow=json', '--non-call-transfer-shadow=json'):
                shadow = compile(source, ['--check-only', flag], cwd)
                assert (normal.returncode, normal.stderr) == (shadow.returncode, shadow.stderr), (source, flag, shadow.stderr)
            return normal
        def rejection(result, source, code, marker):
            text = re.sub(r'\x1b\[[0-9;]*m', '', result.stderr)
            errors = re.findall(r'^error\[(E\d+)\]: ([^\n]*)\n\s*--> ([^\n]+):(\d+):(\d+)', text, re.M)
            line = next(i for i, s in enumerate(source.read_text().splitlines(), 1) if marker in s)
            assert result.returncode == 1 and errors and errors[0][0] == code and errors[0][2] == str(source) and int(errors[0][3]) == line, (source, text)
            if source.stem == 'rejected_transfer':
                assert not any(e[0] in ('E0438', 'E0410') for e in errors), text
            if source.stem == 'noncopy_layout':
                assert 'MissingCedeForNamedSource' in errors[0][1], text
        for name, (code, marker) in NEGATIVES.items():
            source = FIXTURES / (name + '.tk')
            rejection(parity(source), source, code, marker)
            for suffix, flags in (('o', ['-c']), ('ll', ['--emit-llvm'])):
                artifact = work / (name + '.' + suffix)
                assert not artifact.exists()
                rejection(compile(source, [*flags, '-o', artifact]), source, code, marker)
                assert not artifact.exists()
            results.append(dict(case=name, status='semantic-rejection', code=code, parity=True))
        original = ROOT / 'tests/fail/alias_generic_test.tk'
        rejection(parity(original), original, 'E0408', 's1 = s2')
        for suffix, flags in (('o', ['-c']), ('ll', ['--emit-llvm'])):
            artifact = work / ('original.' + suffix)
            rejection(compile(original, [*flags, '-o', artifact]), original, 'E0408', 's1 = s2')
            assert not artifact.exists()
        results.append(dict(case=original.name, status='original-rejection', parity=True))
        positives = [(FIXTURES / (name + '.tk'), 0) for name in POSITIVES]
        positives += [(ROOT / 'tests/pass/g07_alias_generic_test.tk', 0),
                      (ROOT / 'tests/pass/g03_newtype.tk', 0),
                      (ROOT / 'tests/semantics/rc13_negative_purposes/blockers/alias_good.tk', 30)]
        for source, expected in positives:
            checked = parity(source)
            assert checked.returncode == 0, (source, checked.stderr)
            binary = work / source.stem
            built = compile(source, ['-o', binary])
            assert built.returncode == 0, (source, built.stderr)
            ran = subprocess.run([str(binary)], cwd=work, env=env, capture_output=True, text=True, timeout=30)
            assert ran.returncode == expected, (source, ran.returncode, ran.stderr)
            results.append(dict(case=source.name, status='runtime-passed', exit=expected, parity=True))
        for suffix, flags in (('o', ['-c']), ('ll', ['--emit-llvm'])):
            artifact = work / ('valid.' + suffix)
            result = compile(FIXTURES / 'constructors.tk', [*flags, '-o', artifact])
            assert result.returncode == 0 and artifact.is_file() and artifact.stat().st_size, result.stderr
            if suffix == 'll': assert '\ndefine ' in artifact.read_text()
        interface = work / 'interface'
        interface.mkdir()
        provider = interface / 'provider.tk'
        provider.write_text((FIXTURES / 'module_types.tk').read_text())
        consumer = interface / 'consumer.tk'
        consumer.write_text((FIXTURES / 'imported.tk').read_text().replace('tests/semantics/nominal_identity/module_types', 'provider'))
        emitted = compile(provider, ['--emit-interface', '-c', '-o', interface / 'provider.o'], interface)
        assert emitted.returncode == 0 and (interface / 'provider.tki').exists(), emitted.stderr
        for hidden in (False, True):
            if hidden: provider.rename(interface / 'provider.saved-source')
            checked = parity(consumer, interface)
            assert checked.returncode == 0, checked.stderr
            binary = interface / ('hidden' if hidden else 'visible')
            built = compile(consumer, [interface / 'provider.o', '-o', binary], interface)
            assert built.returncode == 0, built.stderr
            assert subprocess.run([str(binary)], cwd=interface, env=env, timeout=30).returncode == 0
            results.append(dict(case='interface', hidden=hidden, status='runtime-passed', parity=True))
        (work / 'results.json').write_text(json.dumps(results, indent=2))
        print(f'{len(positives)} runtime controls; 11 semantic negatives; 22 no-artifact checks; 2 valid artifacts; source-visible/source-hidden linking and parity')

if __name__ == '__main__':
    main()
