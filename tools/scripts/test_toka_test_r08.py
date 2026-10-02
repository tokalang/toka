#!/usr/bin/env python3
"""Real installed CLI linker-start checks, retaining observations on frozen SDKs."""
import argparse,json,os,shutil,sys
from pathlib import Path
from test_toka_test_i2c_batch import Batch,sha

def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--observe',action='store_true');a=p.parse_args()
 b=Batch(a.sdk.resolve(),a.output.resolve());ok='fn main() -> i32 { return 0 }\n';observations=[]
 for variant in ('missing','permission','interpreter'):
  root=b.project('linker-'+variant,[('a_test.tk',ok),('b_test.tk',ok)])
  search=b.output/('path-'+variant);search.mkdir();(search/'python3').symlink_to(Path(sys.executable).resolve())
  if variant!='missing':
   cc=search/'cc';cc.write_text('#!/nonexistent-toka-r08-interpreter\n');cc.chmod(0o600 if variant=='permission' else 0o755)
  env=dict(b.env,PATH=str(search))
  import subprocess
  command=[str(b.sdk/'bin/toka'),'test','--json'];r=subprocess.run(command,cwd=root,env=env,capture_output=True,timeout=45)
  report,end=json.JSONDecoder().raw_decode(r.stdout.decode());assert not r.stdout.decode()[end:].strip()
  b.save('R08-linker-'+variant,root,command,r.returncode,r.stdout,r.stderr,report,rows=['R08'])
  statuses=[t['result'] for t in report['tests']]
  valid=r.returncode==2 and report['result']=='infrastructure_error' and statuses==['infrastructure_error','not_run'] and report['tests'][0]['phases']['run']['state']=='not_started' and report['tests'][1]['phases']['compile_link']['state']=='not_started'
  if not a.observe:
   driver=report['tests'][0]['link_driver'];assert driver['state']=='launch_failed' and driver['pid'] is None
   assert driver['os_error']==(13 if variant=='permission' else 2)
   assert report['errors'][0]['os_error']==driver['os_error']
   assert report['tests'][0]['phases']['compile_link']['exit_code']==1
  observations.append({'case':variant,'exit_code':r.returncode,'result':report['result'],'tests':statuses,'contract_pass':valid,'PATH':str(search)})
 root=b.project('started-link-error',[('a_test.tk','extern fn absent_link_symbol_i2c() -> i32\nfn main() -> i32 { return absent_link_symbol_i2c() }\n'),('b_test.tk',ok)])
 r=b.cli('R03',root,expected=1,rows=['R03']);assert [t['result'] for t in r['tests']]==['compile_failed','passed']
 assert r['tests'][0]['phases']['run']['state']=='not_started'
 if not a.observe:
  driver=r['tests'][0]['link_driver'];assert driver['state']=='completed' and driver['exit_code']!=0 and driver['os_error'] is None
  assert r['tests'][1]['link_driver']['exit_code']==0
 data={'schema':'toka.r08-acceptance','observations':observations,'contract_pass':all(o['contract_pass'] for o in observations),'R03_control':'compile_failed/1 then passed','compiler_sha256':sha(b.sdk/'bin/tokac'),'imports':'stdlib plus standalone harness and installed SDK helpers'}
 (b.output/'result.json').write_text(json.dumps(data,indent=2)+'\n');print(json.dumps(data))
 if not a.observe and not data['contract_pass']:return 1
 return 0
if __name__=='__main__':raise SystemExit(main())
