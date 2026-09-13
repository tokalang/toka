#!/usr/bin/env python3
"""Separate source grammar from semantic permission and runtime qualification."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ILLEGAL = {
    'parameter': 'fn bad(value: i32#) {}',
    'handle_parameter': 'fn bad(*out: i32#) {}',
    'rebind_hint': 'fn bad(*#out: i32#) {}',
    'self': 'shape Cell(x:i32)\nimpl Cell { fn bad(self:Cell#) {} }',
    'field': 'shape Cell(x:i32#)',
    'local': 'fn bad() { auto x:i32# = 1 }',
    'cast': 'fn bad(x:i32) { auto y = x as i32# }',
    'alias': 'alias Writable = i32#',
    'generic_argument': 'shape Box<T>(value:T)\nfn bad(value: Box<i32#>) {}',
    'array': 'fn bad(value: [i32#; 2]) {}',
    'generic_return_argument': 'shape Box<T>(value:T)\nextern fn bad() -> Box<i32#>',
    'function_parameter': 'alias Bad = fn(i32#) -> i32',
    'dyn_trait_argument': 'trait @Reader<T> { fn read(self) -> T }\nfn bad(value:dyn @Reader<i32#>) {}',
    'blocked': 'alias Bad = i32$',
}
LEGAL = {
    'named': 'fn put(*#out#:i32) { out = 1 }',
    'return': 'extern fn result() -> *i32#',
    'function_return': 'alias Callback = fn(i32) -> *i32#',
    'callable_receiver': 'alias Callback = fn#(i32) -> i32',
    'dynamic_callable_receiver': 'alias Callback = dyn fn#(i32) -> i32',
    'array_view': 'alias Views = [&i32#; 2]',
    'morphic_view': "shape Slot<'T>('value:T)\nalias View = Slot<&i32#>",
    'callback_view': 'alias Callback = fn(&i32#) -> i32',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-permission-positions-') as directory:
        work = args.keep_dir or Path(directory)
        work.mkdir(parents=True, exist_ok=True)
        for rejected, cases in ((True, ILLEGAL), (False, LEGAL)):
            for name, text in cases.items():
                source = work / (name + '.tk')
                source.write_text(text + '\nfn main() -> i32 { return 0 }\n')
                prefix = [str(compiler), '--workspace-node', 'permission-positions',
                          '--workspace-root', str(work), str(source)]
                def run(*flags):
                    return subprocess.run(prefix + list(map(str, flags)), cwd=ROOT,
                                          env=env, text=True, capture_output=True, timeout=60)
                normal = run('--check-only')
                shadow = run('--check-only', '--non-call-transfer-shadow=json')
                assert normal.returncode == shadow.returncode and normal.stderr == shadow.stderr, name
                if rejected:
                    assert normal.returncode == 1 and 'E0496' in normal.stderr, (name, normal.stderr)
                    assert 'binding' in normal.stderr, (name, normal.stderr)
                    if name == 'rebind_hint':
                        assert '*#out#: i32' in normal.stderr, normal.stderr
                    for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                        artifact = work / (name + suffix)
                        failed = run(flag, '-o', artifact)
                        assert failed.returncode == 1 and not artifact.exists(), name
                else:
                    assert normal.returncode == 0, (name, normal.stderr)
                print('PASS grammar ' + name, flush=True)


if __name__ == '__main__':
    main()
