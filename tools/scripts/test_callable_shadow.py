#!/usr/bin/env python3
"""Lexical callable selection, real invoke targets and Template dispatcher boundary."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tests/semantics/callable_shadow'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    for key in ('TOKA_USE_LIB_CACHE', 'TOKA_CACHED_LIB_ARCHIVE',
                'TOKA_CACHED_LIB_OBJECTS_FILE', 'TOKA_CACHED_LIB_OBJECTS_MAP'):
        env.pop(key, None)
    cases = ['callable_shadow_minimal', 'bindings', 'qualified',
             'dispatcher_name_collision', 'dispatcher_renamed_control',
             'dispatcher_actual', 'noncallable']
    with tempfile.TemporaryDirectory(prefix='toka-callable-shadow-') as directory:
        work = Path(directory)
        # Execute the public examples as well, so documentation stays usable.
        documentation = (ROOT / 'docs/stdx_template_v1.md').read_text()
        for index, content in enumerate(re.findall(r'```toka\n(.*?)```', documentation, re.S)):
            source = work / f'documentation_{index}.tk'
            source.write_text(content)
            cases.append(source)
        for case in cases:
            source = case if isinstance(case, Path) else FIXTURES / (case + '.tk')
            negative = source.stem == 'noncallable'
            expected = 1 if negative else 0
            def compile(*flags):
                result = subprocess.run([str(compiler), str(source), *map(str, flags)],
                                        cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
                assert result.returncode == expected, (source, result.stderr)
                if negative:
                    assert 'error[E0402]' in result.stderr and str(source) in result.stderr, result.stderr
                return result
            normal = compile('--check-only')
            shadow = compile('--check-only', '--non-call-transfer-shadow=json')
            assert normal.stderr == shadow.stderr, (source, normal.stderr, shadow.stderr)
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (source.stem + suffix)
                compile(flag, '-o', output)
                assert output.exists() != negative, output
                if not negative:
                    assert output.stat().st_size > 0, output
                if not negative and suffix == '.ll' and source.stem == 'callable_shadow_minimal':
                    body = re.search(r'^define [^\n]*@apply\(.*?^}', output.read_text(), re.M | re.S)
                    assert body and '%closure_func(' in body[0], output
                    assert not re.search(r'\bcall\b[^\n]*@dispatch\(', body[0]), body[0]
            if not negative:
                binary = work / source.stem
                compile('-o', binary)
                ran = subprocess.run([str(binary)], cwd=ROOT, env=env, text=True,
                                     capture_output=True, timeout=60)
                assert ran.returncode == 0, (source, ran.stdout, ran.stderr)
            print('PASS', source.name, flush=True)


if __name__ == '__main__':
    main()
