#!/usr/bin/env python3
"""Original library behavior after removing cede from temporary results."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = (
    'g10_async_http_server_test',
    'g10_http_empty_header_value',
    'g10_http_phase1_test',
    'g10_net_http_server_test',
    'g10_websocket',
    'g12_stdx_http_client_server_test',
    'g12_stdx_https_wss_test',
    'g18_header_map_lookup_miss',
    'g12_stdx_websocket_malformed_test',
    'g12_stdx_websocket_test',
    'g13_stdx_net_zero_copy_bench',
    'g16_stdx_http_server_connection_test',
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-release-temporaries-') as directory:
        for name in CASES:
            prefix = [str(compiler), '--workspace-node', 'toka-tests-v1',
                      '--workspace-root', str(ROOT), str(ROOT / 'tests/pass' / (name + '.tk'))]
            normal = subprocess.run(prefix + ['--check-only'], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=90)
            shadow = subprocess.run(prefix + ['--check-only', '--non-call-transfer-shadow=json'],
                                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            assert normal.returncode == shadow.returncode == 0, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, name
            binary = Path(directory) / name
            built = subprocess.run(prefix + ['-o', str(binary)], cwd=ROOT, env=env,
                                   capture_output=True, text=True, timeout=90)
            assert built.returncode == 0, (name, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
            assert ran.returncode == 0, (name, ran.returncode, ran.stdout, ran.stderr)
            print('PASS ' + name, flush=True)


if __name__ == '__main__':
    main()
