#!/usr/bin/env python3
"""Reference capability fidelity, operation ceilings and instance isolation."""
import argparse
import json
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
    with tempfile.TemporaryDirectory(prefix='toka-permission-views-') as directory:
        work = Path(directory)
        def run(source, *flags):
            scope = ROOT if source.is_relative_to(ROOT) else work
            return subprocess.run([str(compiler), '--workspace-node', 'permission-views',
                                   '--workspace-root', str(scope), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        for name in ('writable_reference', 'mixed_instances'):
            source = ROOT / 'tests/semantics/permission_views' / (name + '.tk')
            normal, shadow = run(source, '--check-only'), run(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, (name, normal.stderr)
            json.loads(shadow.stdout)
            binary = work / name
            built = run(source, '-o', binary)
            assert built.returncode == 0, built.stderr
            assert subprocess.run([str(binary)], timeout=10).returncode == 0
            print('PASS view runtime ' + name, flush=True)
        readonly_body = 'fn bad(slot#:Slot<&i32>) { slot.value = 9 }\n'
        warm = 'fn warm(slot#:Slot<&i32#>) { slot.value = 42 }\n'
        cases = {
            'outer_only': ("shape Slot<T>(value:T)\n" + readonly_body, 'E04573'),
            'field_only': ("shape Slot<T>(value#:T)\nfn bad(slot:Slot<&i32>) { slot.value = 9 }\n", 'E04573'),
            'both': ("shape Slot<T>(value#:T)\n" + readonly_body, 'E04573'),
            'mutable_then_readonly': ("shape Slot<T>(value#:T)\n" + warm + readonly_body, 'E04573'),
            'readonly_then_mutable': ("shape Slot<T>(value#:T)\n" + readonly_body + warm, 'E04573'),
            'no_rebind_grant': ("shape Slot<T>(value:T)\nfn bad(slot#:Slot<&i32#>, next#:i32) { slot.&#value = &next }\n", 'E04572'),
            'invariant_up': ("shape Slot<T>(value:T)\nfn accept(value:Slot<&i32#>) {}\nfn bad(value:Slot<&i32>) { accept(value) }\n", 'E04571'),
            'invariant_down': ("shape Slot<T>(value:T)\nfn accept(value:Slot<&i32>) {}\nfn bad(value:Slot<&i32#>) { accept(value) }\n", 'E04571'),
        }
        for name, (text, code) in cases.items():
            source = work / (name + '.tk')
            source.write_text(text + 'fn main() -> i32 { return 0 }\n')
            normal, shadow = run(source, '--check-only'), run(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr)
            assert f'error[{code}]' in normal.stderr, (name, normal.stderr)
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (name + suffix)
                result = run(source, flag, '-o', output)
                assert result.returncode == 1 and not output.exists(), (name, result.stderr)
            print('PASS view rejection ' + name, flush=True)
        print('views: 2 runtime; 8 rejection/parity; 16 no-artifact checks')
if __name__ == '__main__': main()
