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
                ]

                for name_mut, mut in mutations:
                    bad = copy.deepcopy(proof)
                    mut(bad)
                    errs = policy.candidate_018_receipt_errors(bad, cases.SHA, 'v0.18.0', target='linux-x64')
                    self.assertTrue(errs, f"Mutation {name_mut} failed to produce error")
        finally:
            cases.TAG = old


if __name__ == '__main__':
    unittest.main()
