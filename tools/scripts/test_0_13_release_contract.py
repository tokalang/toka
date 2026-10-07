import copy,json,tempfile,unittest
from pathlib import Path
import test_release_platform_policy as cases
import release_platform_policy as policy
import verify_qualified_draft as draft
import verify_release_promotion as promotion

class Contract(unittest.TestCase):
    def test_versions_and_platforms(self):
        self.assertEqual(policy.core_targets('v0.11.0'),policy.LEGACY)
        self.assertEqual(policy.core_targets('v0.12.0'),policy.CORE)
        self.assertEqual(policy.core_targets('v0.13.0'),policy.CORE)
        for tag in ('v0.13.00','v0.13.01','v0.13.0-rc.1','v0.14.0','v1.0.0'):
            self.assertIsNone(promotion.TAG.fullmatch(tag))
        self.assertEqual(policy.policy_id('v0.12.0'),'toka.release-platforms.0.12.v1')
        self.assertEqual(policy.policy_id('v0.13.0'),'toka.release-platforms.0.13.v1')
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
