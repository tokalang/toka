#!/usr/bin/env python3
"""Native empty carriers: implicit attribute conversions only, never arbitrary zero expressions."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    sdk = (ROOT / 'lib/std/sync.tk').read_text()
    original = "auto ~mutex# = new Mutex<'T>(handle = 0:Addr, data_ptr = 0:Addr)"
    assert sdk.count(original) == 1
    with tempfile.TemporaryDirectory(prefix='toka-permission-native-') as directory:
        work = Path(directory)
        library = work / 'lib'
        shutil.copytree(ROOT / 'lib', library, ignore=shutil.ignore_patterns('*.o', '*.a', '*.ll', '*.tki'))
        source = work / 'main.tk'
        source.write_text('import std/sync::{Mutex}\nfn main() -> i32 {\nauto ~owner = Mutex<i32>::make_shared(7)\nreturn 0\n}\n')
        env = dict(os.environ, TOKA_LIB=str(library))
        def run(*flags):
            return subprocess.run([str(compiler), '--workspace-node', 'permission-native',
                                   '--workspace-root', str(work), str(source), *map(str, flags)],
                                  cwd=work, env=env, text=True, capture_output=True, timeout=60)
        baseline = run('--check-only')
        assert baseline.returncode == 0, baseline.stderr
        for name, expression, prelude, extra in (
            ('nonzero', '1:Addr', '', ''),
            ('variable', 'zero', 'auto zero# = 0:Addr\n        ', ''),
            ('call', '__permission_zero()', '', '\nauto __permission_calls# = 0\nfn __permission_zero() -> Addr { __permission_calls += 1\nreturn 0:Addr }\n'),
            ('explicit_cast', '(0:Addr as Addr)', '', ''),
        ):
            modified = sdk.replace(original, prelude + original.replace('handle = 0:Addr', 'handle = ' + expression)) + extra
            # Verify rejection rolls the consuming source back too.
            modified = modified.replace('mutex = cede prepared\n        return cede ~mutex',
                                        'mutex = cede prepared\n        auto restored = prepared.handle\n        return cede ~mutex')
            (library / 'std/sync.tk').write_text(modified)
            normal = run('--check-only')
            shadow = run('--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr)
            assert 'EmptyCarrierLiteralRequired' in normal.stderr, (name, normal.stderr)
            assert 'E0438' not in normal.stderr and 'E0410' not in normal.stderr, (name, normal.stderr)
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (name + suffix)
                failed = run(flag, '-o', output)
                assert failed.returncode == 1 and not output.exists(), (name, failed.stderr)
            print('PASS native literal rejection ' + name, flush=True)
        print('native literal: positive implicit conversion; 4 rejection/parity/rollback cases; 8 no-artifact checks')

if __name__ == '__main__': main()
