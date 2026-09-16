#!/usr/bin/env python3
"""Channel private storage: real programs, failure responsibility and denials."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / 'tests/semantics/channel_storage'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    cc = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-channel-storage-') as directory:
        work = Path(directory)
        observer = work / 'observer.o'
        subprocess.run(['clang', '-std=c11', '-pthread', '-c',
                        str(ROOT / 'tests/runtime/channel_storage_observer.c'), '-o', str(observer)], check=True)
        def compile(source, *flags):
            return subprocess.run([str(cc), str(source), *map(str, flags)], cwd=ROOT,
                                  env=env, capture_output=True, text=True, timeout=90)
        def parity(source, expected=0, reason=None):
            normal = compile(source, '--check-only')
            shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == expected, (source, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, source
            if reason: assert reason in normal.stderr, normal.stderr
        for source in (ROOT / 'tests/semantics/rc13_thread_migration_pending/sender_capture.tk',
                       CASES / 'lifecycle.tk', CASES / 'mixed_capture.tk', CASES / 'mixed_cleanup.tk'):
            parity(source)
            binary = work / source.stem
            built = compile(source, observer, '-o', binary)
            assert built.returncode == 0, built.stderr
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert ran.returncode == 0, (source, ran.returncode, ran.stderr)
            print('PASS runtime ' + source.name, flush=True)
        # Fault only the generated SDK native calls. The runtime's own control
        # mutex and its protocol are not interposed or changed.
        for name, expected, marker in (('lock_failure', 0, 'CHANNEL_LOCK_FAULT'),
                                       ('clone_failure', None, 'Sender clone'),
                                       ('init_failure', 134, 'CHANNEL_INIT_FAULT'),
                                       ('allocation_failure', None, 'CHANNEL_ALLOC_FAULT')):
            source = CASES / (name + '.tk')
            parity(source)
            ir = work / (name + '.ll')
            emitted = compile(source, '--emit-llvm', '-o', ir)
            assert emitted.returncode == 0, emitted.stderr
            text = ir.read_text()
            assert '@pthread_mutex_lock(' in text and '@pthread_mutex_init(' in text
            text = text.replace('@pthread_mutex_lock(', '@channel_native_lock(')
            text = text.replace('@pthread_mutex_init(', '@channel_native_init(')
            if name == 'allocation_failure':
                assert '@malloc(' in text
                text = text.replace('@malloc(', '@channel_native_malloc(')
            injected = work / (name + '-fault.ll')
            injected.write_text(text)
            binary = work / name
            linked = subprocess.run(['clang', str(injected), str(observer),
                str(ROOT / 'lib/sys/toka_rt.c'),
                '-pthread', '-lm', '-o', str(binary)], capture_output=True, text=True, timeout=60)
            assert linked.returncode == 0, linked.stderr
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert marker in ran.stderr, (name, ran.returncode, ran.stderr)
            if expected is None: assert ran.returncode in (1, 134, -6), (name, ran.returncode, ran.stderr)
            else: assert ran.returncode == expected, (name, ran.returncode, ran.stderr)
            print('PASS native failure ' + name + ' rc=' + str(ran.returncode), flush=True)
        for name, reason in (('forged', 'E0418'), ('private_write', 'E0418'),
                             ('borrowed_element', 'E0621'), ('invalidated', 'EnvironmentLifetimeUnproven'),
                             ('mixed_unqualified', 'EnvironmentLifetimeUnproven')):
            source = CASES / (name + '.tk')
            parity(source, 1, reason)
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                target = work / (name + suffix)
                failed = compile(source, flag, '-o', target)
                assert failed.returncode == 1 and reason in failed.stderr and not target.exists(), failed.stderr
            print('PASS rejected ' + name, flush=True)
        source = ROOT / 'tests/semantics/rc13_thread_migration_pending/sender_capture.tk'
        for fault in ('missing', 'origin', 'factory', 'allocation', 'type', 'element', 'thread-list'):
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                target = work / ('fault-' + fault + suffix)
                failed = compile(source, flag, '--native-sync-witness-fault=' + fault, '-o', target)
                assert failed.returncode != 0 and 'E0701' in failed.stderr and not target.exists(), (fault, failed.stderr)
        print('PASS 14 wrong/missing-plan no-artifact checks; no skips', flush=True)

if __name__ == '__main__':
    main()
