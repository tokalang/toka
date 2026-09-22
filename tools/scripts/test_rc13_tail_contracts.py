#!/usr/bin/env python3
"""Discriminating source, permission and storage-level controls for PASS-tail repairs."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
REPLAY = 'tests/semantics/tki_replay/cases/'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    cases = [
        ('tests/semantics/rc13_pass_tail/descriptor_live.tk', None),
        ('tests/semantics/rc13_pass_tail/descriptor_escape.tk', 'E0455'),
        ('tests/semantics/rc13_pass_tail/global_source.tk', None),
        ('tests/semantics/rc13_pass_tail/global_source_readonly.tk', 'E04571'),
        ('tests/semantics/rc13_pass_tail/global_name_local_escape.tk', 'E0455'),
        (REPLAY+'permission_005_partial_cede_lifecycle/pass_async_cancel_cleanup.tk', None),
        (REPLAY+'eff_member_001_return_deps/pass_select_first_release.tk', None),
        (REPLAY+'permission_003_independent_flow/pass_cede_unique_readonly_source_rebuilds_payload.tk', None),
        (REPLAY+'permission_004_referent_ceiling/fail_cede_preserves_blocked_field.tk', 'E0443'),
        (REPLAY+'permission_004_referent_ceiling/fail_direct_unique_move_preserves_ceiling.tk', 'E0443'),
        (REPLAY+'permission_003_independent_flow/fail_direct_unique_move_borrow_conflict.tk', 'E0440'),
        (REPLAY+'permission_002_shared_flow/fail_readonly_field_fresh_binding.tk', 'E04661'),
        (REPLAY+'async_suspend_001_return_deps/pass_static_wrapper_replacement.tk', None),
        (REPLAY+'async_suspend_001_return_deps/pass_static_wrapper_replacement_in_async.tk', None),
        (REPLAY+'async_suspend_001_return_deps/pass_external_owner_lives.tk', None),
        (REPLAY+'async_suspend_001_return_deps/pass_external_owner_lives_in_async.tk', None),
        (REPLAY+'async_suspend_001_return_deps/fail_replace_awaited_source.tk', 'E0442'),
        (REPLAY+'async_suspend_001_return_deps/fail_replace_awaited_source_in_async.tk', 'E0442'),
    ]
    with tempfile.TemporaryDirectory(prefix='toka-tail-contracts-') as temporary:
        work = Path(temporary)
        for ordinal, (relative, error) in enumerate(cases):
            source = ROOT / relative
            expected = 1 if error else 0
            def compile(*flags):
                result = subprocess.run([str(tokac), str(source), *map(str, flags)], cwd=ROOT,
                                        env=env, capture_output=True, text=True, timeout=90)
                assert result.returncode == expected, (source, result.stderr)
                if error:
                    assert f'error[{error}]' in result.stderr and str(source) in result.stderr, result.stderr
                return result
            normal = compile('--check-only')
            shadow = compile('--check-only', '--non-call-transfer-shadow=json')
            assert normal.stderr == shadow.stderr, (source, normal.stderr, shadow.stderr)
            for mode, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (str(ordinal) + suffix)
                compile(mode, '-o', output)
                assert output.exists() != bool(error), output
            if not error:
                output = work / str(ordinal)
                compile('-o', output)
                result = subprocess.run([str(output)], cwd=work, env=env,
                                        capture_output=True, text=True, timeout=60)
                assert result.returncode == 0, (source, result.stdout, result.stderr)
            print('PASS', relative, flush=True)


if __name__ == '__main__':
    main()
