import copy
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


class Contract014(unittest.TestCase):
    def test_versions_and_platforms(self):
        self.assertEqual(policy.core_targets('v0.11.0'), policy.LEGACY)
        self.assertEqual(policy.core_targets('v0.12.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.0'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.1'), policy.CORE)
        self.assertEqual(policy.core_targets('v0.14.0'), policy.CORE)
        for tag in ('v0.14.00', 'v0.14.01', 'v0.14.0-rc.1', 'v0.15.0', 'v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'), 'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.13.1'), 'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.14.0'), 'toka.release-platforms.0.14.v1')

    def test_installed_protocol_014_across_release_chain(self):
        old = cases.TAG
        cases.TAG = 'v0.14.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                proof = {
                    'schema': 'toka.0.14-candidate-controls',
                    'version': 1,
                    'result': 'pass',
                    'candidate_revision': cases.SHA,
                    'version_label': 'v0.14.0',
                    'build_testing': False,
                    'groups': ['G1', 'G2', 'G3-unwrap'],
                }
                gate = report('linux-x64', cases.SHA, 'v0.14.0')
                gate['stages'][-1]['counts']['candidate_014'] = copy.deepcopy(proof)
                self.assertEqual(qualification.report_errors(gate, cases.SHA, 'v0.14.0'), [])
                self.assertEqual(policy.summary_errors(d['summary'], cases.SHA, 'v0.14.0'), [])
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                # Negative controls on candidate_014 properties
                mutations = [
                    ('missing_version', lambda p: p.pop('version')),
                    ('none_version', lambda p: p.update(version=None)),
                    ('bool_version', lambda p: p.update(version=True)),
                    ('bad_version', lambda p: p.update(version=2)),
                    ('bad_schema', lambda p: p.update(schema='toka.wrong-schema')),
                    ('bad_result', lambda p: p.update(result='fail')),
                    ('dirty_build_testing', lambda p: p.update(build_testing=True)),
                    ('wrong_revision', lambda p: p.update(candidate_revision='0' * 40)),
                    ('wrong_label', lambda p: p.update(version_label='v0.13.0')),
                    ('missing_groups', lambda p: p.update(groups=['G1', 'G2'])),
                    ('extra_groups', lambda p: p.update(groups=['G1', 'G2', 'G3-unwrap', 'G4'])),
                ]
                for desc, mutate in mutations:
                    with self.subTest(mutation=desc):
                        bad = copy.deepcopy(proof)
                        mutate(bad)
                        gate_bad = copy.deepcopy(gate)
                        gate_bad['stages'][-1]['counts']['candidate_014'] = bad
                        self.assertTrue(qualification.report_errors(gate_bad, cases.SHA, 'v0.14.0'))

                        summary_bad = copy.deepcopy(d['summary'])
                        summary_bad['reports'][0]['candidate_014'] = bad
                        self.assertTrue(policy.summary_errors(summary_bad, cases.SHA, 'v0.14.0'))

                # Verify historical protocols (v0.11.0 and v0.12.0) are not retroactively burdened
                for hist_tag in ('v0.11.0', 'v0.12.0'):
                    self.assertEqual(qualification.report_errors(report('linux-x64', cases.SHA, hist_tag), cases.SHA, hist_tag), [])
        finally:
            cases.TAG = old

    def test_draft_promotion_and_negatives_014(self):
        old = cases.TAG
        cases.TAG = 'v0.14.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a, d = cases.fixture(Path(name))
                self.assertEqual(len(draft.validate(a)), 3)
                self.assertEqual(promotion.validate(a), [])

                for tag in ('v0.14.01', 'v0.14.0-rc.1', 'v0.15.0'):
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
