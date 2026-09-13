#!/usr/bin/env python3
"""The exact twelve net recoveries recorded by the prior worker, not the older async list."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
CASES = (
    'g04_test_std_net_dns.tk', 'g04_test_std_net_udp.tk',
    'g09_async_context_redline_test.tk', 'g09_async_context_timeout_integration_test.tk',
    'g09_async_reactor_tokenization_test.tk', 'g10_async_net_test.tk',
    'g12_stdx_tls_test.tk', 'g13_net_buffer_abi_test.tk', 'g15_std_random_test.tk',
    'g16_async_accept_context_test.tk', 'g16_ioerror_try_api_test.tk',
    'g16_task_scope_result_cancel_test.tk',
)
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'), TOKAC=str(compiler))
    with tempfile.TemporaryDirectory(prefix='toka-permission-net-') as directory:
        work = args.keep_dir or Path(directory)
        work.mkdir(parents=True, exist_ok=True)
        results = []
        for name in CASES:
            source = ROOT / 'tests/pass' / name
            binary = work / source.stem
            prefix = [str(compiler), '--workspace-node', 'toka-tests-v1', '--workspace-root', str(ROOT), str(source)]
            normal = subprocess.run(prefix + ['--check-only'], cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            shadow = subprocess.run(prefix + ['--check-only', '--non-call-transfer-shadow=json'], cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            parity = normal.returncode == shadow.returncode and normal.stderr == shadow.stderr
            built = subprocess.run([str(compiler), '--workspace-node', 'toka-tests-v1',
                                    '--workspace-root', str(ROOT), str(source), '-o', str(binary)],
                                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            row = dict(file=name, check_rc=normal.returncode, parity=parity, compile_rc=built.returncode, stderr=built.stderr, run_rc=None)
            if built.returncode == 0:
                try:
                    ran = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True,
                                         text=True, timeout=40)
                    row.update(run_rc=ran.returncode, stdout=ran.stdout, runtime_stderr=ran.stderr)
                except subprocess.TimeoutExpired:
                    row['run_rc'] = 'timeout'
            results.append(row)
            print(name, 'build=', row['compile_rc'], 'run=', row['run_rc'], flush=True)
        (work / 'results.json').write_text(json.dumps(results, indent=2))
        assert len(results) == 12 and all(r['check_rc'] == r['compile_rc'] == r['run_rc'] == 0 and r['parity'] for r in results), 'net qualification failed; see per-case results'
if __name__ == '__main__': main()
