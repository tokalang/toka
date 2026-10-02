#!/usr/bin/env python3
"""I2-A controls and real installed-manager lifecycle fixtures (not C6 JSON)."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'lib/toolchain'))
import toka_test as runner
import toka_test_process as processes
from test_toka_test_i1 import manifest, source

FIXTURE = '''import os, signal, subprocess, sys, time, json, pathlib
location = os.environ.get('FIXTURE_EVENT_PATH')
if not location and os.environ.get('TOKA_TEST_CASE_DIR'):
 location = str(pathlib.Path(os.environ['TOKA_TEST_CASE_DIR'])/'fixture-events.jsonl')
def event(stage):
 if location:
  with open(location,'a') as stream:stream.write(json.dumps({'stage':stage,'monotonic_ns':time.monotonic_ns(),'pid':os.getpid()})+'\\n')
event('entered')
delay = int(os.environ.get('FIXTURE_STARTUP_DELAY_MS','0'))
if delay: event('startup_delay');time.sleep(delay/1000)
mode = sys.argv[1]
if mode == 'exit': sys.exit(0)
if mode == 'signal': os.kill(os.getpid(), signal.SIGABRT)
if mode == 'ignore':
 signal.signal(signal.SIGTERM, signal.SIG_IGN)
 event('term_ignored')
if mode == 'residual':
 p = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(60)'])
 print('RESIDUAL', p.pid, flush=True)
 sys.exit(0)
if mode == 'children':
 p = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(60)'])
 def stop(number, frame):
  p.wait(timeout=3); sys.exit(0)
 signal.signal(signal.SIGTERM, stop)
 print('CHILD', p.pid, flush=True)
event('ready')
print('READY', flush=True)
while True: time.sleep(.01)
'''
COMPILER = '''#!{python}
import os,pathlib,sys,time
if sys.argv[1:]==['--version']:
 if os.environ.get('FIXTURE_PROBE_WAIT')=='1':
  print('READY',flush=True)
  time.sleep(60)
 print('controlled compiler');sys.exit(0)
args=sys.argv[1:]
entry=next(pathlib.Path(a) for a in args if a.endswith('.tk') and '=' not in a)
mode=entry.read_text().strip()
if mode=='compile_wait':print('READY',flush=True);time.sleep(60)
if mode=='compile_signal':os.kill(os.getpid(),6)
exe=pathlib.Path(args[args.index('-o')+1])
exe.write_text('#!{python}\\n'+{fixture!r}+'\\n')
# The generated program reads its fixture mode as an argument injected before its code.
data=exe.read_text().replace('mode = sys.argv[1]', 'mode = '+repr(mode))
exe.write_text(data);exe.chmod(0o755)
'''


def fake_compiler(path):
    path.write_text(COMPILER.format(python=sys.executable, fixture=FIXTURE));path.chmod(0o755)


def receipt(root):
    files=list((root/'.toka/test-runs').glob('*/preview.json'))
    assert len(files)==1, files
    return json.loads(files[0].read_text())


def assert_confirmed(phase):
    assert phase['cleanup']['status']=='confirmed', phase
    assert all(phase['cleanup'][k] for k in ('group_gone','direct_child_reaped','logs_closed')),phase
    try:os.killpg(phase['pgid'],0)
    except ProcessLookupError:return
    raise AssertionError('group remains after confirmation')


@unittest.skipUnless(os.name == 'posix', 'I2-A supervision is native POSIX only')
class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='i2a-control-');self.root=Path(self.temp.name)
        self.counter=0
        self.sdk=self.root/'control-lib';(self.sdk/'sys').mkdir(parents=True);(self.sdk/'sys/toka_rt.o').write_bytes(b'controlled runtime fixture')
    def tearDown(self):
        retain=os.environ.get('TOKA_TEST_CONTROL_EVIDENCE')
        if retain:
            target=Path(retain)/self._testMethodName;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copytree(self.root,target)
        self.temp.cleanup()
    def phase(self, mode, budget=100, supervisor=None):
        self.counter+=1;folder=self.root/str(self.counter);folder.mkdir()
        return (supervisor or processes.Supervisor()).run([sys.executable,'-c',FIXTURE,mode],self.root,folder,'run',dict(os.environ,FIXTURE_EVENT_PATH=str(folder/'fixture-events.jsonl')),budget)
    def test_normal_exit_and_raw_signal(self):
        p=self.phase('exit',5000);assert_confirmed(p);self.assertEqual(p['exit_code'],0);self.assertIsNone(p['trigger'])
        p=self.phase('signal',5000);assert_confirmed(p);self.assertEqual(p['signal'],signal.SIGABRT)
    def test_timeout_and_term_kill_escalation(self):
        p=self.phase('ignore',5000);assert_confirmed(p);self.assertEqual(p['trigger'],'timeout')
        events=[json.loads(line) for line in (Path(p['stdout']).parent/'fixture-events.jsonl').read_text().splitlines()]
        self.assertIn('term_ignored',[x['stage'] for x in events]);self.assertIn('ready',[x['stage'] for x in events])
        self.assertTrue(p['cleanup']['kill_sent']);self.assertEqual(p['signal'],signal.SIGKILL)
        self.assertGreaterEqual(p['duration_ms'],2200)
    def test_controlled_descendant_and_normal_residual(self):
        p=self.phase('children',250);assert_confirmed(p);self.assertEqual(p['trigger'],'timeout')
        p=self.phase('residual',5000);assert_confirmed(p);self.assertEqual(p['trigger'],'residual_process');self.assertEqual(p['exit_code'],0)
    def test_confirmation_failure_preserves_trigger(self):
        class CannotConfirm(processes.Supervisor):
            def gone(self,pid):
                super().gone(pid)
                raise PermissionError('injected EPERM confirmation')
        p=self.phase('wait',100,CannotConfirm());self.assertEqual(p['trigger'],'timeout');self.assertEqual(p['cleanup']['status'],'failed')
        self.assertTrue(p['cleanup']['direct_child_reaped'])
    def test_no_signal_after_reap(self):
        class Observed(processes.Supervisor):
            def __init__(self):super().__init__();self.sent=[]
            def send(self,pid,number):
                os.waitid(os.P_PID,pid,os.WEXITED|os.WNOHANG|os.WNOWAIT)
                self.sent.append(number);super().send(pid,number)
        s=Observed();p=self.phase('ignore',5000,s);assert_confirmed(p)
        self.assertEqual(s.sent,[signal.SIGTERM,signal.SIGKILL])
    def test_interrupt_after_worker_exit_skips_unneeded_group_signal(self):
        class EndedInterrupt(processes.Supervisor):
            def exited(self,pid):
                ended=super().exited(pid)
                if ended:self._interrupt(signal.SIGINT,None)
                return ended
        with patch.object(os,'killpg',wraps=os.killpg) as calls:
            raw=self.phase('exit',5000,EndedInterrupt())
        assert_confirmed(raw)
        self.assertEqual(raw['trigger'],'interrupt');self.assertEqual(raw['exit_code'],0)
        self.assertEqual(raw['requested_signals'],[])
        self.assertFalse(any(call.args[1] in (signal.SIGTERM,signal.SIGKILL) for call in calls.call_args_list))

    def test_options_bounds_and_duplicates(self):
        for option in ('--compile-timeout-ms','--run-timeout-ms'):
            for value in ('0','-1','1.5','NaN','2147483648','١'):
                with self.assertRaises(runner.PreviewError):runner.parse_options([option,value])
            with self.assertRaises(runner.PreviewError):runner.parse_options([option,'5',option,'5'])
        self.assertEqual(runner.parse_options(['--run-timeout-ms','1']).run_ms,1)
    def test_cleanup_failure_stops_real_scheduler(self):
        root=self.root/'project';manifest(root);source(root,'tests/a_test.tk','wait');source(root,'tests/b_test.tk','exit')
        compiler=self.root/'tokac';fake_compiler(compiler)
        original=processes.Supervisor.gone
        def fault(instance,pid):
            gone=original(instance,pid)
            if getattr(instance,'_timeout_fault',False):raise PermissionError('injected exit confirmation failure')
            return gone
        originalrun=processes.Supervisor.run
        def run(instance,*args,**kwargs):
            instance._timeout_fault=args[3]=='run'
            return originalrun(instance,*args,**kwargs)
        with patch.object(processes.Supervisor,'gone',fault),patch.object(processes.Supervisor,'run',run),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(runner.PreviewError):runner.execute_preview(['--run-timeout-ms','100'],self.sdk,compiler,root)
        r=receipt(root);self.assertEqual(r['exit_code'],2);self.assertEqual([t['result'] for t in r['tests']],['infrastructure_error','not_run'])
        self.assertEqual(r['tests'][0]['run']['trigger'],'timeout')
    def test_preparation_timeout_uses_independent_budget(self):
        root=self.root/'project';manifest(root);source(root);compiler=self.root/'tokac';fake_compiler(compiler)
        # Controlled lock contention in the genuine context worker; no production fault flag.
        import fcntl
        (root/'.toka').mkdir();lock=(root/'.toka/test-context.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX)
        try:
            with patch.object(runner,'DEFAULT_PREPARE_MS',100),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(runner.PreviewError):runner.execute_preview([],self.sdk,compiler,root)
        finally:lock.close()
        r=receipt(root);self.assertEqual(r['exit_code'],2);self.assertEqual(r['tests'][0]['result'],'not_run')
        self.assertEqual(r['preparation']['context']['trigger'],'timeout');assert_confirmed(r['preparation']['context'])

    def test_lock_wait_uses_cli_override_and_stops_dispatch(self):
        import fcntl
        root=self.root/'project';manifest(root);source(root,'tests/a_test.tk','exit');source(root,'tests/b_test.tk','exit')
        compiler=self.root/'tokac';fake_compiler(compiler)
        (root/'.toka').mkdir();stream=(root/'.toka/test-context.lock').open('w');fcntl.flock(stream,fcntl.LOCK_EX)
        started=time.monotonic()
        try:
            child=subprocess.run([sys.executable,str(ROOT/'lib/toolchain/toka_test.py'),'--sdk-lib',str(self.sdk),'--tokac',str(compiler),'--','--compile-timeout-ms','2000'],cwd=root,capture_output=True,timeout=6)
        finally:stream.close()
        self.assertEqual(child.returncode,2,(child.stdout,child.stderr));self.assertLess(time.monotonic()-started,6)
        r=receipt(root);self.assertEqual(r['budgets_ms']['lock_wait'],2000)
        self.assertEqual([t['result'] for t in r['tests']],['not_run','not_run'])
        self.assertIn('dependency write lock',r['error']);assert_confirmed(r['preparation']['context'])
        self.assertFalse(list((root/'.toka/test-runs').glob('*/000001')))

    def finalization_injection(self, location, infrastructure=False):
        root=self.root/(location+str(infrastructure));manifest(root);source(root,content='exit')
        compiler=self.root/'tokac';fake_compiler(compiler)
        if infrastructure:compiler.unlink()
        original_print=print;original_write=runner.packages.atomic_write;injected=False
        def inject():
            nonlocal injected
            if not injected:injected=True;os.kill(os.getpid(),signal.SIGINT)
        def printer(*args,**kwargs):
            if location=='summary' and args and str(args[0]).startswith('Preview results:'):inject()
            return original_print(*args,**kwargs)
        def writer(path,data):
            if Path(path).name=='preview.json':
                if location=='persistence':inject()
                if location=='persistence-error':
                    inject()
                    if '"finalized": false' in data:raise OSError('injected staging persistence failure')
            return original_write(path,data)
        with patch('builtins.print',printer),patch.object(runner.packages,'atomic_write',writer),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            if infrastructure or location=='persistence-error':
                with self.assertRaises((runner.PreviewError,OSError)):runner.execute_preview([],self.sdk,compiler,root)
                code=2
            else:code=runner.execute_preview([],self.sdk,compiler,root)
        r=receipt(root);self.assertTrue(injected);self.assertTrue(r['finalized'])
        self.assertEqual(code,r['exit_code']);self.assertEqual(r['interrupt_signal'],signal.SIGINT)
        self.assertEqual(code,2 if infrastructure or location=='persistence-error' else 130)
        self.assertEqual(r['result'],'infrastructure_or_configuration_error' if code==2 else 'interrupted')
    def test_final_summary_interrupt_updates_cli_and_receipt(self):self.finalization_injection('summary')
    def test_staging_persistence_interrupt_updates_cli_and_receipt(self):self.finalization_injection('persistence')
    def test_finalization_interrupt_preserves_infrastructure_priority(self):self.finalization_injection('persistence',True)
    def test_persistence_failure_overrides_interrupt(self):self.finalization_injection('persistence-error')

    def test_pending_signal_at_commit_boundary_is_included(self):
        root=self.root/'pending';manifest(root)
        original=signal.pthread_sigmask;injected=False
        def mask(how,numbers):
            nonlocal injected
            previous=original(how,numbers)
            if how==signal.SIG_BLOCK and not injected:
                injected=True;os.kill(os.getpid(),signal.SIGTERM)
            return previous
        with patch.object(signal,'pthread_sigmask',mask),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            code=runner.execute_preview(['--allow-empty'],self.sdk,self.root/'unused',root)
        r=receipt(root);self.assertEqual(code,130);self.assertEqual(r['exit_code'],130)
        self.assertEqual(r['interrupt_signal'],signal.SIGTERM);self.assertTrue(r['finalized'])
    def test_post_commit_signal_does_not_change_frozen_outcome(self):
        root=self.root/'committed';manifest(root)
        original=runner.packages.atomic_write;injected=False
        def writer(path,data):
            nonlocal injected
            if Path(path).name=='preview.json' and '"finalized": true' in data and not injected:
                injected=True;os.kill(os.getpid(),signal.SIGINT)
            return original(path,data)
        with patch.object(runner.packages,'atomic_write',writer),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            code=runner.execute_preview(['--allow-empty'],self.sdk,self.root/'unused',root)
        r=receipt(root);self.assertTrue(injected);self.assertEqual(code,0);self.assertEqual(r['exit_code'],0)
        self.assertIsNone(r['interrupt_signal']);self.assertTrue(r['finalized'])

    def test_native_preparation_timeout_is_infrastructure(self):
        root=self.root/'project';manifest(root);source(root)
        dep=root/'dep';manifest(dep)
        (dep/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=(),native=(required=true,sources=("native/input.c")))\n')
        (dep/'native').mkdir();(dep/'native/input.c').write_text('int answer(void) { return 42; }\n')
        source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
        manifest(root,'dep="./dep",')
        runner.packages.Resolver(root/'package.tk',root/'package.lock',root/'.toka',offline=False,refresh=False).run()
        sdk=self.root/'sdk';(sdk/'toolchain').mkdir(parents=True);(sdk/'sys').mkdir();(sdk/'sys/toka_rt.o').write_bytes(b'controlled runtime fixture')
        shutil.copyfile(ROOT/'tools/scripts/toka_build.py',sdk/'toolchain/toka_build.py')
        for name in ('toka_package.py','toka_safe_extract.py'):
            shutil.copyfile(ROOT/'lib/toolchain'/name,sdk/'toolchain'/name)
        compiler=self.root/'tokac';fake_compiler(compiler)
        ready=self.root/'cc-ready';cc=self.root/'cc'
        cc.write_text('#!'+sys.executable+'\nimport pathlib,time\npathlib.Path('+repr(str(ready))+').write_text("READY")\ntime.sleep(60)\n');cc.chmod(0o755)
        with patch.dict(os.environ,{'CC':str(cc)}),patch.object(runner,'DEFAULT_NATIVE_MS',5000),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(runner.PreviewError):runner.execute_preview([],sdk,compiler,root)
        r=receipt(root)
        self.assertTrue(ready.exists(),json.dumps(r)+str([(str(p),p.read_text()) for p in (root/'.toka/test-runs').glob('*/native*.stderr')]))
        self.assertEqual(r['exit_code'],2);self.assertEqual(r['tests'][0]['result'],'not_run')
        p=r['preparation']['native'];self.assertEqual(p['trigger'],'timeout');assert_confirmed(p)

    def test_missing_supervision_module_returns_two(self):
        helper=self.root/'sdk-helper';helper.mkdir()
        for name in ('toka_test.py','toka_package.py','toka_safe_extract.py','toka_test_report.py'):
            shutil.copyfile(ROOT/'lib/toolchain'/name,helper/name)
        env=dict(os.environ);env.pop('PYTHONPATH',None)
        result=subprocess.run([sys.executable,str(helper/'toka_test.py'),'--sdk-lib',str(helper),'--tokac','unused','--','--help'],env=env,capture_output=True,timeout=10)
        self.assertEqual(result.returncode,2);self.assertIn(b'toka_test_process',result.stderr)

    def test_interrupt_cleanup_failure_overrides_130(self):
        class InterruptFault(processes.Supervisor):
            def exited(self,pid):
                ended=super().exited(pid)
                self._interrupt(signal.SIGINT,None)
                return ended
            def gone(self,pid):
                super().gone(pid)
                raise PermissionError('injected interrupt confirmation failure')
        p=self.phase('wait',1000,InterruptFault())
        self.assertEqual(p['trigger'],'interrupt');self.assertEqual(p['cleanup']['status'],'failed')
        self.assertEqual(p['interrupt_signal'],signal.SIGINT)
        with self.assertRaises(runner.PreviewError):runner.phase_error(p)
    def test_reaped_identity_never_receives_a_signal(self):
        child=subprocess.Popen([sys.executable,'-c','pass'],start_new_session=True);child.wait(timeout=10)
        with patch.object(os,'killpg') as kill:
            with self.assertRaises(ChildProcessError):processes.Supervisor().send(child.pid,signal.SIGTERM)
            kill.assert_not_called()
    def test_confirmed_exit_precedes_deadline(self):
        class LateObservation(processes.Supervisor):
            def exited(self,pid):
                # A test-only observation seam: deadline has passed at first observation.
                while not super().exited(pid):time.sleep(.01)
                return True
        p=self.phase('exit',1,LateObservation());assert_confirmed(p)
        self.assertIsNone(p['trigger']);self.assertEqual(p['exit_code'],0)
    def test_missing_tool_launch_error(self):
        directory=self.root/'logs';directory.mkdir()
        p=processes.Supervisor().run([str(self.root/'missing-tool')],self.root,directory,'compile',dict(os.environ),100)
        self.assertIn('launch_error',p)
        with self.assertRaises(runner.PreviewError):runner.phase_error(p)


def installed(sdk, output):
    output.mkdir(parents=True,exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='i2a-installed-') as temp:
        base=Path(temp);controlled=base/'controlled-sdk';(controlled/'bin').mkdir(parents=True)
        shutil.copyfile(sdk/'bin/toka',controlled/'bin/toka');(controlled/'bin/toka').chmod(0o755)
        (controlled/'lib').symlink_to(sdk/'lib',target_is_directory=True);fake_compiler(controlled/'bin/tokac')
        results=[]
        def case(name, modes, arguments=(), interrupt=None, stage='run', repeat=False, probe_wait=False, real=False, setup=None):
            root=base/name;manifest(root)
            for i,mode in enumerate(modes):source(root,'tests/%02d_test.tk'%i,mode)
            active=sdk if real else controlled
            env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
            env['PATH']=str(active/'bin')+os.pathsep+os.environ['PATH']
            if probe_wait:env['FIXTURE_PROBE_WAIT']='1'
            cleanup = setup(root,env) if setup else None
            command=[str(active/'bin/toka'),'test',*arguments]
            child=subprocess.Popen(command,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
            try:
                if interrupt:
                    deadline=time.monotonic()+20
                    while time.monotonic()<deadline:
                        paths=list((root/'.toka/test-runs').glob('*/'+('000001/'+stage+'.stdout' if stage in ('run','compile') else stage+'.stdout')))
                        handshake = b'Waiting for project dependency lock' if stage == 'context' else b'READY'
                        if any(handshake in p.read_bytes() for p in paths) or (root/'native-ready').exists():break
                        if child.poll() is not None:raise AssertionError('command exited before interrupt handshake')
                        time.sleep(.02)
                    else:raise AssertionError('interrupt handshake timed out')
                    os.kill(child.pid,interrupt)  # ONLY actual command PID, never its terminal group.
                    if repeat:time.sleep(.05);os.kill(child.pid,interrupt)
                stdout,stderr=child.communicate(timeout=20)
                if cleanup:cleanup()
            except BaseException:
                # Outer test guard owns cleanup of its CLI group, not production evidence.
                if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
                stdout,stderr=child.communicate(timeout=10)
                folder=output/(name+'-incomplete');folder.mkdir()
                (folder/'stdout').write_bytes(stdout);(folder/'stderr').write_bytes(stderr)
                if (root/'.toka/test-runs').exists():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
                raise
            r=receipt(root);folder=output/name;folder.mkdir();shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
            (folder/'stdout').write_bytes(stdout);(folder/'stderr').write_bytes(stderr)
            record={'name':name,'command':command,'exit_code':child.returncode,'reported_exit_code':r['exit_code'],
                    'manager_pid':child.pid,'compiler':'original_sdk' if real else 'controlled_fixture',
                    'interrupt_target':'manager_pid_only' if interrupt else None,'receipt':r}
            (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');results.append(record)
            assert child.returncode==r['exit_code'],record
            for t in r['tests']:
                for key in ('compile_link','run'):
                    if key in t and not t[key].get('launch_error'):assert_confirmed(t[key])
            for p in r.get('preparation',{}).values():
                if not p.get('launch_error'):assert_confirmed(p)
            return r
        for mode in ('wait','children','ignore'):
            r=case('timeout-'+mode,[mode,'exit'],['--run-timeout-ms','5000' if mode=='ignore' else '1000']);assert r['exit_code']==1
            assert [t['result'] for t in r['tests']]==['timed_out','passed']
        r=case('compile-timeout',['compile_wait','exit'],['--compile-timeout-ms','1000']);assert r['exit_code']==1
        assert [t['result'] for t in r['tests']]==['timed_out','passed']
        r=case('residual',['residual','exit']);assert r['exit_code']==1 and r['tests'][0]['run']['trigger']=='residual_process'
        for number in (signal.SIGINT,signal.SIGTERM):
            r=case('interrupt-'+str(number),['ignore','exit'],interrupt=number,repeat=True)
            assert r['exit_code']==130 and r['interrupt_count']>=2
            assert [t['result'] for t in r['tests']]==['interrupted','not_run']
            assert r['tests'][0]['run']['trigger']=='interrupt'
        r=case('interrupt-compile',['compile_wait','exit'],interrupt=signal.SIGINT,stage='compile');assert r['exit_code']==130
        r=case('interrupt-probe',['exit'],interrupt=signal.SIGINT,stage='probe',probe_wait=True);assert r['exit_code']==130
        def locked_context(root,env):
            import fcntl
            (root/'.toka').mkdir();stream=(root/'.toka/test-context.lock').open('w')
            fcntl.flock(stream,fcntl.LOCK_EX)
            return stream.close
        r=case('lock-wait-cli-override',['exit','exit'],['--compile-timeout-ms','2000'],setup=locked_context)
        assert r['exit_code']==2 and r['budgets_ms']['lock_wait']==2000
        assert all(t['result']=='not_run' for t in r['tests'])
        assert 'dependency write lock' in r['error']
        assert r['preparation']['context']['duration_ms']>=2000
        r=case('interrupt-context',['exit'],interrupt=signal.SIGINT,stage='context',setup=locked_context)
        assert r['exit_code']==130 and r['tests'][0]['result']=='not_run'
        def slow_native(root,env):
            dep=root/'native-dependency';manifest(dep)
            (dep/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=(),native=(required=true,sources=("native/input.c")))\n')
            (dep/'native').mkdir();(dep/'native/input.c').write_text('int answer(void) { return 42; }\n')
            source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
            manifest(root,'dep="./native-dependency",')
            runner.packages.Resolver(root/'package.tk',root/'package.lock',root/'.toka',offline=False,refresh=False).run()
            cc=root/'controlled-cc';cc.write_text('#!'+sys.executable+'\nimport pathlib,time\npathlib.Path('+repr(str(root/'native-ready'))+').write_text("READY")\ntime.sleep(60)\n');cc.chmod(0o755)
            env['CC']=str(cc)
        r=case('interrupt-native',['exit'],interrupt=signal.SIGINT,stage='native',setup=slow_native)
        assert r['exit_code']==130 and r['tests'][0]['result']=='not_run'
        assert r['preparation']['native']['trigger']=='interrupt'
        r=case('probe-timeout',['exit'],['--compile-timeout-ms','1000'],probe_wait=True);assert r['exit_code']==2 and r['tests'][0]['result']=='not_run'
        r=case('compiler-crash',['compile_signal','exit']);assert r['exit_code']==2 and r['tests'][1]['result']=='not_run'
        r=case('run-signal',['signal','exit']);assert r['exit_code']==1 and r['tests'][1]['result']=='passed'
        realwait='''import std/io::{println}
extern fn libc_usleep(usec: u32) -> i32
fn main() -> i32 {
 println("READY")
 loop { unsafe { libc_usleep(10000:u32) } }
 return 0
}
'''
        r=case('original-compiler-interrupt',[realwait,'fn main() -> i32 { return 0 }'],interrupt=signal.SIGINT,real=True)
        assert r['exit_code']==130 and r['tests'][1]['result']=='not_run'
        r=case('original-compiler-runtime-timeout',[realwait],['--run-timeout-ms','1000'],real=True);assert r['exit_code']==1
        summary={'result':'pass','preview':True,'stage':'I2-A','scenarios':len(results),
                 'stable_json':False,'original_sdk':str(sdk),'results':[{'name':x['name'],'exit_code':x['exit_code']} for x in results]}
        (output/'installed-result.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--sdk',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.sdk:
        installed(args.sdk.resolve(),args.output.resolve())
    else:unittest.main(argv=[sys.argv[0]],verbosity=2)
