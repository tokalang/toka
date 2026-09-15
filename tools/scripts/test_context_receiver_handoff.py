#!/usr/bin/env python3
"""Keep receiver ownership intact through binding, Option and Context returns."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COMMON = '''import std/channel::{channel, Receiver}
import core/option::{Option}
import std/context::{background, with_cancel, BackgroundContext}
import std/task::{block_on}
fn worker(cede rx: Receiver<bool>) -> async i32 {
    auto rx# = cede rx
    auto received = rx#.recv()
    assert(received.is_ok(), "worker receive")
    assert(received.unwrap().is_none(), "worker observes closed channel")
    return 0
}
fn wrap(cede rx: Receiver<bool>) -> Option<Receiver<bool>> {
    return Option<Receiver<bool>>::Some(cede rx)
}
'''
ROUTES = {
    'direct': '''auto pair = channel<bool>()
    auto rx# = cede pair.rx
    cede pair.tx
''',
    'wrapped': '''auto pair = channel<bool>()
    auto result = wrap(cede pair.rx)
    auto rx# = result.unwrap()
    cede pair.tx
''',
    'context': '''auto bg# = background()
    auto result = with_cancel<BackgroundContext>(cede bg)
    auto ctx# = cede result.ctx
    auto canceler = result.canceler.clone()
    canceler.call()
    canceler.call()
    auto result_rx = ctx#.done()
    auto rx# = result_rx.unwrap()
''',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-context-handoff-') as directory:
        work = Path(directory)
        for name, body in ROUTES.items():
            source = work / (name + '.tk')
            source.write_text(COMMON + 'fn main()->i32 {\n' + body +
                              'auto h = worker(cede rx).start\nreturn block_on<i32>(h)\n}\n')
            base = [str(compiler), '--workspace-node', 'context-handoff',
                    '--workspace-root', str(work), str(source)]
            def compile_with(flags):
                return subprocess.run(base + flags, cwd=ROOT, env=env,
                                      capture_output=True, text=True, timeout=60)
            normal = compile_with(['--check-only'])
            assert normal.returncode == 0, (name, normal.stderr)
            for flag in ('--non-call-transfer-shadow=json', '--call-transfer-shadow=json'):
                shadow = compile_with(['--check-only', flag])
                assert shadow.returncode == normal.returncode and shadow.stderr == normal.stderr, (name, flag, shadow.stderr)
                evidence = json.loads(shadow.stdout)
                if flag == '--call-transfer-shadow=json':
                    worker = [record for record in evidence['records']
                              if record.get('callee') == 'worker']
                    assert len(worker) == 1, (name, worker)
                    assert worker[0]['execution_boundary'] == 'StartHandoff'
                    plan = worker[0]['stage0']
                    expected = dict(actual_type='Receiver_M_bool#',
                                    formal_type='Receiver_M_bool',
                                    ownership='OwnedValue', outcome='Admitted',
                                    value_production='MoveOwned',
                                    source='InvalidateSubtree',
                                    drop='CalleeAssumesLiability',
                                    dependency_complete=True)
                    assert all(plan[key] == value for key, value in expected.items()), (name, plan)
                    assert plan['exact_path'] and not plan['dependency_roots'], (name, plan)
            binary = work / name
            built = compile_with(['-o', str(binary)])
            assert built.returncode == 0, (name, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert ran.returncode == 0, (name, ran.stdout, ran.stderr)
            # The successful handoff consumes the exact binding once.
            source.write_text(source.read_text().replace(
                'return block_on<i32>(h)',
                'auto again = worker(cede rx).start\nreturn block_on<i32>(h)'))
            rejected_output = work / (name + '-rejected.o')
            rejected = compile_with(['-c', '-o', str(rejected_output)])
            assert rejected.returncode == 1 and 'E0438' in rejected.stderr, (name, rejected.stderr)
            assert not rejected_output.exists()
            print('PASS receiver handoff: ' + name, flush=True)

        # Preserve the original cancellation, timeout, parent propagation and
        # value-query assertions; this is not a rewritten equivalent fixture.
        binary = work / 'g09_context'
        built = subprocess.run(
            [str(compiler), '--workspace-node', 'toka-tests-v1',
             '--workspace-root', str(ROOT), str(ROOT / 'tests/pass/g09_context.tk'),
             '-o', str(binary)], cwd=ROOT, env=env,
            capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 0, (ran.stdout, ran.stderr)
        assert 'All std/context tests passed!' in ran.stdout, ran.stdout
        print('PASS original g09_context', flush=True)


if __name__ == '__main__':
    main()
