#!/usr/bin/env python3
"""Owned TOML arrays/template lists: original behavior, parity, lifetime and artifacts."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    for key in ('TOKA_USE_LIB_CACHE', 'TOKA_CACHED_LIB_ARCHIVE',
                'TOKA_CACHED_LIB_OBJECTS_FILE', 'TOKA_CACHED_LIB_OBJECTS_MAP'):
        env.pop(key, None)
    cases = [('tests/pass/g15_stdx_toml_test.tk', None),
             ('tests/pass/g18_stdx_template_test.tk', None),
             ('tests/semantics/toml_template/owned_values.tk', None),
             ('tests/semantics/toml_template/toml_local_escape.tk', 'E0455'),
             ('tests/semantics/toml_template/template_local_escape.tk', 'E0455')]
    with tempfile.TemporaryDirectory(prefix='toka-toml-template-') as directory:
        work = Path(directory)
        for relative, error in cases:
            source = ROOT / relative
            def compile(*flags):
                return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                      cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            expected = 1 if error else 0
            normal = compile('--check-only')
            shadow = compile('--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == expected, (source, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, (source, normal.stderr, shadow.stderr)
            if error:
                assert f'error[{error}]' in normal.stderr and str(source) in normal.stderr, normal.stderr
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (source.stem + suffix)
                result = compile(flag, '-o', output)
                assert result.returncode == expected, (source, result.stderr)
                if error:
                    assert f'error[{error}]' in result.stderr and str(source) in result.stderr, result.stderr
                    assert not output.exists(), output
                else:
                    assert output.is_file() and output.stat().st_size > 0, output
            if not error:
                output = work / source.stem
                result = compile('-o', output)
                assert result.returncode == 0, (source, result.stderr)
                ran = subprocess.run([str(output)], cwd=ROOT, env=env,
                                     capture_output=True, text=True, timeout=90)
                assert ran.returncode == 0, (source, ran.stdout, ran.stderr)
            print('PASS', source.name, flush=True)


if __name__ == '__main__':
    main()
