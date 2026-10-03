#!/usr/bin/env python3
"""C4/T08 amendment controls: immutable installed SDK helpers, explicit injection."""
import argparse,contextlib,hashlib,io,json,os,signal,subprocess,sys,time
from pathlib import Path
from unittest.mock import patch

CASES={'previous-normal':(2,['infrastructure_error','not_run']),
       'normal-recovery':(0,['passed','passed']),
       'timeout-recovery':(1,['timed_out','passed']),
       'interrupt-recovery':(130,['interrupted','not_run']),
       'persistent-permission':(2,['infrastructure_error','not_run']),
       'deadline-without-esrch':(2,['infrastructure_error','not_run']),
       'identity-unavailable':(2,['infrastructure_error','not_run']),
       'permission-before-reap':(2,['infrastructure_error','not_run'])}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def control(sdk,out,case):
    sdk=sdk.resolve();out.mkdir(parents=True,exist_ok=False)
    descriptor=json.loads((sdk/'preview-sdk.json').read_text())
    assert descriptor['candidate_sha'].startswith('1f83447b' if case=='previous-normal' else '8341ab9b')
    for name,item in descriptor['components'].items():assert sha(sdk/name)==item['sha256'],name
    assert len(descriptor['components'])==146
    sys.path.insert(0,str(sdk/'lib/toolchain'))
    import toka_test as runner
    import toka_test_process as processes
    assert Path(runner.__file__).resolve().is_relative_to(sdk)
    root=out/'project';(root/'tests').mkdir(parents=True)
    (root/'package.tk').write_text('pub const PACKAGE=(name="eperm",version="1.0.0",dependencies=())\n')
    wait=case in ('timeout-recovery','permission-before-reap')
    first='fn main()->i32 { return 0 }\n'
    if wait:first='import std/io::{println}\nextern fn libc_usleep(n:u32)->i32\nfn main()->i32 { println("READY")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0 }\n'
    (root/'tests/a_test.tk').write_text(first)
    (root/'tests/b_test.tk').write_text('import std/io::{println}\nfn main()->i32 { println("SECOND_STARTED")\n return 0 }\n')
    events=[];raw=[]
    class Probe(runner.Supervisor):
        active=None;injected=False;interrupt_sent=False;permission_sent=False
        def run(self,*args,**kwargs):
            self.active=args[3];result=super().run(*args,**kwargs)
            raw.append(json.loads(json.dumps(result)));return result
        def exited(self,pid):
            ended=super().exited(pid)
            if self.active=='run' and case=='interrupt-recovery' and ended and not self.interrupt_sent:
                self.interrupt_sent=True;events.append({'kind':'CLI_SIGINT','signal':2,'monotonic_ns':time.monotonic_ns()});os.kill(os.getpid(),signal.SIGINT)
            return ended
        def send(self,pid,number):
            os.waitid(os.P_PID,pid,os.WEXITED|os.WNOHANG|os.WNOWAIT)
            events.append({'kind':'signal_attempt','pid':pid,'signal':int(number),'wait_right_confirmed':True,'monotonic_ns':time.monotonic_ns()})
            if self.active=='run' and case=='permission-before-reap' and not self.permission_sent:
                self.permission_sent=True;raise PermissionError(1,'controlled pre-reap signal EPERM')
            return super().send(pid,number)
        def gone(self,pid):
            if self.active!='run':return super().gone(pid)
            try:os.waitid(os.P_PID,pid,os.WEXITED|os.WNOHANG|os.WNOWAIT)
            except ChildProcessError:pass
            else:raise AssertionError('probe injection requires a reaped direct child')
            events.append({'kind':'post_reap_probe','pid':pid,'monotonic_ns':time.monotonic_ns()})
            if case=='identity-unavailable':raise processes.SupervisionError('controlled post-reap identity unavailable')
            if case=='deadline-without-esrch':return False # Inject unavailable confirmation, not real group existence.
            if case=='persistent-permission' or not self.injected:
                self.injected=True;events.append({'kind':'injected_EPERM','errno':1,'pid':pid});raise PermissionError(1,'controlled post-reap group EPERM')
            actual=super().gone(pid)
            events.append({'kind':'real_OS_probe','ESRCH':actual,'pid':pid,'monotonic_ns':time.monotonic_ns()});return actual
    args=['--json']+(['--run-timeout-ms','1500'] if wait else [])
    bounded=case in ('persistent-permission','deadline-without-esrch')
    stdout,stderr=io.StringIO(),io.StringIO()
    with contextlib.ExitStack() as stack:
        stack.enter_context(patch.object(runner,'Supervisor',Probe))
        if bounded:stack.enter_context(patch.object(processes,'KILL_WAIT_MS',100))
        stack.enter_context(contextlib.redirect_stdout(stdout));stack.enter_context(contextlib.redirect_stderr(stderr))
        code=runner.execute_preview(args,sdk/'lib',sdk/'bin/tokac',root)
    report=json.loads(stdout.getvalue());wanted,results=CASES[case];tests=report['tests']
    run=next(r for r in raw if r['command'][0].endswith('test-executable'))
    recovery=case.endswith('recovery')
    started=any('SECOND_STARTED' in Path(t['logs']['run_stdout']).read_text() for t in tests if t['logs']['run_stdout'])
    checks={'exit_and_report':code==report['exit_code']==wanted,
            'test_results':[t['result'] for t in tests]==results,
            'subsequent_execution':started==(results[1]=='passed'),
            'leader_reaped':run['cleanup']['direct_child_reaped'],
            'cleanup_state':run['cleanup']['status']==('confirmed' if recovery else 'failed'),
            'no_signal_after_reap':all(e['wait_right_confirmed'] for e in events if e['kind']=='signal_attempt'),
            'finalized_preview':report['finalized'] and report['preview']}
    if recovery:
        checks['explicit_real_ESRCH']=any(e['kind']=='real_OS_probe' and e['ESRCH'] for e in events)
        checks['errno_retained']=run['cleanup']['confirmation_probe_errors'][0]['os_error']==1
    if case=='timeout-recovery':checks['timeout_preserved']=tests[0]['trigger']=='timeout' and tests[0]['phases']['run']['signal']==15
    if case=='interrupt-recovery':checks['interrupt_preserved']=report['termination']['signal']==2 and tests[0]['trigger']=='interrupt'
    if bounded:checks['original_short_budget']=95<=run['cleanup']['duration_ms']<1000
    if case=='persistent-permission':checks['errno_failure']=run['os_error']==1 and not run['cleanup']['group_gone']
    if case=='identity-unavailable':checks['identity_failure']='identity unavailable' in run['cleanup']['error'] and not run['requested_signals']
    if case=='permission-before-reap':checks['pre_reap_error_stays_failed']='pre-reap' in run['cleanup']['error']
    (out/'stdout').write_text(stdout.getvalue());(out/'stderr').write_text(stderr.getvalue())
    result={'case':case,'evidence_layer':'local installed-SDK helper fault injection; real SDK compiler and OS wait/ESRCH probes',
            'SDK_source_sha':descriptor['candidate_sha'],'components_verified':146,'SDK_modified':False,
            'shortened_confirmation_budget_ms':100 if bounded else None,'exit_code':code,'report':report,'raw_phases':raw,'probe_events':events,'checks':checks,'pass':all(checks.values())}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');assert result['pass'],result['checks']
def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--previous-sdk',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--case',choices=CASES)
    a=p.parse_args()
    if a.case:return control(a.sdk,a.output,a.case)
    assert a.previous_sdk is not None;a.output.mkdir(parents=True,exist_ok=False)
    for case in CASES:
        sdk=a.previous_sdk if case=='previous-normal' else a.sdk
        result=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--sdk',str(sdk),'--output',str(a.output/case),'--case',case],capture_output=True,timeout=60)
        (a.output/(case+'.stdout')).write_bytes(result.stdout);(a.output/(case+'.stderr')).write_bytes(result.stderr)
        assert result.returncode==0,(case,result.stderr.decode(errors='replace'));print(case+' pass',flush=True)
    records=[json.loads((a.output/case/'result.json').read_text()) for case in CASES]
    (a.output/'result.json').write_text(json.dumps({'result':'pass','cases':len(records),'records':[{'case':r['case'],'SDK_source_sha':r['SDK_source_sha'],'exit_code':r['exit_code'],'checks':r['checks']} for r in records],'platform':sys.platform,'script_sha256':sha(Path(__file__)),'preview':True,'Accepted':False},indent=2)+'\n')
if __name__=='__main__':main()
