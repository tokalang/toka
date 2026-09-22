#!/usr/bin/env python3
"""Build's flat string-list representation: content, lifetime and real hybrid build."""
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
    env = dict(os.environ, TOKAC=str(compiler), TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-build-metadata-') as directory:
        work = Path(directory)
        for source, error in ((ROOT / 'tests/semantics/build_metadata/owned_strings.tk', None),
                              (ROOT / 'tests/semantics/build_metadata/local_view_escape.tk', 'E0455'),
                              (ROOT / 'tests/pass/g10_build_hybrid_test.tk', None)):
            prefix = [str(compiler), str(source)]
            def compile(*flags):
                return subprocess.run(prefix + list(map(str, flags)), cwd=ROOT, env=env,
                                      capture_output=True, text=True, timeout=90)
            normal, shadow = compile('--check-only'), compile('--check-only', '--non-call-transfer-shadow=json')
            expected = 1 if error else 0
            assert normal.returncode == shadow.returncode == expected and normal.stderr == shadow.stderr, (source, normal.stderr, shadow.stderr)
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (source.stem + suffix)
                result = compile(flag, '-o', output)
                assert result.returncode == expected, result.stderr
                if error:
                    assert f'error[{error}]' in result.stderr and str(source) in result.stderr and not output.exists(), result.stderr
                else:
                    assert output.is_file() and output.stat().st_size > 0
            if not error:
                binary = work / source.stem
                built = compile('-o', binary)
                assert built.returncode == 0, built.stderr
                ran = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
                assert ran.returncode == 0, (source, ran.stdout, ran.stderr)
                if source.stem == 'g10_build_hybrid_test':
                    assert 'Hybrid build integration test completed successfully!' in ran.stdout, ran.stdout
            print('PASS', source.name, flush=True)

if __name__ == '__main__':
    main()
