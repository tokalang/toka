#!/usr/bin/env python3
"""T05-T08 installed-SDK cleanup controls and real CLI residual processes."""
import argparse,contextlib,io,json,os,signal,subprocess,sys,time
from pathlib import Path
from unittest.mock import patch
from test_toka_test_i2c_batch import Batch,manifest,source,sha,check
from test_toka_test_eperm_policy import CASES as POLICY_CASES
WAIT='import std/io::{println}\nextern fn libc_usleep(n:u32)->i32\nfn main()->i32 { println("READY")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0 }\n'
OK='fn main()->i32 { return 0 }\n'
FAILURES=[prefix+'-'+fault for prefix in ('interrupt','timeout') for fault in ('permission','wait','output')]
RESIDUES=['residual-runtime','residual-compiler']
NATIVE=r'''#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
int residue_fixture(void){
 int channel[2];if(pipe(channel))return 91;pid_t child=fork();if(child<0)return 92;
 if(!child){close(channel[0]);signal(SIGTERM,SIG_DFL);char c='R';if(write(channel[1],&c,1)!=1)_exit(93);close(channel[1]);for(;;)pause();}
 close(channel[1]);char c;if(read(channel[0],&c,1)!=1 || c!='R')return 94;close(channel[0]);
 char path[4096];snprintf(path,sizeof(path),"%s/residue.json",getenv("TOKA_TEST_CASE_DIR"));FILE *f=fopen(path,"w");if(!f)return 95;
 fprintf(f,"{\"leader_pid\":%d,\"child_pid\":%d,\"leader_pgid\":%d,\"child_pgid\":%d,\"ready\":true}\n",getpid(),child,getpgrp(),getpgid(child));fclose(f);return 0;
}
'''
def installed_identity(sdk):
    data=json.loads((sdk/'preview-sdk.json').read_text());assert data['candidate_sha']=='8341ab9bedaf8d10703cf9bfb89911bd6c648e0a'
    for n,v in data['components'].items():assert sha(sdk/n)==v['sha256'],n
    assert len(data['components'])==146;return data

def failure(sdk,out,case):
    installed_identity(sdk);out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(sdk/'lib/toolchain'));import toka_test as runner;import toka_test_process as processes
    assert Path(runner.__file__).resolve().is_relative_to(sdk)
    root=out/'project';manifest(root);source(root,'tests/a_test.tk',WAIT);source(root,'tests/b_test.tk','import std/io::{println}\nfn main()->i32 { println("SECOND_STARTED")\n return 0 }\n')
    state={'phase':None,'pid':None,'reaped':False,'fault':False,'interrupt':False};events=[];raw=[]
    real_waitpid=os.waitpid
    class Probe(runner.Supervisor):
        def run(self,*args,**kwargs):
            state['phase']=args[3];r=super().run(*args,**kwargs);raw.append(json.loads(json.dumps(r)));return r
        def exited(self,pid):
            ended=super().exited(pid)
            if state['phase']=='run':
                state['pid']=pid
                ready=any(b'READY' in p.read_bytes() for p in (root/'.toka/test-runs').glob('*/000001/run.stdout'))
                if case.startswith('interrupt') and ready and not ended and not state['interrupt']:
                    state['interrupt']=True;events.append({'kind':'real_SIGINT','signal':2,'monotonic_ns':time.monotonic_ns()});os.kill(os.getpid(),signal.SIGINT)
            return ended
        def send(self,pid,number):
            os.waitid(os.P_PID,pid,os.WEXITED|os.WNOHANG|os.WNOWAIT)
            events.append({'kind':'signal','pid':pid,'signal':int(number),'before_reap':True});return super().send(pid,number)
        def gone(self,pid):
            actual=super().gone(pid)
            if state['phase']=='run':
                state['reaped']=True;events.append({'kind':'real_group_probe','ESRCH':actual,'pid':pid})
                if case.endswith('permission'):raise PermissionError(1,'controlled persistent cleanup EPERM')
            return actual
    def faulty_wait(pid,options):
        if state['phase']=='run' and pid==state['pid'] and case.endswith('wait') and not state['fault']:
            state['fault']=True;events.append({'kind':'wait_failure','pid':pid,'errno':5});raise OSError(5,'controlled exit wait failure')
        return real_waitpid(pid,options)
    class OutputFault(processes.OutputTail):
        def pending_bytes(self):
            if state['phase']=='run' and state['reaped'] and case.endswith('output'):return 1
            return super().pending_bytes()
        def pump(self):
            if state['phase']=='run' and state['reaped'] and case.endswith('output'):
                state['fault']=True;events.append({'kind':'output_confirmation_failure','errno':5});raise OSError(5,'controlled output confirmation failure')
            return super().pump()
    stdout,stderr=io.StringIO(),io.StringIO()
    with patch.object(runner,'Supervisor',Probe),patch.object(os,'waitpid',faulty_wait),patch.object(processes,'OutputTail',OutputFault),patch.object(processes,'KILL_WAIT_MS',100),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):
        code=runner.execute_preview(['--json','--run-timeout-ms','1500'],sdk/'lib',sdk/'bin/tokac',root)
    # The harness owns fault-injected wait recovery; do not call this SDK confirmation.
    if case.endswith('wait'):
        try:pid,status=real_waitpid(state['pid'],os.WNOHANG);events.append({'kind':'harness_reap','pid':pid,'raw_returncode':os.waitstatus_to_exitcode(status) if pid else None})
        except ChildProcessError:events.append({'kind':'harness_wait_right_already_reaped'})
    report=json.loads(stdout.getvalue());run=raw[-1];tests=report['tests'];trigger='interrupt' if case.startswith('interrupt') else 'timeout'
    checks={'exit_code':code==report['exit_code']==2,'infrastructure_precedence':report['result']=='infrastructure_error',
            'stops_schedule':[t['result'] for t in tests]==['infrastructure_error','not_run'],
            'original_trigger':tests[0]['trigger']==run['trigger']==trigger,'cleanup_failed':run['cleanup']['status']=='failed',
            'ready_observed':b'READY' in Path(tests[0]['logs']['run_stdout']).read_bytes(),
            'second_not_started':tests[1]['phases']['compile_link']['state']=='not_started' and tests[1]['logs']['run_stdout'] is None,
            'finalized':report['finalized'],'JSON_single_report':json.loads(stdout.getvalue())==report}
    if trigger=='interrupt':checks['SIGINT_preserved']=report['termination']['signal']==2 and run['interrupt_signal']==2
    else:checks['timeout_preserved']=report['termination']['trigger']=='timeout'
    try:os.killpg(state['pid'],0);absent=False
    except ProcessLookupError:absent=True
    checks['harness_no_surviving_group']=absent
    (out/'stdout').write_text(stdout.getvalue());(out/'stderr').write_text(stderr.getvalue())
    record={'name':case,'rows':['T05' if trigger=='interrupt' else 'T06'],'exit_code':code,'report':report,'raw_phases':raw,'observations':events,'checks':checks,'contract_pass':all(checks.values()),'confirmation_budget_injected_ms':100,'evidence_layer':'installed SDK helper fault injection with actual compiler/runtime, SIGINT and OS probes; harness reap separate'}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');assert record['contract_pass'],checks

class Residual(Batch):
    def run_case(self,case):
        root=self.project(case,[('a_test.tk',OK),('b_test.tk',OK)]);active=None
        if case=='residual-runtime':
            dep=self.work/'fixture';manifest(dep);(dep/'package.tk').write_text('pub const PACKAGE=(name="fixture",version="1.0.0",dependencies=(),native=(required=true,sources=("native/residue.c")))\n')
            source(dep,'native/residue.c',NATIVE);source(dep,'lib/official/fixture.tk','extern fn residue_fixture()->i32\npub fn residual()->i32 { return residue_fixture() }\n')
            manifest(root,'fixture="../fixture",');source(root,'tests/a_test.tk','import official/fixture::{residual}\nfn main()->i32 { return residual() }\n')
            r=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=self.env,capture_output=True,timeout=30);assert r.returncode==0,r.stderr
        else:
            active=self.output/'compiler-proxy';(active/'bin').mkdir(parents=True);(active/'lib').symlink_to(self.sdk/'lib',target_is_directory=True)
            import shutil
            shutil.copyfile(self.sdk/'bin/toka',active/'bin/toka');(active/'bin/toka').chmod(0o755)
            code='#!'+sys.executable+'\nimport json,os,pathlib,signal,subprocess,sys,time\nargs=sys.argv[1:]\nr=subprocess.call(['+repr(str(self.sdk/'bin/tokac'))+',*args])\nif r or not any(a.endswith("a_test.tk") for a in args):sys.exit(r)\nrd,wr=os.pipe();child=os.fork()\nif not child:\n os.close(rd);signal.signal(signal.SIGTERM,signal.SIG_DFL);os.write(wr,b"R");os.close(wr)\n while True:time.sleep(.01)\nos.close(wr);assert os.read(rd,1)==b"R";os.close(rd)\npathlib.Path(os.environ["TOKA_TEST_CASE_DIR"],"residue.json").write_text(json.dumps({"leader_pid":os.getpid(),"child_pid":child,"leader_pgid":os.getpgrp(),"child_pgid":os.getpgid(child),"ready":True}))\n'
            (active/'bin/tokac').write_text(code);(active/'bin/tokac').chmod(0o755)
        r=self.cli(case,root,expected=1,rows=['T07'],active=active);t=r['tests'][0];key='run' if case=='residual-runtime' else 'compile_link';phase=t['phases'][key]
        marker=Path(t['logs']['run_stdout'] if key=='run' else t['logs']['compile_stdout']).parent/'residue.json';witness=json.loads(marker.read_text());survivors=[]
        for pid in (witness['leader_pid'],witness['child_pid']):
            try:os.kill(pid,0);survivors.append(pid)
            except ProcessLookupError:pass
        checks={'raw_exit_zero':phase['exit_code']==0 and phase['signal'] is None,'residual_trigger':t['trigger']=='residual_process' and t['reason']=='residual_process',
                'result':t['result']==('run_failed' if key=='run' else 'compile_failed'),'later_passed':r['tests'][1]['result']=='passed',
                'ready_same_group':witness['ready'] and witness['leader_pgid']==witness['child_pgid']==phase['process']['pgid'],
                'cleanup_confirmed':t['cleanup']['status']=='confirmed' and t['cleanup']['leader_reaped'] and t['cleanup']['group_absent'] and t['cleanup']['output_complete'],
                'no_survivors':not survivors}
        if key=='compile_link':checks['no_first_run']=t['phases']['run']['state']=='not_started'
        folder=self.output/case;record=json.loads((folder/'result.json').read_text());record.update(checks=checks,contract_pass=all(checks.values()),witness=witness,surviving_members=survivors)
        (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        if active:(active/'lib').unlink()
        assert record['contract_pass'],checks

def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--previous-sdk',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--case',choices=FAILURES+RESIDUES)
    a=p.parse_args();a.sdk=a.sdk.resolve();a.output=a.output.resolve();installed_identity(a.sdk)
    if a.case:
        if a.case in RESIDUES:Residual(a.sdk,a.output).run_case(a.case)
        else:failure(a.sdk,a.output,a.case)
        return
    assert a.previous_sdk;a.output.mkdir(parents=True,exist_ok=False);records=[]
    for case in list(POLICY_CASES)+FAILURES+RESIDUES:
        policy=case in POLICY_CASES;script=Path(__file__).with_name('test_toka_test_eperm_policy.py') if policy else Path(__file__)
        sdk=a.previous_sdk if case=='previous-normal' else a.sdk
        child=subprocess.run([sys.executable,str(script),'--sdk',str(sdk),'--output',str(a.output/case),'--case',case],capture_output=True,timeout=60)
        (a.output/(case+'.stdout')).write_bytes(child.stdout);(a.output/(case+'.stderr')).write_bytes(child.stderr)
        assert child.returncode==0,(case,child.stderr.decode(errors='replace'))
        path=a.output/case/(case+'/result.json' if case in RESIDUES else 'result.json');r=json.loads(path.read_text())
        assert all(r['checks'].values());records.append({'name':case,'path':str(path),'exit_code':r['exit_code'],'checks':r['checks']});print(case+' pass',flush=True)
    (a.output/'result.json').write_text(json.dumps({'result':'pass','cases':16,'records':records,'SDK_source_sha':'8341ab9bedaf8d10703cf9bfb89911bd6c648e0a','script_sha256':sha(Path(__file__)),'preview':True,'Accepted':False},indent=2)+'\n')
if __name__=='__main__':main()
