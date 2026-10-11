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
                proof = cases.valid_candidate_014(cases.SHA, 'v0.14.0', 'linux-x64')
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
                    ('missing_receipts_key', lambda p: p.pop('receipts')),
                    ('empty_receipts', lambda p: p.update(receipts=[])),
                    ('wrong_receipt_type', lambda p: p['receipts'].__setitem__(0, "invalid")),
                    ('all_commands_failed', lambda p: [r.update(exit_code=1) for r in p['receipts'] if r.get('name', '').startswith(('version-', 'g1-', 'g2-', 'g3-compile-', 'g3-run-'))]),
                    ('counts_mismatch', lambda p: p['counts'].update(total_receipts=73)),
                    ('counts_failed_nonzero', lambda p: p['counts'].update(failed=1)),
                    ('missing_tools', lambda p: p.pop('four_tool_versions')),
                    ('wrong_tool_version', lambda p: p['four_tool_versions'].update(tokac='tokac 0.13.1')),
                    ('wrong_fixtures_digest', lambda p: p.update(fixtures_digest='0' * 64)),
                    ('wrong_control_sha', lambda p: p.update(control_script_sha256='0' * 64)),
                    ('noop_programs', lambda p: [r['argv'].__setitem__(0, '/bin/true' if r['exit_code'] == 0 else '/bin/false') for r in p['receipts']]),
                    ('wrong_fixture', lambda p: [r['argv'].__setitem__(i, '/fixtures/unrelated.tk') for r in p['receipts'] for i, a in enumerate(r['argv']) if a.endswith('.tk')]),
                    ('wrong_run_binary', lambda p: [r['argv'].__setitem__(0, '/bin/true') for r in p['receipts'] if '-run' in r.get('name', '')]),
                    ('contradictory_domain_and_artifacts', lambda p: [r.update(result='failed', diagnostic='E00000', artifact_absent=False, stdout='failure instead of assertions', stderr='ordinary error') for r in p['receipts']]),
                    ('missing_target', lambda p: p.pop('target', None)),
                    ('bad_group_counts', lambda p: p['counts'].update(groups={'G1': 0, 'G2': 0, 'G3-unwrap': 0, 'version': 0})),
                    ('nonfinite_timing', lambda p: [r.update(execution_ms=float('nan')) for r in p['receipts']]),
                    ('missing_raw_outputs', lambda p: [(r.pop('stdout', None), r.pop('stderr', None)) for r in p['receipts']]),
                    ('missing_negative_stderr', lambda p: [r.pop('stderr', None) for r in p['receipts'] if r['exit_code'] == 1]),
                    ('invalid_raw_output_types', lambda p: [r.update(stdout=None, stderr=[]) for r in p['receipts']]),
                    ('wrong_run_name_fragment', lambda p: [r.update(argv=['/not-produced/not-' + r['name'].split('-run-')[-1].replace('g1-lifecycle-run', 'g1_lifecycle') + '-helper']) for r in p['receipts'] if r['name'].startswith(('g2-run-', 'g3-run-'))]),
                    ('contradictory_zero_output', lambda p: [r.update(stdout='ok=true, wire=3, calls=1, frames=1, state=0, closed=false\n') for r in p['receipts'] if r['name'] == 'g2-run-framed_stream_zero']),
                    ('empty_protocol_outputs', lambda p: [r.update(stdout='') for r in p['receipts'] if r['name'].startswith('g2-run-') and r['name'].split('g2-run-')[1] not in ('lifecycle_pass_suite', 'given_11_branch', 'closed_empty')]),
                    ('compile_run_path_mismatch', lambda p: [r['argv'].__setitem__(r['argv'].index('-o') + 1, '/not-produced/unrelated.bin') for r in p['receipts'] if r['name'] in ('g1-lifecycle-compile', 'g2-compile-closed_empty', 'g3-compile-aggregate')]),
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

                # Promotion rejection when replay receipts candidate_014 is mutated or missing
                for p_desc, p_mutate in [
                    ('missing_candidate_014', lambda r: r.pop('candidate_014', None)),
                    ('none_candidate_014', lambda r: r.update(candidate_014=None)),
                    ('header_only_candidate_014', lambda r: r.update(candidate_014={
                        'schema': 'toka.0.14-candidate-controls', 'version': 1, 'result': 'pass',
                        'candidate_revision': cases.SHA, 'version_label': 'v0.14.0', 'build_testing': False,
                        'groups': ['G1', 'G2', 'G3-unwrap']
                    })),
                    ('corrupt_receipt_type', lambda r: r['candidate_014']['receipts'].__setitem__(0, 'bad')),
                    ('receipt_command_failed', lambda r: r['candidate_014']['receipts'][0].update(exit_code=1)),
                ]:
                    with self.subTest(replay_mutation=p_desc):
                        d_bad = copy.deepcopy(d)
                        p_mutate(d_bad['replay']['receipts'][0])
                        cases.dump(a.replay_receipt, d_bad['replay'])
                        self.assertTrue(promotion.validate(a))
                # Restore valid replay receipt
                cases.dump(a.replay_receipt, d['replay'])
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
