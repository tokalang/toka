#!/usr/bin/env python3
"""Exercise the real replay shell guards before tags and with annotated tags."""
import os,subprocess,tempfile,textwrap,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TEXT=(ROOT/'.github/workflows/qualified_artifact_replay.yml').read_text()
def guard(job):
 block=TEXT.split('  '+job+':\n',1)[1]
 if job=='policy-plan':
  block=block.split('  policy-replay:',1)[0];body=block.split('          set -euo pipefail\n',1)[1].split('          extra=()',1)[0]
 else:
  block=block.split('  policy-plan:',1)[0];body=block.split('          set -euo pipefail\n',1)[1].split('          run_api=',1)[0]
 return 'set -euo pipefail\n'+textwrap.dedent(body)
class Binding(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
  self.git('init','-q');(self.root/'input').write_text('fixed source')
  self.git('add','input');self.git('-c','user.name=control','-c','user.email=control@example.invalid','commit','-qm','fixed')
  self.sha=self.git('rev-parse','HEAD').stdout.strip()
 def tearDown(self):self.tmp.cleanup()
 def git(self,*args):return subprocess.run(['git','-c','core.fsmonitor=false',*args],cwd=self.root,text=True,capture_output=True,check=True)
 def check(self,job='policy-plan',success=True,**override):
  env=dict(os.environ,TAG_NAME='v0.12.0',CANDIDATE_SHA=self.sha,CANDIDATE_ONLY='true',ASSET_SOURCE='candidate_run',QUALIFICATION_RUN_ID='123',ARCHIVE_SHA256='a'*64);env.update(override)
  row=subprocess.run(['bash','-c',guard(job)],cwd=self.root,env=env,text=True,capture_output=True);self.assertEqual(row.returncode==0,success,row.stderr)
 def annotate(self,label):self.git('-c','user.name=control','-c','user.email=control@example.invalid','tag','-a',label,'-m','fixed source')
 def test_candidate_bytes_before_tag(self):self.check()
 def test_default_requires_tag(self):self.check(success=False,CANDIDATE_ONLY='false')
 def test_annotated_tag_remains_required(self):
  self.annotate('v0.12.0');self.check(CANDIDATE_ONLY='false');self.git('tag','-d','v0.12.0');self.git('tag','v0.12.0');self.check(success=False,CANDIDATE_ONLY='false')
 def test_candidate_mode_rejects_tagged_archive_source(self):self.check(success=False,ASSET_SOURCE='qualified_run')
 def test_wrong_revision_and_label(self):
  self.check(success=False,CANDIDATE_SHA='b'*40);self.check(success=False,TAG_NAME='v0.12.01');self.check(success=False,TAG_NAME='v0.11.0')
 def test_legacy_never_uses_candidate_only(self):
  self.annotate('v0.11.0');self.check(job='replay',success=False,TAG_NAME='v0.11.0');self.check(job='replay',TAG_NAME='v0.11.0',CANDIDATE_ONLY='false')
if __name__=='__main__':unittest.main()
