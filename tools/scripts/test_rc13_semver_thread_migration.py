#!/usr/bin/env python3
"""Run the source-only semver/thread migration and existing thread controls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ('g15_stdx_semver_test.tk', 'g10_net_read_exact.tk',
         'g10_net_tcp_echoserver.tk', 'g09_thread_example.tk', 'g09_sync_condvar.tk')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-semver-thread-') as temporary:
        work = args.keep_dir or Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        rows = []
        for name in CASES:
            command = [str(compiler), '--workspace-node', 'toka-tests-v1',
                       '--workspace-root', str(ROOT), str(ROOT / 'tests/pass' / name)]
            def compile(flags):
                return subprocess.run(command + flags, cwd=ROOT, env=env,
                                      capture_output=True, text=True, timeout=90)
            normal = compile(['--check-only'])
            shadow = compile(['--check-only', '--non-call-transfer-shadow=json'])
            row = dict(case=name, check_rc=normal.returncode, stderr=normal.stderr,
                       parity=normal.returncode == shadow.returncode and normal.stderr == shadow.stderr,
                       build_rc=None, run_rc=None)
            if normal.returncode == 0:
                binary = work / Path(name).stem
                built = compile(['-o', str(binary)])
                row.update(build_rc=built.returncode, build_stderr=built.stderr)
                if built.returncode == 0:
                    try:
                        ran = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                             capture_output=True, text=True, timeout=40)
                        row.update(run_rc=ran.returncode, stdout=ran.stdout, runtime_stderr=ran.stderr)
                        if 'skipping' in ran.stdout.lower():
                            row['run_rc'] = 'skipped-network-not-qualified'
                    except subprocess.TimeoutExpired:
                        row['run_rc'] = 'timeout'
            rows.append(row)
            print(name, row['check_rc'], row['build_rc'], row['run_rc'], flush=True)
            (work / 'results.json').write_text(json.dumps(rows, indent=2))
        assert all(r['check_rc'] == r['build_rc'] == r['run_rc'] == 0 and r['parity'] for r in rows), str(work / 'results.json')

if __name__ == '__main__':
    main()
