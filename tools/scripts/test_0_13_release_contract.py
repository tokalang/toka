import copy,json,tempfile,unittest
from pathlib import Path
import test_release_platform_policy as cases
import release_platform_policy as policy
import verify_qualified_draft as draft
import verify_release_promotion as promotion
import verify_release_qualification as qualification
from test_release_workflow import report

class Contract(unittest.TestCase):
    def test_versions_and_platforms(self):
        self.assertEqual(policy.core_targets('v0.11.0'),policy.LEGACY)
        self.assertEqual(policy.core_targets('v0.12.0'),policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.0'),policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.1'),policy.CORE)
        for tag in ('v0.13.00','v0.13.01','v0.13.0-rc.1','v0.14.0','v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'),'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'),'toka.release-platforms.0.13.v1')
        self.assertEqual(policy.policy_id('v0.13.1'),'toka.release-platforms.0.13.v1')
    def test_installed_protocol_version_across_release_chain(self):
        old=cases.TAG;cases.TAG='v0.13.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a,d=cases.fixture(Path(name))
                proof={'schema':'toka.0.13-candidate-controls','version':1,
                       'result':'pass','candidate_revision':cases.SHA,
                       'version_label':'v0.13.0','build_testing':False,
                       'groups':['A1','B1','B1-boundaries','B1-relative','D1-D2']}
                gate=report('linux-x64',cases.SHA,'v0.13.0')
                gate['stages'][-1]['counts']['candidate_013']=copy.deepcopy(proof)
                self.assertEqual(qualification.report_errors(gate,cases.SHA,'v0.13.0'),[])
                self.assertEqual(policy.summary_errors(d['summary'],cases.SHA,'v0.13.0'),[])
                self.assertEqual(len(draft.validate(a)),3)
                self.assertEqual(promotion.validate(a),[])
                for value in ('missing',None,True,False,999,1.0,'1'):
                    with self.subTest(version=value):
                        bad=copy.deepcopy(proof)
                        if value=='missing':bad.pop('version')
                        else:bad['version']=value
                        gate['stages'][-1]['counts']['candidate_013']=bad
                        self.assertTrue(qualification.report_errors(gate,cases.SHA,'v0.13.0'))
                        summary=copy.deepcopy(d['summary'])
                        summary['reports'][0]['candidate_013']=bad
                        self.assertTrue(policy.summary_errors(summary,cases.SHA,'v0.13.0'))
                        cases.dump(a.qualification_summary,summary)
                        with self.assertRaises(ValueError):draft.validate(a)
                        self.assertTrue(promotion.validate(a))
                # The installed 0.13 protocol is not retroactively required of old reports.
                for tag in ('v0.11.0','v0.12.0'):
                    self.assertEqual(qualification.report_errors(report('linux-x64',cases.SHA,tag),cases.SHA,tag),[])
                cases.TAG='v0.12.0'
                with tempfile.TemporaryDirectory() as legacy_name:
                    legacy,documents=cases.fixture(Path(legacy_name))
                    self.assertEqual(policy.summary_errors(documents['summary'],cases.SHA,cases.TAG),[])
                    self.assertEqual(len(draft.validate(legacy)),3)
                    self.assertEqual(promotion.validate(legacy),[])
        finally:cases.TAG=old

    def test_draft_promotion_and_negatives(self):
        old=cases.TAG;cases.TAG='v0.13.0'
        try:
            with tempfile.TemporaryDirectory() as name:
                a,d=cases.fixture(Path(name))
                for stage in d['summary']['reports']:
                    if 'stages' in stage:pass
                self.assertEqual(len(draft.validate(a)),3);self.assertEqual(promotion.validate(a),[])
                for tag in ('v0.13.01','v0.13.0-rc.1','v0.14.0'):
                    b=copy.copy(a);b.tag_name=tag
                    with self.assertRaises(ValueError):draft.validate(b)
                b=copy.copy(a);b.candidate_sha='b'*40
                with self.assertRaises(ValueError):draft.validate(b)
                item=next(a.qualified_archives_dir.glob('*/*.tar.gz'));item.unlink()
                with self.assertRaises(ValueError):draft.validate(a)
        finally:cases.TAG=old

    def test_0_13_1_r3_r5_contract_and_negatives(self):
        old=cases.TAG;cases.TAG='v0.13.1'
        try:
            with tempfile.TemporaryDirectory() as name:
                a,d=cases.fixture(Path(name))
                proof={'schema':'toka.0.13-candidate-controls','version':1,
                       'result':'pass','candidate_revision':cases.SHA,
                       'version_label':'v0.13.1','build_testing':False,
                       'groups':['A1','B1','B1-boundaries','B1-relative','D1-D2']}
                r3_r5=cases.valid_r3_r5(cases.SHA,'v0.13.1','linux-x64')
                gate=report('linux-x64',cases.SHA,'v0.13.1')
                gate['stages'][-1]['counts']['candidate_013']=copy.deepcopy(proof)
                gate['stages'][-1]['counts']['r3_r5']=copy.deepcopy(r3_r5)
                self.assertEqual(qualification.report_errors(gate,cases.SHA,'v0.13.1'),[])
                self.assertEqual(policy.summary_errors(d['summary'],cases.SHA,'v0.13.1'),[])
                self.assertEqual(len(draft.validate(a)),3)
                self.assertEqual(promotion.validate(a),[])

                # Header-only, missing fields, or bad top-level values
                for bad_r3_r5 in (None,{},
                                  {'schema':'toka.r3-r5-installed-controls','version':1,'result':'pass','candidate_revision':cases.SHA,'version_label':'v0.13.1','target':'linux-x64'}, # header-only
                                  dict(r3_r5, version=2),
                                  dict(r3_r5, version=True),
                                  dict(r3_r5, result='fail'),
                                  dict(r3_r5, candidate_revision='b'*40),
                                  dict(r3_r5, version_label='v0.13.0'),
                                  dict(r3_r5, target='wrong-target'),
                                  dict(r3_r5, fixtures_digest='0'*64),
                                  dict(r3_r5, control_script_name='wrong.py'),
                                  dict(r3_r5, control_script_sha256='0'*64)):
                    bad_gate=copy.deepcopy(gate)
                    if bad_r3_r5 is None:bad_gate['stages'][-1]['counts'].pop('r3_r5',None)
                    else:bad_gate['stages'][-1]['counts']['r3_r5']=bad_r3_r5
                    self.assertTrue(qualification.report_errors(bad_gate,cases.SHA,'v0.13.1'))

                # Counts mutations
                for bad_counts in ({},
                                   dict(r3_r5['counts'], failed=22, passed=0),
                                   dict(r3_r5['counts'], total_checks=21),
                                   dict(r3_r5['counts'], passed=True)):
                    bad_gate=copy.deepcopy(gate)
                    bad_gate['stages'][-1]['counts']['r3_r5']=dict(r3_r5, counts=bad_counts)
                    self.assertTrue(qualification.report_errors(bad_gate,cases.SHA,'v0.13.1'))

                # Checks mutations (missing checks, bad exit code, bad diagnostic, missing artifact_absent)
                bad_checks_list = [
                    [], # empty checks
                    r3_r5['checks'][:-1], # missing one check
                    [dict(r3_r5['checks'][0], result='fail')] + r3_r5['checks'][1:], # failed check
                    [dict(r3_r5['checks'][0], diagnostic='E9999')] + r3_r5['checks'][1:], # wrong diagnostic
                    [dict(r3_r5['checks'][1], artifact_absent=False)] + r3_r5['checks'][2:], # artifact not absent
                    r3_r5['checks'][:-1] + [dict(r3_r5['checks'][-1], exit_code=1)], # positive check non-zero
                    r3_r5['checks'][:-1] + [dict(r3_r5['checks'][-1], exit_code=False)], # positive check bool
                ]
                for bad_checks in bad_checks_list:
                    bad_gate=copy.deepcopy(gate)
                    bad_gate['stages'][-1]['counts']['r3_r5']=dict(r3_r5, checks=bad_checks)
                    self.assertTrue(qualification.report_errors(bad_gate,cases.SHA,'v0.13.1'))

                # Receipts mutations (ordinary error 2, crashed signal, missing signal, termination/error, false argv, wrong fixture, missing flags)
                r0 = copy.deepcopy(r3_r5['receipts'][0])
                r0_no_sig = copy.deepcopy(r0); r0_no_sig.pop('signal')
                r0_term = copy.deepcopy(r0); r0_term['termination'] = 'timeout'
                r0_err = copy.deepcopy(r0); r0_err['error'] = 'some error'
                r0_false = copy.deepcopy(r0); r0_false['argv'] = ['/bin/false']
                r0_wrong_fix = copy.deepcopy(r0); r0_wrong_fix['argv'] = ['/sdk/bin/tokac', '--check-only', '/fixtures/other.tk']
                r0_no_flag = copy.deepcopy(r0); r0_no_flag['argv'] = ['/sdk/bin/tokac', '/fixtures/r3_task_direct_escape.tk']
                r_pos_false = copy.deepcopy(r3_r5['receipts'][-1]); r_pos_false['argv'] = ['/bin/false']
                bad_receipts_list = [
                    [], # empty receipts
                    r3_r5['receipts'][:-1], # missing one receipt
                    [dict(r3_r5['receipts'][0], exit_code=2)] + r3_r5['receipts'][1:], # negative exit code 2
                    [dict(r3_r5['receipts'][0], exit_code=0)] + r3_r5['receipts'][1:], # negative exit code 0
                    [dict(r3_r5['receipts'][0], signal=9)] + r3_r5['receipts'][1:], # abnormal signal
                    [r0_no_sig] + r3_r5['receipts'][1:], # missing signal key
                    [r0_term] + r3_r5['receipts'][1:], # termination=timeout
                    [r0_err] + r3_r5['receipts'][1:], # error=...
                    [r0_false] + r3_r5['receipts'][1:], # argv=[/bin/false]
                    [r0_wrong_fix] + r3_r5['receipts'][1:], # wrong fixture in argv
                    [r0_no_flag] + r3_r5['receipts'][1:], # missing mode flag
                    r3_r5['receipts'][:-1] + [r_pos_false], # pos-run with /bin/false
                    r3_r5['receipts'][:-1] + [dict(r3_r5['receipts'][-1], exit_code=1)], # positive exit code 1
                    r3_r5['receipts'][:-1] + [dict(r3_r5['receipts'][-1], exit_code=False)], # positive exit code bool
                ]
                for bad_receipts in bad_receipts_list:
                    bad_gate=copy.deepcopy(gate)
                    bad_gate['stages'][-1]['counts']['r3_r5']=dict(r3_r5, receipts=bad_receipts)
                    self.assertTrue(qualification.report_errors(bad_gate,cases.SHA,'v0.13.1'))

                bad_summary=copy.deepcopy(d['summary'])
                bad_summary['reports'][0].pop('r3_r5',None)
                self.assertTrue(policy.summary_errors(bad_summary,cases.SHA,'v0.13.1'))

                # Mutation propagating through summary and replay
                bad_summary2=copy.deepcopy(d['summary'])
                bad_summary2['reports'][0]['r3_r5']=dict(r3_r5, fixtures_digest='0'*64)
                self.assertTrue(policy.summary_errors(bad_summary2,cases.SHA,'v0.13.1'))

                bad_a=copy.copy(a)
                bad_replay=copy.deepcopy(d['replay'])
                bad_replay['receipts'][0].pop('r3_r5',None)
                cases.dump(a.replay_receipt,bad_replay)
                self.assertTrue(promotion.validate(bad_a))

                bad_replay2=copy.deepcopy(d['replay'])
                bad_replay2['receipts'][0]['r3_r5']=dict(r3_r5, receipts=[dict(r3_r5['receipts'][0], exit_code=2)] + r3_r5['receipts'][1:])
                cases.dump(a.replay_receipt,bad_replay2)
                self.assertTrue(promotion.validate(bad_a))
        finally:cases.TAG=old

if __name__=='__main__':unittest.main()
