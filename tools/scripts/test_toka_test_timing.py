#!/usr/bin/env python3
"""M04 monotonic clock injection and M05 genuine fixed-task cold/warm samples."""
import argparse,contextlib,datetime,hashlib,io,json,os,platform,shutil,subprocess,sys,time
from pathlib import Path
from unittest.mock import patch
from test_toka_test_i2c_batch import Batch,manifest,source,sha,check
SDK_SHA='8341ab9bedaf8d10703cf9bfb89911bd6c648e0a'
OK='import official/dep::{answer}\nfn main()->i32 {\n if answer() != 42 { return 7 }\n return 0\n}\n'
WAIT='import std/io::{println}\nextern fn libc_usleep(n:u32)->i32\nfn main()->i32 { println("READY_CLOCK")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0 }\n'
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def tree(root):return {str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file() and '.toka' not in p.relative_to(root).parts}
def durations(report):
 vals=[]
 for p in report['timings'].values():
  if p['duration_ms'] is not None:vals.append(p['duration_ms'])
 for t in report['tests']:
  for p in t['phases'].values():
   if p['duration_ms'] is not None:vals.append(p['duration_ms'])
  if t['cleanup']['duration_ms'] is not None:vals.append(t['cleanup']['duration_ms'])
 return vals
class Timing(Batch):
 def host(self):
  records={'platform':platform.platform(),'machine':platform.machine(),'processor':platform.processor(),'cpu_count':os.cpu_count(),'python':sys.version,'uname':list(os.uname()),'recorded_at_UTC':utc(),'runner_environment':{k:os.environ.get(k) for k in ('RUNNER_OS','RUNNER_ARCH','RUNNER_NAME','ImageOS','ImageVersion','GITHUB_RUN_ID','GITHUB_RUN_ATTEMPT')}}
  commands=[['cc','--version'],['llvm-config','--version']]
  commands += [['sysctl','hw.model','hw.ncpu','hw.memsize','machdep.cpu.brand_string']] if sys.platform=='darwin' else [['lscpu']]
  records['commands']=[]
  for args in commands:
   try:
    r=subprocess.run(args,capture_output=True,timeout=10);records['commands'].append({'argv':args,'exit_code':r.returncode,'stdout':r.stdout.decode(errors='replace'),'stderr':r.stderr.decode(errors='replace')})
   except (OSError,subprocess.TimeoutExpired) as e:records['commands'].append({'argv':args,'error':str(e)})
  (self.output/'host.json').write_text(json.dumps(records,indent=2)+'\n')
 def samples(self):
  root=self.project('fixed-task',[('ok_test.tk',OK)]);dep=root/'dep';manifest(dep);source(dep,'lib/official/dep.tk','pub fn answer()->i32 { return 42 }\n');manifest(root,'dep="./dep",')
  command=[str(self.sdk/'bin/toka'),'fetch'];started=time.monotonic();first=utc();fetch=subprocess.run(command,cwd=root,env=self.env,capture_output=True,timeout=90)
  setup={'argv':command,'cwd':str(root),'first_SDK_command_started_UTC':first,'duration_ms':(time.monotonic()-started)*1000,'exit_code':fetch.returncode,'network_acquisition_ms':None,'network_basis':'fixed local dependency; no network acquisition in this fixture','stage':'setup excluded from sample durations'}
  (self.output/'setup.stdout').write_bytes(fetch.stdout);(self.output/'setup.stderr').write_bytes(fetch.stderr);(self.output/'setup.json').write_text(json.dumps(setup,indent=2)+'\n');assert fetch.returncode==0,fetch.stderr
  baseline=tree(root);lock=sha(root/'package.lock');records=[]
  common={name:value for name,value in baseline.items() if name!='package.lock'}
  task={'common_task_source_hashes':common,'common_task_id':hashlib.sha256(json.dumps(common,sort_keys=True).encode()).hexdigest(),'source_hashes':baseline,'lock_sha256':lock,'task_path':str(root),'fixed_task_id':hashlib.sha256(json.dumps(baseline,sort_keys=True).encode()).hexdigest(),'cold_definition':'remove only this owned project .toka before each cold call','warm_definition':'immediately after paired cold call, same project path and lock, retain .toka','OS_page_cache_controlled':False,'SDK_startup_cache_controlled':False,'SDK_commands_before_first_measured_sample':['fetch'],'pairs':5,'sample_command':[str(self.sdk/'bin/toka'),'test','--json'],'external_guard_ms':90000}
  (self.output/'task.json').write_text(json.dumps(task,indent=2)+'\n')
  for index in range(5):
   for mode in ('cold','warm'):
    cache=root/'.toka'
    if mode=='cold' and cache.exists():assert cache.is_dir() and not cache.is_symlink();shutil.rmtree(cache)
    before={'cache_exists':cache.exists(),'prior_run_ids':sorted(p.name for p in (cache/'test-runs').glob('*')) if cache.exists() else [],'source_hashes':tree(root),'lock_sha256':sha(root/'package.lock')}
    assert before['source_hashes']==baseline and before['lock_sha256']==lock
    assert before['cache_exists']==(mode=='warm')
    name='M05-'+mode+'-'+str(index+1);folder=self.output/name;folder.mkdir();command=task['sample_command'];started=time.monotonic();started_utc=utc()
    try:child=subprocess.run(command,cwd=root,env=self.env,capture_output=True,timeout=90)
    except subprocess.TimeoutExpired as e:
     (folder/'stdout').write_bytes(e.stdout or b'');(folder/'stderr').write_bytes(e.stderr or b'');(folder/'incomplete.json').write_text(json.dumps({'kind':'external_guard_truncated','duration_ms':(time.monotonic()-started)*1000,'guard_ms':90000,'not_normal_completion':True}));raise
    external=(time.monotonic()-started)*1000;report=json.loads(child.stdout);check(report)
    (folder/'stdout').write_bytes(child.stdout);(folder/'stderr').write_bytes(child.stderr)
    run=Path(report['artifact_root']);shutil.copytree(run,folder/'test-run');raw=json.loads((run/'preview.json').read_text());t=report['tests'][0]
    total=report['timings']['total']['duration_ms'];execution=report['timings']['execution']['duration_ms'];compile_run=t['phases']['compile_link']['duration_ms']+t['phases']['run']['duration_ms']
    checks={'exit_zero':child.returncode==report['exit_code']==0,'passes':t['result']=='passed','task_unchanged':tree(root)==baseline,'lock_unchanged':sha(root/'package.lock')==lock==report['identity']['lock_sha256'],'nonnegative_times':all(v>=0 for v in durations(report)),'actual_contains_snapshot':external>=total,'snapshot_contains_execution':total>=execution,'execution_contains_compile_run':execution>=compile_run,'combined_compile_link':t['compile_mode']=='combined' and t['phases']['compile']['duration_ms'] is None and t['phases']['link']['duration_ms'] is None,'snapshot_boundary':report['timing_boundary']=='total and report_preparation stop at final serialization snapshot'}
    record={'name':name,'row':'M05','pair':index+1,'cache_mode':mode,'first_measured_execution':index==0 and mode=='cold','started_UTC':started_utc,'argv':command,'cwd':str(root),'exit_code':child.returncode,'report':report,'raw_receipt':raw,'external_process_ms':external,'external_minus_snapshot_ms':external-total,'before':before,'checks':checks,'contract_pass':all(checks.values()),'evidence_layer':'real installed CLI without helper/clock/timeout instrumentation'}
    (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');assert record['contract_pass'],checks;records.append(record);print(name+' pass',flush=True)
  samples=[{'name':r['name'],'cache_mode':r['cache_mode'],'pair':r['pair'],'first_measured_execution':r['first_measured_execution'],'external_process_ms':r['external_process_ms'],'C6_total_ms':r['report']['timings']['total']['duration_ms'],'compile_link_ms':r['report']['tests'][0]['phases']['compile_link']['duration_ms'],'run_ms':r['report']['tests'][0]['phases']['run']['duration_ms']} for r in records]
  (self.output/'M05-summary.json').write_text(json.dumps({'result':'pass','samples':samples,'all_samples_retained':True,'first_sample_removed':False,'means':{mode:{key:sum(r[key] for r in samples if r['cache_mode']==mode)/5 for key in ('external_process_ms','C6_total_ms','compile_link_ms','run_ms')} for mode in ('cold','warm')}},indent=2)+'\n')
 def clock(self):
  root=self.project('wall-clock',[('wait_test.tk',WAIT)]);runner=self.runner;events=[];raw=[];real_wall=time.time;state={'backwards':False}
  def wall():return real_wall()-(86400 if state['backwards'] else 0)
  class Backward(runner.Supervisor):
   phase=None
   def run(self,*args,**kwargs):self.phase=args[3];r=super().run(*args,**kwargs);raw.append(json.loads(json.dumps(r)));return r
   def exited(self,pid):
    ended=super().exited(pid)
    if self.phase=='run':
     now=time.time();ready=any(b'READY_CLOCK' in p.read_bytes() for p in (root/'.toka/test-runs').glob('*/000001/run.stdout'))
     if ready and not state['backwards']:
      events.append({'kind':'before_wall_jump','wall':now,'monotonic':time.monotonic(),'ready':True});state['backwards']=True;events.append({'kind':'after_wall_jump','wall':time.time(),'monotonic':time.monotonic(),'ready':True})
    return ended
  stdout,stderr=io.StringIO(),io.StringIO()
  with patch.object(runner,'Supervisor',Backward),patch.object(time,'time',wall),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):code=runner.execute_preview(['--json','--run-timeout-ms','5000'],self.sdk/'lib',self.sdk/'bin/tokac',root)
  report=json.loads(stdout.getvalue());check(report);t=report['tests'][0];folder=self.output/'M04';folder.mkdir();(folder/'stdout').write_text(stdout.getvalue());(folder/'stderr').write_text(stderr.getvalue());shutil.copytree(Path(report['artifact_root']),folder/'test-run')
  checks={'timeout':code==report['exit_code']==1 and t['result']=='timed_out' and t['trigger']=='timeout','wall_really_decreased':len(events)==2 and events[1]['wall']<events[0]['wall'],'monotonic_continued':len(events)==2 and events[1]['monotonic']>=events[0]['monotonic'],'actual_budget':5000<=t['phases']['run']['duration_ms']<10000 and report['timeouts']['run_ms']==5000,'nonnegative_times':all(v>=0 for v in durations(report)),'cleanup_confirmed':t['cleanup']['status']=='confirmed' and t['cleanup']['group_absent'] and t['cleanup']['leader_reaped'],'raw_signal':t['phases']['run']['signal']==15}
  (folder/'result.json').write_text(json.dumps({'name':'M04','exit_code':code,'report':report,'raw_phases':raw,'clock_observations':events,'checks':checks,'contract_pass':all(checks.values()),'scope':'installed SDK helper wall-clock API injection; OS clock unchanged, real monotonic unmodified','evidence_layer':'controlled wall API rollback after actual program readiness'},indent=2)+'\n');assert all(checks.values()),checks
 def run(self):
  data=json.loads((self.sdk/'preview-sdk.json').read_text());assert data['candidate_sha']==SDK_SHA and len(data['components'])==146
  for name,value in data['components'].items():assert sha(self.sdk/name)==value['sha256']
  self.host();self.samples();self.clock()
  (self.output/'result.json').write_text(json.dumps({'result':'pass','SDK_source_sha':SDK_SHA,'script_sha256':sha(Path(__file__)),'measured_samples':10,'clock_controls':1,'setup_fetch_commands':1,'preview':True,'Accepted':False},indent=2)+'\n')
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();Timing(a.sdk.resolve(),a.output.resolve()).run()
if __name__=='__main__':main()
