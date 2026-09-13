#!/usr/bin/env python3
"""Operation authority and runtime checks; distinct from grammar qualification."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SETUP = '''
    auto *storage# = unsafe alloc [1] i32
    auto address = unsafe *storage as Addr
    auto *writer# = unsafe (address as *i32)
'''
GENERIC_POSITIVE = '''fn put<T>(*out#:T, value:T) { out = value }
fn main() -> i32 {
''' + SETUP + '''
    put<i32>(*writer, 31)
    if writer != 31 { return 1 }
    put(*writer, 47)
    if writer != 47 { return 2 }
    unsafe free [1] *storage
    return 0
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-permission-operations-') as directory:
        work = args.keep_dir or Path(directory)
        work.mkdir(parents=True, exist_ok=True)
        positive = work / 'generic_write.tk'
        positive.write_text(GENERIC_POSITIVE)
        cases = [(positive, None)]
        cases.append((ROOT / 'tests/semantics/permission_views/byte_storage.tk', None))
        cases.append((ROOT / 'tests/semantics/permission_views/byte_storage_readonly.tk', 'E04571'))
        for name in ('out_param_scalar_copy_writeback_test', 'out_param_handle_rebind_control_test',
                     'out_param_handle_rebind_explicit_call_test', 'net_out_param_contract_test'):
            cases.append((ROOT / 'tests/pass' / (name + '.tk'), None))
        for name, code in (
            ('out_param_readonly_plain_argument_rejected', 'E04571'),
            ('net_out_param_readonly_rejected', 'E04571'),
            ('out_param_handle_rebind_missing_request_rejected', 'E04571'),
            ('out_param_addr_form_assignment_rejected', 'E0448')):
            cases.append((ROOT / 'tests/fail' / (name + '.tk'), code))
        for suffix in ('<i32>', ''):
            label = 'explicit' if suffix else 'inferred'
            source = work / ('generic_body_' + label + '.tk')
            source.write_text('fn bad<T>(*out:T, value:T) { out = value }\nfn main() -> i32 {\n' +
                              SETUP + f'bad{suffix}(*writer, 7)\n' +
                              'unsafe free [0] *storage\nreturn 0\n}\n')
            cases.append((source, 'E04572'))
            source = work / ('generic_caller_' + label + '.tk')
            source.write_text('fn put<T>(*out#:T, value:T) { out = value }\nfn main() -> i32 {\n' +
                              SETUP + 'auto *readonly = unsafe (address as *i32)\n' +
                              f'put{suffix}(*readonly, 7)\n' +
                              'unsafe free [0] *storage\nreturn 0\n}\n')
            cases.append((source, 'E04571'))
        # Keep current-library integration last so an unrelated shared library
        # compile blocker does not hide the direct operation controls. It still
        # fails this gate; it is not skipped or counted as a positive result.
        cases.sort(key=lambda item: item[0].name.startswith('net_out_param_'))
        for source, code in cases:
            scope = ROOT if source.is_relative_to(ROOT) else work
            prefix = [str(compiler), '--workspace-node', 'permission-operations',
                      '--workspace-root', str(scope), str(source)]
            def run(*flags):
                return subprocess.run(prefix + list(map(str, flags)), cwd=ROOT,
                                      env=env, text=True, capture_output=True, timeout=60)
            normal = run('--check-only')
            shadow = run('--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode and normal.stderr == shadow.stderr, source.name
            if code:
                assert normal.returncode == 1 and f'error[{code}]' in normal.stderr, (source.name, normal.stderr)
                json.loads(shadow.stdout)
                for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                    artifact = work / (source.stem + suffix)
                    failed = run(flag, '-o', artifact)
                    assert failed.returncode == 1 and not artifact.exists(), (source.name, failed.stderr)
            else:
                assert normal.returncode == 0, (source.name, normal.stderr)
                json.loads(shadow.stdout)
                artifact = work / source.stem
                built = run('-o', artifact)
                assert built.returncode == 0, (source.name, built.stderr)
                executed = subprocess.run([str(artifact)], capture_output=True, text=True, timeout=15)
                assert executed.returncode == 0, (source.name, executed.returncode, executed.stderr)
            print('PASS operation ' + source.name, flush=True)
        print(f'{sum(code is None for _, code in cases)} runtime cases; {sum(code is not None for _, code in cases)} rejection cases; explicit/inferred generic authority; normal/shadow parity')


if __name__ == '__main__':
    main()
