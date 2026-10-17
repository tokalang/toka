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


class Contract018(unittest.TestCase):
    def test_versions_and_platforms(self):
        self.assertEqual(policy.core_targets('v0.11.0'), policy.LEGACY)
        self.assertEqual(policy.core_targets('v0.12.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.1'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.14.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.15.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.15.1'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.16.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.17.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.18.0'), policy.CORE)
        for tag in ('v0.18.00', 'v0.18.01', 'v0.18.0-rc.1', 'v0.19.0', 'v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'), 'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.13.1'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.14.0'), 'toka.release-platforms.0.14.v1')
        self.assertEqual(policy.policy_id('v0.15.0'), 'toka.release-platforms.0.15.v1')
        self.assertEqual(policy.policy_id('v0.15.1'), 'toka.release-platforms.0.15.v1')
        self.assertEqual(policy.policy_id('v0.16.0'), 'toka.release-platforms.0.16.v1')
        self.assertEqual(policy.policy_id('v0.17.0'), 'toka.release-platforms.0.17.v1')
        self.assertEqual(policy.policy_id('v0.18.0'), 'toka.release-platforms.0.18.v1')

    def test_installed_protocol_018_across_release_chain(self):
        old = cases.TAG
        cases.TAG = 'v0.18.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                proof = cases.valid_candidate_018(cases.SHA, 'v0.18.0', 'linux-x64')
                gate = report('linux-x64', cases.SHA, 'v0.18.0')
                gate['stages'][-1]['counts']['candidate_018'] = copy.deepcopy(proof)
                self.assertEqual(qualification.report_errors(gate, cases.SHA, 'v0.18.0'), [])
                self.assertEqual(policy.summary_errors(d['summary'], cases.SHA, 'v0.18.0'), [])
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                # Negative controls on candidate_018 properties
                mutations = [
                    ('missing_version', lambda p: p.pop('version')),
                    ('none_version', lambda p: p.update(version=None)),
                    ('bool_version', lambda p: p.update(version=True)),
                    ('bad_version', lambda p: p.update(version=2)),
                    ('bad_schema', lambda p: p.update(schema='toka.wrong-schema')),
                    ('bad_result', lambda p: p.update(result='fail')),
                    ('dirty_build_testing', lambda p: p.update(build_testing=True)),
                    ('none_build_testing', lambda p: p.update(build_testing=None)),
                    ('candidate_revision_mismatch', lambda p: p.update(candidate_revision='0'*40)),
                    ('version_label_mismatch', lambda p: p.update(version_label='v0.18.1')),
                    ('target_mismatch', lambda p: p.update(target='macos-arm64')),
                    ('missing_target', lambda p: p.pop('target')),
                    ('empty_target', lambda p: p.update(target='')),
                    ('bad_groups', lambda p: p.update(groups=['G-wrong'])),
                    ('bad_control_script_name', lambda p: p.update(control_script_name='wrong.py')),
                    ('bad_control_script_sha', lambda p: p.update(control_script_sha256='0'*64)),
                    ('missing_tki', lambda p: p.pop('source_hidden_tki')),
                    ('bad_tki_missing_iface', lambda p: p['source_hidden_tki'].update(missing_interface_rejected=False)),
                    ('bad_tki_corrupt_iface', lambda p: p['source_hidden_tki'].update(corrupt_interface_rejected=False)),
                    ('bad_tki_consumer_result', lambda p: p['source_hidden_tki'].update(consumer_result='fail')),
                    ('bad_tki_source_present', lambda p: p['source_hidden_tki'].update(provider_source_present=True)),
                    ('bad_tki_short_sha', lambda p: p['source_hidden_tki'].update(provider_tki_sha256='0000')),
                    ('missing_four_tools', lambda p: p.pop('four_tool_versions')),
                    ('bad_four_tools_val', lambda p: p['four_tool_versions'].update(tokac='tokac 0.17.0')),
                    ('rc_tool_version', lambda p: p['four_tool_versions'].update(tokac='tokac 1.0.0-rc.1')),
                    ('bad_neg_total', lambda p: p['negative_controls'].update(total=28)),
                    ('bad_neg_passed', lambda p: p['negative_controls'].update(passed=28)),
                    ('bad_pos_total', lambda p: p['positive_controls'].update(total=2)),
                    ('bad_pos_passed', lambda p: p['positive_controls'].update(passed=2)),
                    ('bad_pos_rc0', lambda p: p['positive_controls'].update(native_rc0=False)),
                    ('bad_conv_total', lambda p: p['compositional_convergence'].update(total=59)),
                    ('bad_conv_passed', lambda p: p['compositional_convergence'].update(passed=59)),
                    ('empty_receipts', lambda p: p.update(receipts=[])),
                    ('bad_neg_modes', lambda p: p['negative_controls'].update(modes=[])),
                    ('bad_consumed_tki_sha', lambda p: p['source_hidden_tki'].update(consumed_tki_sha256='0'*64)),
                    ('bad_consumed_obj_sha', lambda p: p['source_hidden_tki'].update(consumed_object_sha256='0'*64)),
                    ('garbage_receipt', lambda p: p.update(receipts=[{'garbage': True}])),
                    ('missing_receipt', lambda p: p['receipts'].pop()),
                    ('bad_neg_exit_code', lambda p: p['receipts'][-20].update(exit_code=0)),
                    ('bad_missing_iface_cmd', lambda p: p['source_hidden_tki'].pop('missing_interface_command')),
                    ('bad_missing_iface_cmd_exit', lambda p: p['source_hidden_tki']['missing_interface_command'].update(exit_code=0)),
                    ('bad_corrupt_iface_cmd', lambda p: p['source_hidden_tki'].pop('corrupt_interface_command')),
                    ('bad_counts_total', lambda p: p['counts'].update(total_receipts=109)),
                    ('bad_counts_groups', lambda p: p['counts'].update(groups={})),

                    # [P1-2a] source_hidden_tki command forgery mutations
                    ('p1_2a_bool_exit_code', lambda p: p['source_hidden_tki']['missing_interface_command'].update(exit_code=True)),
                    ('p1_2a_signal_exit', lambda p: p['source_hidden_tki']['missing_interface_command'].update(signal=9, termination='signal 9')),
                    ('p1_2a_cmd_bin_false', lambda p: p['source_hidden_tki']['missing_interface_command'].update(argv=['/bin/false', '--check-only', 'consumer.tk'])),
                    ('p1_2a_missing_flag', lambda p: p['source_hidden_tki']['missing_interface_command'].update(argv=[p['source_hidden_tki']['missing_interface_command']['argv'][0], 'consumer.tk'])),
                    ('p1_2a_stdout_sha_tamper', lambda p: p['source_hidden_tki']['missing_interface_command'].update(stdout_sha256='0'*64)),
                    ('p1_2a_missing_diagnostic', lambda p: p['source_hidden_tki']['missing_interface_command'].update(stderr='some unrelated error\n', stderr_sha256=hashlib.sha256(b'some unrelated error\n').hexdigest())),
                    ('p1_2a_cross_check_mismatch', lambda p: [
                        p['source_hidden_tki'].__setitem__('missing_interface_command', dict(p['source_hidden_tki']['missing_interface_command'])),
                        next(r for r in p['receipts'] if r['name'] == 'neg-tki-missing-iface').update(argv=['/sdk/bin/tokac', '--check-only', '/sdk/work/other_consumer.tk']),
                    ]),

                    # [P1-2b] negative command forgery mutations
                    ('p1_2b_untrusted_expected_code', lambda p: [
                        r.update(stderr='error[E9999]: fake\n', stderr_sha256=hashlib.sha256(b'error[E9999]: fake\n').hexdigest(), expected_code='E9999')
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-check-only'
                    ]),
                    ('p1_2b_missing_check_only_flag', lambda p: [
                        r.update(argv=[r['argv'][0], r['argv'][-1]])
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-check-only'
                    ]),
                    ('p1_2b_missing_compile_obj_flag', lambda p: [
                        r.update(argv=[r['argv'][0], r['argv'][2], '-o', r['argv'][4]])
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-compile-obj'
                    ]),
                    ('p1_2b_missing_emit_llvm_flag', lambda p: [
                        r.update(argv=[r['argv'][0], r['argv'][2], '-o', r['argv'][4]])
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-emit-llvm'
                    ]),
                    ('p1_2b_missing_fixture', lambda p: [
                        r.update(argv=[r['argv'][0], '--check-only'])
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-check-only'
                    ]),
                    ('p1_2b_compiler_bin_false', lambda p: [
                        r.update(argv=['/bin/false'] + r['argv'][1:])
                        for r in p['receipts'] if r['name'] == 'neg-neg1a_forged_block_on_noncede_start-check-only'
                    ]),

                    # [P1-2c] positive command forgery mutations
                    ('p1_2c_pos_tokac_bin_true', lambda p: [
                        r.update(argv=['/bin/true'])
                        for r in p['receipts'] if r['name'] == 'pos-pos1_async_close_7_lifecycle_paths-check-only'
                    ]),
                    ('p1_2c_pos_native_bin_true', lambda p: [
                        r.update(argv=['/bin/true'])
                        for r in p['receipts'] if r['name'] == 'pos-pos1_async_close_7_lifecycle_paths-native-execution'
                    ]),
                    ('p1_2c_pos_conv_bin_true', lambda p: [
                        r.update(argv=['/bin/true'])
                        for r in p['receipts'] if r['name'] == 'pos-compositional-convergence'
                    ]),
                    ('p1_2c_pos_conv_empty_output', lambda p: [
                        r.update(stdout='', stdout_sha256=hashlib.sha256(b'').hexdigest())
                        for r in p['receipts'] if r['name'] == 'pos-compositional-convergence'
                    ]),
                    ('p1_2c_pos_tki_bin_true', lambda p: [
                        r.update(argv=['/bin/true'])
                        for r in p['receipts'] if r['name'] == 'pos-tki-provider-compile'
                    ]),

                    # [P1-2d] version command forgery mutations
                    ('p1_2d_version_echo', lambda p: [
                        r.update(argv=['/bin/echo', '0.18.0'])
                        for r in p['receipts'] if r['name'] == 'version-toka'
                    ]),
                    ('p1_2d_version_extra_arg', lambda p: [
                        r.update(argv=[r['argv'][0], '--version', '--extra'])
                        for r in p['receipts'] if r['name'] == 'version-tokafmt'
                    ]),
                    ('p1_2d_version_empty_stdout', lambda p: [
                        r.update(stdout='', stdout_sha256=hashlib.sha256(b'').hexdigest())
                        for r in p['receipts'] if r['name'] == 'version-tokalsp'
                    ]),
                ]

                for name_mut, mut in mutations:
                    bad = copy.deepcopy(proof)
                    mut(bad)
                    errs = policy.candidate_018_receipt_errors(bad, cases.SHA, 'v0.18.0', target='linux-x64')
                    self.assertTrue(errs, f"Mutation {name_mut} failed to produce error")
        finally:
            cases.TAG = old

    def test_draft_promotion_and_negatives_018(self):
        old = cases.TAG
        cases.TAG = 'v0.18.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                # Promotion rejection when replay receipts candidate_018 is mutated or missing
                for p_desc, p_mutate in [
                    ('missing_candidate_018', lambda r: r.pop('candidate_018', None)),
                    ('none_candidate_018', lambda r: r.update(candidate_018=None)),
                    ('header_only_candidate_018', lambda r: r.update(candidate_018={
                        'schema': 'toka.0.18-candidate-controls', 'version': 1, 'result': 'pass',
                        'candidate_revision': cases.SHA, 'version_label': 'v0.18.0', 'build_testing': False,
                        'groups': ['G-018-cede-self-async-trait']
                    })),
                    ('corrupt_receipt_type', lambda r: r['candidate_018']['receipts'].__setitem__(0, 'bad')),
                    ('receipt_command_not_ok', lambda r: r['candidate_018']['receipts'][0].update(ok=False)),
                    ('missing_receipts', lambda r: r['candidate_018'].update(receipts=[])),
                    ('garbage_receipt', lambda r: r['candidate_018'].update(receipts=[{'garbage': True}])),
                    ('bad_tki_sha_mismatch', lambda r: r['candidate_018']['source_hidden_tki'].update(consumed_tki_sha256='0'*64)),
                    ('p1_2a_replay_bool_exit', lambda r: r['candidate_018']['source_hidden_tki']['missing_interface_command'].update(exit_code=True)),
                    ('p1_2b_replay_neg_bin_true', lambda r: [x.update(argv=['/bin/true']) for x in r['candidate_018']['receipts'] if x['name'].startswith('neg-')][:1]),
                    ('p1_2c_replay_pos_bin_true', lambda r: [x.update(argv=['/bin/true']) for x in r['candidate_018']['receipts'] if x['name'].startswith('pos-')][:1]),
                    ('p1_2d_replay_version_echo', lambda r: [x.update(argv=['/bin/echo', '0.18.0']) for x in r['candidate_018']['receipts'] if x['name'] == 'version-toka'][:1]),
                ]:
                    with self.subTest(replay_mutation=p_desc):
                        d_bad = copy.deepcopy(d)
                        p_mutate(d_bad['replay']['receipts'][0])
                        cases.dump(a.replay_receipt, d_bad['replay'])
                        self.assertTrue(promotion.validate(a))
                # Restore valid replay receipt
                cases.dump(a.replay_receipt, d['replay'])
                self.assertEqual(promotion.validate(a), [])

                # Summary mutations rejecting draft creation and promotion
                for s_desc, s_mutate in [
                    ('summary_missing_c018', lambda s: s['reports'][0].pop('candidate_018', None)),
                    ('summary_c018_fail', lambda s: s['reports'][0]['candidate_018'].update(result='fail')),
                    ('summary_tki_mismatch', lambda s: s['reports'][0]['candidate_018']['source_hidden_tki'].update(consumed_tki_sha256='0'*64)),
                    ('summary_p1_2a_bool_exit', lambda s: s['reports'][0]['candidate_018']['source_hidden_tki']['missing_interface_command'].update(exit_code=True)),
                    ('summary_p1_2b_neg_bin_true', lambda s: [x.update(argv=['/bin/true']) for x in s['reports'][0]['candidate_018']['receipts'] if x['name'].startswith('neg-')][:1]),
                    ('summary_p1_2c_pos_bin_true', lambda s: [x.update(argv=['/bin/true']) for x in s['reports'][0]['candidate_018']['receipts'] if x['name'].startswith('pos-')][:1]),
                    ('summary_p1_2d_version_echo', lambda s: [x.update(argv=['/bin/echo', '0.18.0']) for x in s['reports'][0]['candidate_018']['receipts'] if x['name'] == 'version-toka'][:1]),
                ]:
                    with self.subTest(summary_draft_mutation=s_desc):
                        d_bad = copy.deepcopy(d)
                        s_mutate(d_bad['summary'])
                        cases.dump(a.qualification_summary, d_bad['summary'])
                        with self.assertRaises(ValueError):
                            draft.validate(a)
                        self.assertTrue(promotion.validate(a))
                # Restore valid qualification summary
                cases.dump(a.qualification_summary, d['summary'])

                for tag in ('v0.18.01', 'v0.18.0-rc.1', 'v0.19.0'):
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

    def test_parse_counts_preserves_candidate_018(self):
        import release_gate
        fake_payload = {
            "schema": "toka.release-package-smoke",
            "count": 22,
            "candidate_018": {"sample": "data_018"}
        }
        stdout = f"leading output\n{json.dumps(fake_payload)}\n"
        counts = release_gate.parse_counts("package_smoke", stdout)
        self.assertEqual(counts.get("checks"), 22)
        self.assertEqual(counts.get("candidate_018"), {"sample": "data_018"})


if __name__ == '__main__':
    unittest.main()
