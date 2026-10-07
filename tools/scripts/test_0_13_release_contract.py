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
        for tag in ('v0.13.00','v0.13.01','v0.13.0-rc.1','v0.14.0','v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'),'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'),'toka.release-platforms.0.13.v1')
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

if __name__=='__main__':unittest.main()
