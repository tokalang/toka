#!/usr/bin/env python3
"""Frozen SDK T09/T10/T11/T13: real CLI boundaries and explicit observation seams."""
import argparse,contextlib,io,json,os,signal,subprocess,sys,time
from pathlib import Path
from unittest.mock import patch
from test_toka_test_i2c_batch import Batch,manifest,source,sha,check
SDK_SHA='8341ab9bedaf8d10703cf9bfb89911bd6c648e0a'
OK='fn main()->i32 { return 0 }\n'
NATIVE=r'''#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
static int cleanup_fd=-1;
static void term_seen(int sig){char c='T';write(cleanup_fd,&c,1);}
int observation_fixture(int mode){
 char release[4096],marker[4096];const char *dir=getenv("TOKA_TEST_CASE_DIR");
 snprintf(release,sizeof(release),"%s/exit-release",dir);
 snprintf(marker,sizeof(marker),"%s/cleanup-ready",dir);
 if(mode==1){cleanup_fd=open(marker,O_WRONLY|O_CREAT|O_TRUNC,0600);if(cleanup_fd<0)return 91;signal(SIGTERM,term_seen);}
 if(mode==2){
  int channel[2];if(pipe(channel))return 92;pid_t child=fork();if(child<0)return 93;
  if(!child){close(channel[0]);if(setsid()<0)_exit(94);int fd=open("/dev/null",O_RDWR);if(fd<0)_exit(95);for(int i=0;i<3;i++)if(dup2(fd,i)<0)_exit(96);if(fd>2)close(fd);
   char c='R';if(write(channel[1],&c,1)!=1)_exit(97);close(channel[1]);while(access(release,F_OK))usleep(10000);_exit(0);
  }
  close(channel[1]);char c;if(read(channel[0],&c,1)!=1 || c!='R')return 98;close(channel[0]);
  snprintf(marker,sizeof(marker),"%s/escape.json",dir);FILE *f=fopen(marker,"w");if(!f)return 99;
  fprintf(f,"{\"leader_pid\":%d,\"child_pid\":%d,\"leader_pgid\":%d,\"child_pgid\":%d,\"setsid_ready\":true,\"stdio\":\"devnull\"}\n",getpid(),child,getpgrp(),getpgid(child));fclose(f);return 0;
 }
 puts("READY_OBSERVATION");fflush(stdout);
 if(mode==1){for(;;)pause();}
 while(access(release,F_OK))usleep(10000);return 0;
}
'''
class Observation(Batch):
 def native_project(self,name,mode):
  root=self.project(name,[('a_test.tk','import official/fixture::{control}\nfn main()->i32 { return control('+str(mode)+') }\n'),('b_test.tk',OK)])
  manifest(root,'fixture="../native-fixture",')
  r=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=self.env,capture_output=True,timeout=30);assert r.returncode==0,r.stderr
  return root
 def log_ready(self,root):return next((p for p in (root/'.toka/test-runs').glob('*/000001/run.stdout') if b'READY_OBSERVATION' in p.read_bytes()),None)
 def await_ready(self,root,child=None):
  until=time.monotonic()+10
  while time.monotonic()<until:
   log=self.log_ready(root)
   if log:return log
   if child and child.poll() is not None:raise AssertionError('runner ended before readiness')
   time.sleep(.01)
  raise AssertionError('readiness deadline exceeded')
 def record(self,name,root,code,out,err,report,rows,checks,extra=None,layer='installed CLI'):
  r=self.save(name,root,[str(self.sdk/'bin/toka'),'test','--json'],code,out,err,report,rows=rows,layer=layer)
  count=r['summary'];checks.update(exit_matches=code==r['exit_code'],finalized=r['finalized'],summary_consistent=count['total'] is None or count['total']==len(r['tests'])==sum(v for k,v in count.items() if k!='total'))
  path=self.output/name/'result.json';data=json.loads(path.read_text());data.update(checks=checks,contract_pass=all(checks.values()),**(extra or {}));path.write_text(json.dumps(data,indent=2)+'\n');assert data['contract_pass'],checks
 def phase_confirmed(self,r):
  c=r['tests'][0]['cleanup'];return c['status']=='confirmed' and all(c[k] for k in ('leader_reaped','group_absent','output_complete'))
 def invoke(self,root,args,env=None):
  command=[str(self.sdk/'bin/toka'),'test','--json',*args];r=subprocess.run(command,cwd=root,env=env or self.env,capture_output=True,timeout=60);return r.returncode,r.stdout,r.stderr,json.loads(r.stdout)
 def warm_launcher(self):
  folder=self.output/'warm-python';folder.mkdir()
  code='#!'+sys.executable+'\n'+"""import json,pathlib,sys
sys.path.insert(0,SDK_HELPER)
import toka_test as runner
original=runner.Supervisor.run
class Warm(runner.Supervisor):
 def exited(self,pid):
  ended=super().exited(pid)
  if (self.directory/'warmup.stdout').exists() and b'READY_OBSERVATION' in (self.directory/'warmup.stdout').read_bytes():(self.directory/'exit-release').touch()
  return ended
def prewarm(self,*args,**kwargs):
 if args[3]=='run':
  warm=Warm();warm.directory=pathlib.Path(args[2])
  raw=original(warm,args[0],args[1],args[2],'warmup',args[4],10000)
  (warm.directory/'warmup.json').write_text(json.dumps(raw,indent=2)+'\\n')
  assert raw['exit_code']==0 and raw['cleanup']['status']=='confirmed',raw
  (warm.directory/'exit-release').unlink(missing_ok=True)
 return original(self,*args,**kwargs)
runner.Supervisor.run=prewarm
sys.argv=sys.argv[1:]
sys.exit(runner.main())
""".replace('SDK_HELPER',repr(str(self.sdk/'lib/toolchain')))
  path=folder/'python3';path.write_text(code);path.chmod(0o755)
  return dict(self.env,PATH=str(folder)+os.pathsep+self.env['PATH']),{'layer':'explicit cache prewarm through installed SDK supervisor; SDK and compiler unchanged','script_sha256':sha(path),'warmup_budget_ms':10000,'measured_run_budget_ms':1000}
 def run(self):
  descriptor=json.loads((self.sdk/'preview-sdk.json').read_text());assert descriptor['candidate_sha']==SDK_SHA and len(descriptor['components'])==146
  for name,value in descriptor['components'].items():assert sha(self.sdk/name)==value['sha256']
  dep=self.work/'native-fixture';manifest(dep);(dep/'package.tk').write_text('pub const PACKAGE=(name="fixture",version="1.0.0",dependencies=(),native=(required=true,sources=("native/observation.c")))\n')
  source(dep,'native/observation.c',NATIVE);source(dep,'lib/official/fixture.tk','extern fn observation_fixture(mode:i32)->i32\npub fn control(mode:i32)->i32 { return observation_fixture(mode) }\n')
  for option in ('compile','run'):
   for index,value in enumerate(('0','-1','1.5','NaN','2147483648')):
    name='T10a-'+option+'-'+str(index);root=self.project(name,[('a_test.tk',OK)]);code,out,err,r=self.invoke(root,['--'+option+'-timeout-ms',value])
    self.record(name,root,code,out,err,r,['T10a'],{'configuration':code==2 and r['result']=='configuration_error','no_process':r.get('preparation',{})=={} and r['tests']==[] and r['identity']['status']=='not_checked','no_default_used':r['timeouts']['compile_ms'] is None and r['timeouts']['run_ms'] is None},extra={'argument':value,'option':option})
  root=self.native_project('runtime-budget',0);env,warm_definition=self.warm_launcher();code,out,err,r=self.invoke(root,['--run-timeout-ms','1000'],env);t=r['tests'][0]
  self.record('T10b',root,code,out,err,r,['T10b'],{'timed_out':code==1 and t['result']=='timed_out','budget':r['timeouts']['run_ms']==1000 and r['timeouts']['run_source']=='cli','ready':self.log_ready(root) is not None,'duration':1000<=t['phases']['run']['duration_ms']<10000,'confirmed':self.phase_confirmed(r),'raw_signal':t['phases']['run']['signal']==15,'next_passed':r['tests'][1]['result']=='passed'},extra={'warmup_launcher':warm_definition,'warmup_receipts':[json.loads(p.read_text()) for p in (root/'.toka/test-runs').glob('*/000*/warmup.json')]},layer='real installed CLI; explicit Python-launcher fixture prewarm before measured runtime')
  root=self.native_project('escaped',2);release=None;witness=None
  try:
   code,out,err,r=self.invoke(root,[]);phase=r['tests'][0]['phases']['run'];directory=Path(r['tests'][0]['logs']['run_stdout']).parent;release=directory/'exit-release';witness=json.loads((directory/'escape.json').read_text())
   pid=witness['child_pid'];pgid=os.getpgid(pid);os.kill(pid,0)
   owned={'child_alive_after_SDK_confirmation':True,'pgid_at_cleanup':pgid,'harness_release':str(release),'release_monotonic_ns':time.monotonic_ns()}
   assert pgid==pid==witness['child_pgid'] and pgid!=phase['process']['pgid'];release.touch();until=time.monotonic()+5
   while time.monotonic()<until:
    try:os.kill(pid,0)
    except ProcessLookupError:owned['harness_confirmed_ESRCH']=True;break
    time.sleep(.01)
   else:owned['harness_confirmed_ESRCH']=False
   self.record('T09',root,code,out,err,r,['T09'],{'pass':code==0 and all(t['result']=='passed' for t in r['tests']),'controlled_confirmed':self.phase_confirmed(r),'scope':r['supervision']['scope']=='direct_child_and_process_group','escape':witness['setsid_ready'] and witness['stdio']=='devnull' and pgid!=witness['leader_pgid'],'harness_cleanup':owned['harness_confirmed_ESRCH']},extra={'escape_witness':witness,'harness_cleanup':owned})
  finally:
   if release is not None:release.touch()
   for marker in (root/'.toka/test-runs').glob('*/000001/escape.json'):(marker.parent/'exit-release').touch()
  for name in ('T11a','T11b','T11c'):self.priority(name)
  for name,number,repeat in [('T11d',signal.SIGINT,True),('T13',signal.SIGTERM,False)]:self.interrupted_cli(name,number,repeat)
 def priority(self,name):
  root=self.project(name,[('a_test.tk',OK),('b_test.tk',OK)]) if name=='T11a' else self.native_project(name,0)
  events=[];raw=[];outer=self;runner=self.runner
  class Ordered(runner.Supervisor):
   active=None;injected=False;expired=False;released=False
   def run(s,*args,**kwargs):
    s.active=args[3];s.directory=Path(args[2]);s.started=time.monotonic();s.budget=args[5]
    r=super().run(*args,**kwargs);raw.append(json.loads(json.dumps(r)));return r
   def exited(s,pid):
    ended=super().exited(pid)
    if s.active!='run' or s.injected:return ended
    if name=='T11a':
     until=time.monotonic()+10
     while not ended and time.monotonic()<until:time.sleep(.01);ended=super().exited(pid)
     assert ended
     while (time.monotonic()-s.started)*1000<s.budget+10:time.sleep(.001)
     events.append({'observation':'confirmed_exit_and_deadline','terminal_confirmed':True,'deadline_reached':True});s.injected=True;return True
    if name=='T11b':
     if not s.expired:
      outer.await_ready(root)
      assert not super().exited(pid)
      if (time.monotonic()-s.started)*1000>=s.budget:
       s.expired=True;events.append({'observation':'deadline_without_terminal','terminal_confirmed':False,'deadline_reached':True,'fixture_ready':True})
      return False
     (s.directory/'exit-release').touch();until=time.monotonic()+10
     while not super().exited(pid) and time.monotonic()<until:time.sleep(.001)
     assert super().exited(pid);s.injected=True;events.append({'observation':'exit_after_timeout_observation','terminal_confirmed':True});return True
    outer.await_ready(root)
    while (time.monotonic()-s.started)*1000<s.budget:time.sleep(.001)
    assert not super().exited(pid);events.append({'observation':'SIGINT_and_deadline','terminal_confirmed':False,'deadline_reached':True,'signal':2});s.injected=True;os.kill(os.getpid(),signal.SIGINT);return False
  stdout,stderr=io.StringIO(),io.StringIO()
  with patch.object(runner,'Supervisor',Ordered),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):code=runner.execute_preview(['--json','--run-timeout-ms','1000'],self.sdk/'lib',self.sdk/'bin/tokac',root)
  r=json.loads(stdout.getvalue());t=r['tests'][0];phase=t['phases']['run'];expected={'T11a':(0,'passed','none',0),'T11b':(1,'timed_out','timeout',0),'T11c':(130,'interrupted','interrupt',None)}[name]
  checks={'result':code==expected[0] and t['result']==expected[1],'trigger':t['trigger']==expected[2],'raw_exit':phase['exit_code']==expected[3],'confirmed':self.phase_confirmed(r),'deadline_observed':events[0]['deadline_reached'],'next':r['tests'][1]['result']==('not_run' if name=='T11c' else 'passed')}
  if name=='T11c':checks['SIGINT']=r['termination']['signal']==2 and phase['signal']==15
  self.record(name,root,code,stdout.getvalue().encode(),stderr.getvalue().encode(),r,[name],checks,extra={'ordered_observations':events,'raw_phases':raw},layer='installed SDK helper observation seam; real processes, monotonic deadlines and SIGINT')
 def interrupted_cli(self,name,number,repeat):
  root=self.native_project(name,1 if repeat else 0);command=[str(self.sdk/'bin/toka'),'test','--json'];child=subprocess.Popen(command,cwd=root,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);sent=[]
  try:
   log=self.await_ready(root,child);os.kill(child.pid,number);sent.append({'signal':int(number),'ready':str(log),'monotonic_ns':time.monotonic_ns()})
   if repeat:
    marker=log.parent/'cleanup-ready';until=time.monotonic()+10
    while time.monotonic()<until:
     if marker.exists() and marker.read_bytes()==b'T':break
     if child.poll() is not None:raise AssertionError('runner ended before cleanup handshake')
     time.sleep(.001)
    else:raise AssertionError('no cleanup handshake')
    os.kill(child.pid,number);sent.append({'signal':int(number),'cleanup_ready':str(marker),'monotonic_ns':time.monotonic_ns()})
   out,err=child.communicate(timeout=15);r=json.loads(out);receipt=json.loads(next((root/'.toka/test-runs').glob('*/preview.json')).read_text());t=r['tests'][0];raw=receipt['tests'][0]['run']
   checks={'exit':child.returncode==130,'interrupted':t['result']=='interrupted' and t['trigger']=='interrupt','signal':r['termination']['signal']==number,'not_run':r['tests'][1]['result']=='not_run','confirmed':self.phase_confirmed(r),'raw_signal':t['phases']['run']['signal']==(9 if repeat else 15)}
   if repeat:checks.update(repeat_count=raw['interrupt_count']>=2,grace=raw['cleanup']['duration_ms']>=1900,signals=raw['requested_signals']==[15,9])
   self.record(name,root,child.returncode,out,err,r,[name],checks,extra={'actual_CLI_PID':child.pid,'signal_observations':sent,'raw_receipt':receipt})
  finally:
   if child.poll() is None:child.terminate();child.communicate(timeout=10)

def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();batch=Observation(a.sdk.resolve(),a.output.resolve());batch.run();assert len(batch.results)==17
 records=[]
 for item in batch.results:
  path=batch.output/item['name']/'result.json';r=json.loads(path.read_text());assert r['contract_pass'];records.append({'name':item['name'],'path':str(path),'exit_code':r['exit_code'],'checks':r['checks']})
 (batch.output/'result.json').write_text(json.dumps({'result':'pass','records':records,'cases':17,'SDK_source_sha':SDK_SHA,'script_sha256':sha(Path(__file__)),'preview':True,'Accepted':False},indent=2)+'\n')
if __name__=='__main__':main()
