#!/usr/bin/env python3
"""Real installed CLI lifecycle receipts, with explicit readiness and raw status."""
import argparse,fcntl,json,os,shutil,signal,subprocess,sys,time
from pathlib import Path
from test_toka_test_i2c_batch import Batch,source,manifest,sha
OK='fn main()->i32 { return 0 }\n'
WAIT='import std/io::{println}\nextern fn libc_usleep(n:u32)->i32\nfn main()->i32 { println("READY_RUNTIME")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0 }\n'
NATIVE=r'''#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
static pid_t child;
static void exit_child(int n){_exit(0);}
static void stop_parent(int n){int status;while(waitpid(child,&status,0)<0 && errno==EINTR){};_exit(0);}
int lifecycle_fixture(int mode){
 int ready[2],ack[2];if(pipe(ready)||pipe(ack))return 90;
 child=fork();if(child<0)return 91;
 if(!child){
  close(ready[0]);close(ack[1]);signal(SIGTERM,mode==2?SIG_IGN:exit_child);
  char ch='R';if(write(ready[1],&ch,1)!=1)return 92;
  if(read(ack[0],&ch,1)!=1 || ch!='A')return 93;
  ch='C';if(write(ready[1],&ch,1)!=1)return 94;
  for(;;)pause();
 }
 close(ready[1]);close(ack[0]);signal(SIGTERM,stop_parent);
 char ch;if(read(ready[0],&ch,1)!=1 || ch!='R')return 95;
 ch='A';if(write(ack[1],&ch,1)!=1)return 96;
 if(read(ready[0],&ch,1)!=1 || ch!='C')return 97;
 char path[4096];snprintf(path,sizeof(path),"%s/handshake.json",getenv("TOKA_TEST_CASE_DIR"));
 FILE *file=fopen(path,"w");if(!file)return 98;
 struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
 fprintf(file,"{\"leader_pid\":%d,\"child_pid\":%d,\"leader_pgid\":%d,\"child_pgid\":%d,\"bidirectional_ready\":true,\"child_ignores_term\":%s,\"monotonic_ns\":%lld}\n",getpid(),child,getpgrp(),getpgid(child),mode==2?"true":"false",(long long)now.tv_sec*1000000000LL+now.tv_nsec);
 fclose(file);puts("READY_NATIVE");fflush(stdout);for(;;)pause();
}
'''
class Lifecycle(Batch):
 def save_case(self,name,root,command,child,out,err,rows,expected,checks):
  r,end=json.JSONDecoder().raw_decode(out.decode());assert not out.decode()[end:].strip()
  folder=self.output/name;folder.mkdir();(folder/'stdout').write_bytes(out);(folder/'stderr').write_bytes(err)
  if (root/'.toka/test-runs').exists():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
  count=r['summary'];total=count['total'];tests=r['tests']
  facts={'exit_code':child.returncode==r['exit_code']==expected,'finalized':r['finalized'],'summary_reconciles':total is None or total==len(tests)==sum(v for k,v in count.items() if k!='total')}
  facts.update(checks(r))
  data={'name':name,'rows':rows,'command':command,'exit_code':child.returncode,'expected_exit_code':expected,'report':r,'checks':facts,'contract_pass':all(facts.values()),'evidence_layer':'installed SDK real CLI; explicit compiler/selection injection where recorded'}
  (folder/'result.json').write_text(json.dumps(data,indent=2)+'\n');self.results.append(data);return r
 def call(self,name,root,args,rows,expected,checks,active=None,env=None,ready=None):
  command=[str((active or self.sdk)/'bin/toka'),'test','--json',*args];child=subprocess.Popen(command,cwd=root,env=env or self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);sent=None
  try:
   if ready:
    end=time.monotonic()+15
    while time.monotonic()<end:
     marker=ready(root)
     if marker:break
     if child.poll() is not None:raise AssertionError('CLI ended before readiness')
     time.sleep(.01)
    else:raise AssertionError('fixture did not become ready')
    sent={'pid':child.pid,'signal':2,'monotonic_ns':time.monotonic_ns(),'readiness':str(marker)};os.kill(child.pid,signal.SIGINT)
    if name=='T04-selection':(self.output/'selection-release').touch()
   out,err=child.communicate(timeout=20)
  finally:
   if child.poll() is None:child.terminate();child.communicate(timeout=10)
  r=self.save_case(name,root,command,child,out,err,rows,expected,checks)
  if sent:(self.output/name/'interrupt-witness.json').write_text(json.dumps(sent,indent=2)+'\n')
  return r
 def compiler(self):
  variant=self.output/'compiler-proxy';(variant/'bin').mkdir(parents=True);(variant/'lib').symlink_to(self.sdk/'lib',target_is_directory=True);shutil.copyfile(self.sdk/'bin/toka',variant/'bin/toka');(variant/'bin/toka').chmod(0o755)
  script='#!'+sys.executable+'\nimport json,os,pathlib,subprocess,sys,time\noriginal='+repr(str(self.sdk/'bin/tokac'))+'\nargs=sys.argv[1:]\nif args==["--version"]:sys.exit(subprocess.call([original,*args]))\nif any("a_compile_wait_test.tk" in arg for arg in args):\n print("READY_COMPILER",flush=True)\n while True:time.sleep(.01)\nsys.exit(subprocess.call([original,*args]))\n'
  (variant/'bin/tokac').write_text(script);(variant/'bin/tokac').chmod(0o755);return variant
 def ready_log(self,root,stage,marker):return next((p for p in (root/'.toka/test-runs').glob('*/000001/'+stage+'.stdout') if marker in p.read_bytes()),None)
 def phase(self,r,key):return r['tests'][0]['phases'][key]
 def confirmed(self,r,key):
  t=r['tests'][0];c=t['cleanup'];return {'cleanup_confirmed':c['status']=='confirmed','leader_reaped':c['leader_reaped'] is True,'group_absent':c['group_absent'] is True,'logs_closed':c['output_complete'] is True,'scope':r['supervision']['scope']=='direct_child_and_process_group','phase_started':t['phases'][key]['process'] is not None}
 def timeout(self,r,key):return {**self.confirmed(r,key),'timed_out':r['tests'][0]['result']=='timed_out','trigger':r['tests'][0]['trigger']=='timeout','continues':r['tests'][1]['result']=='passed','raw_signal':self.phase(r,key)['signal']==15,'bounded':self.phase(r,key)['duration_ms']<10000}
 def run(self):
  proxy=self.compiler();root=self.project('compile-timeout',[('a_compile_wait_test.tk',OK),('b_test.tk',OK)])
  self.call('R06',root,['--compile-timeout-ms','2000'],['R06'],1,lambda r:{**self.timeout(r,'compile_link'),'no_run':self.phase(r,'run')['state']=='not_started','ready':b'READY_COMPILER' in Path(r['tests'][0]['logs']['compile_stdout']).read_bytes()},active=proxy)
  root=self.project('run-timeout',[('a_test.tk',WAIT),('b_test.tk',OK)])
  self.call('R07',root,['--run-timeout-ms','1500'],['R07'],1,lambda r:{**self.timeout(r,'run'),'ready':b'READY_RUNTIME' in Path(r['tests'][0]['logs']['run_stdout']).read_bytes()})
  dep=self.work/'native-fixture';manifest(dep);(dep/'package.tk').write_text('pub const PACKAGE=(name="fixture",version="1.0.0",dependencies=(),native=(required=true,sources=("native/lifecycle.c")))\n');source(dep,'native/lifecycle.c',NATIVE);source(dep,'lib/official/fixture.tk','extern fn lifecycle_fixture(mode:i32)->i32\npub fn wait_fixture(mode:i32)->i32 { return lifecycle_fixture(mode) }\n')
  for mode,name in ((1,'T01'),(2,'T02')):
   body='import official/fixture::{wait_fixture}\nfn main()->i32 { return wait_fixture('+str(mode)+') }\n';root=self.project('native-'+name,[('a_test.tk',body),('b_test.tk',OK)]);manifest(root,'fixture="../native-fixture",')
   fetch=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=self.env,capture_output=True,timeout=30);assert fetch.returncode==0,fetch.stderr
   def checks(r,name=name):
    t=r['tests'][0];path=Path(t['logs']['run_stdout']).parent/'handshake.json';handshake=json.loads(path.read_text());alive=[]
    for pid in (handshake['leader_pid'],handshake['child_pid']):
     try:os.kill(pid,0);alive.append(pid)
     except ProcessLookupError:pass
    (self.output/name/'handshake-observation.json').write_text(json.dumps({'handshake':handshake,'surviving_members':alive,'observation_monotonic_ns':time.monotonic_ns()},indent=2)+'\n')
    f={**self.confirmed(r,'run'),'timed_out':t['result']=='timed_out','trigger':t['trigger']=='timeout','continues':r['tests'][1]['result']=='passed','handshake':handshake['bidirectional_ready'],'same_group':handshake['leader_pgid']==handshake['child_pgid']==t['phases']['run']['process']['pgid'],'no_survivors':not alive,'raw_status':t['phases']['run']['signal']==9 if name=='T02' else t['phases']['run']['exit_code']==0}
    signals=t['cleanup']['requested_signals'];f['requested_signals']=signals==([15,9] if name=='T02' else [15])
    if name=='T02':f.update(grace_observed=t['cleanup']['duration_ms']>=1900,bounded=t['cleanup']['duration_ms']<8000,child_ignored_term=handshake['child_ignores_term'])
    return f
   self.call(name,root,['--run-timeout-ms','2000'],[name],1,checks)
  root=self.project('interrupt-runtime',[('a_test.tk',WAIT),('b_test.tk',OK)])
  def interrupt(r,key):return {**self.confirmed(r,key),'interrupted':r['tests'][0]['result']=='interrupted','interrupt_signal':r['interrupt_signal']==2 and r['termination']['signal']==2,'trigger':r['tests'][0]['trigger']=='interrupt','not_run':r['tests'][1]['result']=='not_run','phase':r['termination']['phase']==key,'raw_signal':self.phase(r,key)['signal']==15}
  self.call('T03',root,[],['T03'],130,lambda r:interrupt(r,'run'),ready=lambda root:self.ready_log(root,'run',b'READY_RUNTIME'))
  root=self.project('interrupt-compile',[('a_compile_wait_test.tk',OK),('b_test.tk',OK)])
  self.call('T04-compile',root,[],['T04'],130,lambda r:{**interrupt(r,'compile_link'),'no_run':self.phase(r,'run')['state']=='not_started'},active=proxy,ready=lambda root:self.ready_log(root,'compile',b'READY_COMPILER'))
  root=self.project('interrupt-dependencies',[('a_test.tk',OK),('b_test.tk',OK)]);manifest(root,'fixture="../native-fixture",');fetch=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=self.env,capture_output=True,timeout=30);assert fetch.returncode==0
  fd=(root/'.toka/test-context.lock').open('w');fcntl.flock(fd,fcntl.LOCK_EX)
  try:
   self.call('T04-dependencies',root,[],['T04'],130,lambda r:{'interrupted':r['result']=='interrupted','all_not_run':all(t['result']=='not_run' and t['phases']['compile_link']['state']=='not_started' for t in r['tests']),'signal':r['interrupt_signal']==2,'active_phase':r['termination']['phase']=='context','partial_timing':r['timings']['dependencies']['duration_ms'] is not None,'cleanup':r['preparation']['context']['cleanup']['status']=='confirmed','raw_signal':r['preparation']['context']['phase']['signal']==15},ready=lambda root:next((p for p in (root/'.toka/test-runs').glob('*/context.stdout') if b'Waiting for project dependency lock' in p.read_bytes()),None))
  finally:fd.close()
  root=self.project('interrupt-selection',[('a_test.tk',OK),('b_test.tk',OK)]);launch=self.output/'selection-python';launch.mkdir();marker=self.output/'selection-ready';release=self.output/'selection-release'
  wrapper='#!'+sys.executable+'\nimport pathlib,sys,time\nsys.path.insert(0,'+repr(str(self.sdk/'lib/toolchain'))+')\nimport toka_test as runner\noriginal=runner.select_entries\ndef gated(*args):\n pathlib.Path('+repr(str(marker))+').write_text("selection entered")\n while not pathlib.Path('+repr(str(release))+').exists():time.sleep(.01)\n return original(*args)\nrunner.select_entries=gated\nsys.argv=sys.argv[1:]\nsys.exit(runner.main())\n';(launch/'python3').write_text(wrapper);(launch/'python3').chmod(0o755)
  self.call('T04-selection',root,[],['T04'],130,lambda r:{'interrupted':r['result']=='interrupted','signal':r['interrupt_signal']==2,'phase':r['termination']['phase']=='selection','no_preparation':r['preparation']=={},'no_compile':all(t['phases']['compile_link']['state']=='not_started' for t in r['tests']),'partial_timing':r['timings']['selection']['duration_ms'] is not None,'known_count_retained':r['summary']['total']==2 and len(r['tests'])==2 and r['summary']['not_run']==2},env=dict(self.env,PATH=str(launch)+os.pathsep+self.env['PATH']),ready=lambda root:marker if marker.exists() else None)
  (self.output/'T04-selection/injection.json').write_text(json.dumps({'layer':'real installed CLI with Python launcher gating installed helper selection','public_CLI_option':False,'wrapper_sha256':sha(launch/'python3'),'SDK_modified':False},indent=2)+'\n')
  (proxy/'lib').unlink()
  records=[{'name':r['name'],'rows':r['rows'],'exit_code':r['exit_code'],'checks':r['checks'],'contract_pass':r['contract_pass']} for r in self.results];(self.output/'result.json').write_text(json.dumps({'schema':'toka.lifecycle-acceptance','contract_pass':all(r['contract_pass'] for r in records),'records':records},indent=2)+'\n')
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--observe',action='store_true');a=p.parse_args();b=Lifecycle(a.sdk.resolve(),a.output.resolve());b.run();d=json.loads((b.output/'result.json').read_text());print(json.dumps({'cases':len(d['records']),'contract_pass':d['contract_pass'],'failed':[(r['name'],[k for k,v in r['checks'].items() if not v]) for r in d['records'] if not r['contract_pass']]}));return 0 if a.observe or d['contract_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
