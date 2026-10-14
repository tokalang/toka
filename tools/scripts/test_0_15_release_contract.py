import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import test_release_platform_policy as cases
import release_platform_policy as policy
import verify_qualified_draft as draft
import verify_release_promotion as promotion
import verify_release_qualification as qualification
from test_release_workflow import report


class Contract015(unittest.TestCase):
    def test_versions_and_platforms(self):
        self.assertEqual(policy.core_targets('v0.11.0'), policy.LEGACY)
        self.assertEqual(policy.core_targets('v0.12.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.1'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.14.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.15.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.15.1'), policy.CORE)
        for tag in ('v0.15.00', 'v0.15.01', 'v0.15.0-rc.1', 'v0.16.0', 'v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'), 'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.13.1'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.14.0'), 'toka.release-platforms.0.14.v1')
        self.assertEqual(policy.policy_id('v0.15.0'), 'toka.release-platforms.0.15.v1')
        self.assertEqual(policy.policy_id('v0.15.1'), 'toka.release-platforms.0.15.v1')

    def test_installed_protocol_015_across_release_chain(self):
        old = cases.TAG
        cases.TAG = 'v0.15.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                proof = cases.valid_candidate_015(cases.SHA, 'v0.15.0', 'linux-x64')
                gate = report('linux-x64', cases.SHA, 'v0.15.0')
                gate['stages'][-1]['counts']['candidate_015'] = copy.deepcopy(proof)
                self.assertEqual(qualification.report_errors(gate, cases.SHA, 'v0.15.0'), [])
                self.assertEqual(policy.summary_errors(d['summary'], cases.SHA, 'v0.15.0'), [])
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                # Negative controls on candidate_015 properties
                mutations = [
                    ('missing_version', lambda p: p.pop('version')),
                    ('none_version', lambda p: p.update(version=None)),
                    ('bool_version', lambda p: p.update(version=True)),
                    ('bad_version', lambda p: p.update(version=2)),
                    ('bad_schema', lambda p: p.update(schema='toka.wrong-schema')),
                    ('bad_result', lambda p: p.update(result='fail')),
                    ('dirty_build_testing', lambda p: p.update(build_testing=True)),
                    ('wrong_revision', lambda p: p.update(candidate_revision='0' * 40)),
                    ('wrong_label', lambda p: p.update(version_label='v0.14.0')),
                    ('missing_groups', lambda p: p.update(groups=[])),
                    ('extra_groups', lambda p: p.update(groups=['G-recursion', 'extra'])),
                    ('missing_receipts_key', lambda p: p.pop('receipts')),
                    ('empty_receipts', lambda p: p.update(receipts=[])),
                    ('wrong_receipt_type', lambda p: p['receipts'].__setitem__(0, "invalid")),
                    ('not_ok_receipt', lambda p: p['receipts'][0].update(ok=False)),
                    ('counts_mismatch', lambda p: p['counts'].update(total_receipts=37)),
                    ('counts_failed_nonzero', lambda p: p['counts'].update(failed=1)),
                    ('missing_tools', lambda p: p.pop('four_tool_versions')),
                    ('wrong_tool_version', lambda p: p['four_tool_versions'].update(tokac='tokac 0.14.0')),
                    ('wrong_fixtures_digest', lambda p: p.update(fixtures_digest='0' * 64)),
                    ('wrong_control_sha', lambda p: p.update(control_script_sha256='0' * 64)),
                    ('missing_target', lambda p: p.pop('target', None)),
                    ('all_commands_failed', lambda p: [r.update(exit_code=1) for r in p['receipts'] if r.get('name', '').startswith(('version-', 'pos-'))]),
                    ('noop_programs', lambda p: [r['argv'].__setitem__(0, '/bin/true' if r['exit_code'] == 0 else '/bin/false') for r in p['receipts']]),
                    ('wrong_fixture', lambda p: [r['argv'].__setitem__(i, '/fixtures/unrelated.tk') for r in p['receipts'] for i, a in enumerate(r['argv']) if a.endswith('.tk')]),
                    ('wrong_run_binary', lambda p: [r['argv'].__setitem__(0, '/bin/true') for r in p['receipts'] if '-run' in r.get('name', '')]),
                    ('contradictory_domain_and_artifacts', lambda p: [r.update(result='failed', diagnostic='E00000', artifact_absent=False, stdout='failure instead of assertions', stderr='ordinary error') for r in p['receipts']]),
                    ('nonfinite_timing', lambda p: [r.update(execution_ms=float('nan')) for r in p['receipts']]),
                    ('missing_raw_outputs', lambda p: [(r.pop('stdout', None), r.pop('stderr', None)) for r in p['receipts']]),
                    ('missing_negative_stderr', lambda p: [r.pop('stderr', None) for r in p['receipts'] if r['exit_code'] == 1]),
                    ('invalid_raw_output_types', lambda p: [r.update(stdout=None, stderr=[]) for r in p['receipts']]),
                    ('bad_stdout_hash', lambda p: [r.update(stdout_sha256='0' * 64) for r in p['receipts']]),
                    ('bad_stderr_hash', lambda p: [r.update(stderr_sha256='0' * 64) for r in p['receipts']]),
                    ('negative_artifacts_present', lambda p: [r.update(artifact_absent=False) for r in p['receipts'] if r.get('name', '').startswith('neg-')]),
                    ('duplicate_receipt_names', lambda p: [r.update(name='same-command') for r in p['receipts']]),
                    ('positive_nonzero_exit', lambda p: [r.update(exit_code=1) for r in p['receipts'] if r.get('name') == 'pos-cleanup_drop-run']),
                    ('positive_corrupted_run_binary', lambda p: [r.update(argv=['/tmp/unrelated_bin']) for r in p['receipts'] if r.get('name') == 'pos-tki-consumer-run']),
                    ('negative_exit_zero', lambda p: [r.update(exit_code=0) for r in p['receipts'] if r.get('name', '').startswith('neg-')]),
                    ('positive_unexpected_diagnostic', lambda p: [r.update(diagnostic='E0406') for r in p['receipts'] if r.get('name', '').startswith('pos-')]),
                    ('abnormal_signal', lambda p: [r.update(signal=15) for r in p['receipts']]),
                    ('abnormal_error', lambda p: [r.update(error='aborted') for r in p['receipts']]),
                    ('positive_compiler_echo', lambda p: [r['argv'].__setitem__(0, '/bin/echo') for r in p['receipts'] if r.get('name', '').startswith('pos-') and r.get('name', '').endswith('-compile')]),
                    ('negative_wrong_modes', lambda p: [
                        r['argv'].__setitem__(slice(None), [a for a in r['argv'] if a not in ('-c', '--emit-llvm', '--check-only')]) or r['argv'].insert(1, '--check-only')
                        for r in p['receipts'] if r.get('name') in ('neg-async-direct-object', 'neg-async-direct-ir', 'neg-async-direct-compile')
                    ]),
                    ('version_command_without_version_flag', lambda p: [
                        r['argv'].__setitem__(slice(None), [a for a in r['argv'] if a != '--version'])
                        for r in p['receipts'] if r.get('name', '').startswith('version-')
                    ]),
                    ('missing_sourceless_facts', lambda p: p.pop('source_hidden_tki', None)),
                    ('false_sourceless_facts', lambda p: p.update(source_hidden_tki={
                        'provider_tki_sha256': '0' * 64,
                        'provider_object_sha256': '0' * 64,
                        'missing_interface_rejected': False,
                        'corrupt_interface_rejected': False,
                        'consumer_result': 'fail',
                        'provider_source_present': True,
                    })),
                    ('tki_consumer_missing_import_and_object', lambda p: [
                        r['argv'].__setitem__(slice(None), [x for j, x in enumerate(r['argv']) if x != '-I' and (j == 0 or r['argv'][j - 1] != '-I') and not x.endswith('.o')])
                        for r in p['receipts'] if r.get('name') == 'pos-tki-consumer-compile'
                    ]),
                    ('wrong_exact_tool_version', lambda p: [
                        p['four_tool_versions'].update({k: v.replace('0.15.0', '0.15.00') for k, v in p['four_tool_versions'].items()}),
                        [r.update(stdout=r['stdout'].replace('0.15.0', '0.15.00'), stdout_sha256=hashlib.sha256(r['stdout'].replace('0.15.0', '0.15.00').encode('utf-8')).hexdigest()) for r in p['receipts'] if r.get('name', '').startswith('version-')]
                    ]),
                    ('different_tokac_same_basename', lambda p: [
                        r['argv'].__setitem__(0, '/unrelated/bin/tokac')
                        for r in p['receipts'] if r.get('name', '').startswith('pos-') and r.get('name', '').endswith('-compile')
                    ]),
                    ('compile_with_version_action', lambda p: [
                        r['argv'].insert(1, '--version')
                        for r in p['receipts'] if r.get('name', '').startswith('pos-') and r.get('name', '').endswith('-compile')
                    ]),
                    ('wrong_tki_input_paths', lambda p: [
                        r['argv'].__setitem__(r['argv'].index('-I') + 1, '/unrelated/interfaces') or
                        r['argv'].__setitem__(slice(None), [('/unrelated/other.o' if a.endswith('.o') else a) for a in r['argv']])
                        for r in p['receipts'] if r.get('name') == 'pos-tki-consumer-compile'
                    ]),
                    ('missing_source_absence_fact', lambda p: p['source_hidden_tki'].pop('provider_source_present', None)),
                    ('source_present_integer_one', lambda p: p['source_hidden_tki'].update(provider_source_present=1)),
                    ('unbound_provider_hashes', lambda p: p['source_hidden_tki'].update(provider_tki_sha256='1' * 64, provider_object_sha256='2' * 64)),
                    ('prerelease_tool_version', lambda p: [
                        p['four_tool_versions'].update({k: v.replace('0.15.0', '0.15.0-rc.1') for k, v in p['four_tool_versions'].items()}),
                        [r.update(stdout=r['stdout'].replace('0.15.0', '0.15.0-rc.1'), stdout_sha256=hashlib.sha256(r['stdout'].replace('0.15.0', '0.15.0-rc.1').encode('utf-8')).hexdigest()) for r in p['receipts'] if r.get('name', '').startswith('version-')]
                    ]),
                    ('noninteger_counts', lambda p: p['counts'].update(passed=38.0, failed=False)),
                    ('delete_consumed_hashes', lambda p: (p['source_hidden_tki'].pop('consumed_tki_sha256', None), p['source_hidden_tki'].pop('consumed_object_sha256', None))),
                    ('delete_negative_commands', lambda p: (p['source_hidden_tki'].pop('missing_interface_command', None), p['source_hidden_tki'].pop('corrupt_interface_command', None))),
                    ('exit_only_negative_commands', lambda p: p['source_hidden_tki'].update(missing_interface_command={'exit_code': 1})),
                    ('negative_commands_bin_false', lambda p: p['source_hidden_tki']['missing_interface_command'].update(argv=['/bin/false'], cmd=['/bin/false'])),
                    ('negative_commands_boolean_exit', lambda p: p['source_hidden_tki']['missing_interface_command'].update(exit_code=True)),
                    ('negative_commands_signal_error', lambda p: p['source_hidden_tki']['missing_interface_command'].update(signal=15, termination='terminated', error='failed to execute')),
                    ('negative_commands_bad_hash', lambda p: p['source_hidden_tki']['missing_interface_command'].update(stdout_sha256='not-a-hash', stderr_sha256='not-a-hash')),
                    ('random_provider_hash_without_consumed', lambda p: [
                        p['source_hidden_tki'].pop('consumed_tki_sha256', None),
                        p['source_hidden_tki'].pop('consumed_object_sha256', None),
                        p['source_hidden_tki'].update(provider_tki_sha256=hashlib.sha256(b'unrelated interface').hexdigest(), provider_object_sha256=hashlib.sha256(b'unrelated object').hexdigest())
                    ]),
                    ('compile_explain_action', lambda p: [
                        r['argv'].insert(1, '--explain=E0406')
                        for r in p['receipts'] if r.get('name', '').startswith('pos-') and r.get('name', '').endswith('-compile')
                    ]),
                    ('duplicate_output_flag', lambda p: [
                        r['argv'].extend(['-o', '/unrelated/replaced-output'])
                        for r in p['receipts'] if r.get('name', '').startswith('pos-') and r.get('name', '').endswith('-compile')
                    ]),
                    ('delete_raw_outputs', lambda p: [
                        p['source_hidden_tki'][k].pop('stdout', None) or p['source_hidden_tki'][k].pop('stderr', None)
                        for k in ('missing_interface_command', 'corrupt_interface_command')
                    ]),
                    ('delete_context_timing_absence', lambda p: [
                        [p['source_hidden_tki'][k].pop(f, None) for f in ('cwd', 'execution_ms', 'elapsed_seconds', 'artifact_absent', 'signal', 'termination', 'error', 'ok', 'result')]
                        for k in ('missing_interface_command', 'corrupt_interface_command')
                    ]),
                    ('delete_all_optional_tki_facts', lambda p: [
                        [p['source_hidden_tki'][k].pop(f, None) for f in ('stdout', 'stderr', 'cwd', 'execution_ms', 'elapsed_seconds', 'artifact_absent', 'signal', 'termination', 'error', 'ok', 'result')]
                        for k in ('missing_interface_command', 'corrupt_interface_command')
                    ]),
                    ('ordinary_error_present', lambda p: [
                        p['source_hidden_tki'][k].update(
                            stdout='', stderr='error: linker failed because an unrelated library was not found\n',
                            stdout_sha256=hashlib.sha256(b'').hexdigest(),
                            stderr_sha256=hashlib.sha256(b'error: linker failed because an unrelated library was not found\n').hexdigest()
                        )
                        for k in ('missing_interface_command', 'corrupt_interface_command')
                    ]),
                    ('ordinary_error_hidden', lambda p: [
                        p['source_hidden_tki'][k].update(
                            stdout='', stderr='error: linker failed because an unrelated library was not found\n',
                            stdout_sha256=hashlib.sha256(b'').hexdigest(),
                            stderr_sha256=hashlib.sha256(b'error: linker failed because an unrelated library was not found\n').hexdigest()
                        ) or (p['source_hidden_tki'][k].pop('stdout', None), p['source_hidden_tki'][k].pop('stderr', None))
                        for k in ('missing_interface_command', 'corrupt_interface_command')
                    ]),
                ]
                for desc, mutate in mutations:
                    with self.subTest(mutation=desc):
                        bad = copy.deepcopy(proof)
                        mutate(bad)
                        gate_bad = copy.deepcopy(gate)
                        gate_bad['stages'][-1]['counts']['candidate_015'] = bad
                        self.assertTrue(qualification.report_errors(gate_bad, cases.SHA, 'v0.15.0'))

                        summary_bad = copy.deepcopy(d['summary'])
                        summary_bad['reports'][0]['candidate_015'] = bad
                        self.assertTrue(policy.summary_errors(summary_bad, cases.SHA, 'v0.15.0'))

                # Verify historical protocols (v0.11.0 and v0.12.0) are not retroactively burdened
                for hist_tag in ('v0.11.0', 'v0.12.0'):
                    self.assertEqual(qualification.report_errors(report('linux-x64', cases.SHA, hist_tag), cases.SHA, hist_tag), [])
        finally:
            cases.TAG = old

    def test_draft_promotion_and_negatives_015(self):
        old = cases.TAG
        cases.TAG = 'v0.15.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                # Promotion rejection when replay receipts candidate_015 is mutated or missing
                for p_desc, p_mutate in [
                    ('missing_candidate_015', lambda r: r.pop('candidate_015', None)),
                    ('none_candidate_015', lambda r: r.update(candidate_015=None)),
                    ('header_only_candidate_015', lambda r: r.update(candidate_015={
                        'schema': 'toka.0.15-candidate-controls', 'version': 1, 'result': 'pass',
                        'candidate_revision': cases.SHA, 'version_label': 'v0.15.0', 'build_testing': False,
                        'groups': ['G-recursion']
                    })),
                    ('corrupt_receipt_type', lambda r: r['candidate_015']['receipts'].__setitem__(0, 'bad')),
                    ('receipt_command_not_ok', lambda r: r['candidate_015']['receipts'][0].update(ok=False)),
                ]:
                    with self.subTest(replay_mutation=p_desc):
                        d_bad = copy.deepcopy(d)
                        p_mutate(d_bad['replay']['receipts'][0])
                        cases.dump(a.replay_receipt, d_bad['replay'])
                        self.assertTrue(promotion.validate(a))
                # Restore valid replay receipt
                cases.dump(a.replay_receipt, d['replay'])
                self.assertEqual(promotion.validate(a), [])

                for tag in ('v0.15.01', 'v0.15.0-rc.1', 'v0.16.0'):
                    b = copy.copy(a)
                    b.tag_name = tag
                    with self.assertRaises(ValueError):
                        draft.validate(b)

                b = copy.copy(a)
                b.candidate_sha = 'b' * 40
                with self.assertRaises(ValueError):
                    draft.validate(b)

                item = next(a.qualified_archives_dir.glob('*/*.tar.gz'))
                item.unlink()
                with self.assertRaises(ValueError):
                    draft.validate(a)
        finally:
            cases.TAG = old


if __name__ == '__main__':
    unittest.main()
