#!/usr/bin/env python3
"""C6 reports, trusted provenance and real installed-CLI boundaries."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import selectors
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'lib/toolchain'))
import toka_test as runner
import toka_test_report as reports
from test_toka_test_i1 import manifest, source
from test_toka_test_i2a import fake_compiler


def validate(report):
    required=set(reports.new_report())-{'preview'}
    assert required<=set(report),required-set(report)
    assert report['schema']=='toka.test-report' and report['version']==1 and report['finalized'] is True
    assert report['preview'] is True
    counts=report['summary'];assert set(counts)=={'total','passed','failed','infrastructure_error','interrupted','not_run'}
    assert counts['total'] is None or counts['total']==len(report['tests'])==sum(counts[key] for key in counts if key!='total')
    for timing in report['timings'].values():check_phase(timing)
    for test in report['tests']:
        assert set(test['phases'])=={'compile_link','compile','link','run'}
        for phase in test['phases'].values():check_phase(phase)
        for path in test['logs'].values():
            if path is not None:assert Path(path).is_absolute() and Path(path).is_file(),path
    for diag in report['diagnostics']:
        assert diag['source']['origin'] in ('user','dependency','sdk','unknown')
        if diag['source']['origin']=='unknown':assert diag['source']['classification_basis']=='none' and diag['source']['package_node_id'] is None
    return report


def check_phase(value):
    assert value['state'] in ('completed','aborted','not_started')
    if value['state']=='not_started':
        assert all(value[key] is None for key in ('duration_ms','process','exit_code','signal','os_error'))
    else:assert isinstance(value['duration_ms'],(int,float)) and value['duration_ms']>=0


@unittest.skipUnless(os.name=='posix','managed reports are native POSIX only')
class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='i2b-control-');self.base=Path(self.temp.name)
        self.root=self.base/'project';manifest(self.root)
        self.sdk=self.base/'sdk/lib';(self.sdk/'sys').mkdir(parents=True);(self.sdk/'sys/toka_rt.o').write_bytes(b'controlled runtime fixture')
        self.compiler=self.base/'tokac';fake_compiler(self.compiler)
    def tearDown(self):self.temp.cleanup()
    def invoke(self,args=(),compiler=None):
        out,err=io.StringIO(),io.StringIO()
        with contextlib.redirect_stdout(out),contextlib.redirect_stderr(err):
            code=runner.execute_preview([*args,'--json'],self.sdk,compiler or self.compiler,self.root)
        text=out.getvalue();decoder=json.JSONDecoder();data,end=decoder.raw_decode(text)
        self.assertEqual(text[end:].strip(),'');self.assertEqual(code,data['exit_code']);validate(data)
        if data['artifact_root']:
            path=Path(data['artifact_root'])/'report.json'
            if path.is_file():self.assertEqual(json.loads(path.read_text()),data)
        return data,err.getvalue()
    def test_pass_fixed_phases_raw_status_and_timings(self):
        source(self.root,content='exit');r,err=self.invoke()
        self.assertEqual(r['exit_code'],0);self.assertEqual(r['summary']['passed'],1)
        t=r['tests'][0];self.assertEqual(t['compile_mode'],'combined');self.assertEqual(t['phases']['run']['exit_code'],0)
        self.assertIn('Preview',err);self.assertGreaterEqual(r['timings']['total']['duration_ms'],r['timings']['execution']['duration_ms'])
    def test_compile_failure_and_not_started_run(self):
        source(self.root,content='compile_signal');r,err=self.invoke()
        self.assertEqual(r['exit_code'],2);self.assertEqual(r['tests'][0]['phases']['compile_link']['signal'],6)
        self.assertEqual(r['tests'][0]['phases']['run']['state'],'not_started')
    def test_runtime_signal_is_failed_not_interrupt(self):
        source(self.root,content='signal');source(self.root,'tests/z_test.tk','exit');r,err=self.invoke()
        self.assertEqual(r['exit_code'],1);self.assertEqual(r['summary']['failed'],1);self.assertEqual(r['summary']['passed'],1)
        self.assertEqual(r['tests'][0]['phases']['run']['signal'],6);self.assertIsNone(r['tests'][0]['interrupt_signal'])
    def test_timeout_has_raw_signal_and_separate_cleanup_time(self):
        source(self.root,content='ignore');r,err=self.invoke(['--run-timeout-ms','1000'])
        t=r['tests'][0];self.assertEqual(r['exit_code'],1);self.assertEqual(t['trigger'],'timeout')
        self.assertEqual(t['phases']['run']['signal'],signal.SIGKILL)
        self.assertGreaterEqual(t['cleanup']['duration_ms'],1900);self.assertLess(t['phases']['run']['duration_ms'],2000)
    def test_launch_failure_preserves_not_run_and_native_errno(self):
        source(self.root,content='exit');self.compiler.unlink();r,err=self.invoke()
        self.assertEqual(r['exit_code'],2);self.assertEqual(r['summary']['not_run'],1)
        self.assertEqual(r['preparation']['probe']['phase']['os_error'],2)
    def test_configuration_error_single_json_and_unknown_total(self):
        for args in (['--bogus'],['--json'],['--run-timeout-ms','0'],['missing.tk']):
            r,err=self.invoke(args);self.assertEqual(r['exit_code'],2)
            self.assertIsNone(r['summary']['total']);self.assertTrue(r['errors'])
    def test_empty_is_empty_not_passed(self):
        r,err=self.invoke(['--allow-empty']);self.assertEqual(r['result'],'empty');self.assertEqual(r['summary']['total'],0);self.assertEqual(r['summary']['passed'],0)
    def test_artifact_creation_failure_still_delivers_memory_json(self):
        target=self.root/'.toka';target.write_text('cannot create a directory here')
        r,err=self.invoke();self.assertEqual(r['exit_code'],2);self.assertIsNone(r['artifact_root']);self.assertTrue(r['errors'])
    def test_report_write_failure_changes_result_to_two(self):
        source(self.root,content='exit');original=runner.packages.atomic_write
        def write(path,data):
            if Path(path).name=='report.json':raise OSError(28,'controlled report storage full')
            return original(path,data)
        with patch.object(runner.packages,'atomic_write',write):r,err=self.invoke()
        self.assertEqual(r['exit_code'],2);self.assertEqual(r['result'],'infrastructure_error');self.assertTrue(r['errors'])
    def test_raw_binary_ansi_and_unicode_logs_do_not_pollute_json(self):
        source(self.root,content='exit')
        script=self.compiler.read_text().replace("exe.write_text(data);exe.chmod(0o755)","exe.write_text('#!'+sys.executable+'\\nimport os\\nos.write(1,b\"\\\\x1b[31mraw\\\\xff\\\\xe4\\\\xb8\\\\xad\")\\nos.write(2,b\"error[E0000] plain text\")\\n');exe.chmod(0o755)")
        self.compiler.write_text(script)
        r,err=self.invoke();self.assertEqual(r['exit_code'],0)
        logs=r['tests'][0]['logs'];self.assertEqual(Path(logs['run_stdout']).read_bytes(),b'\x1b[31mraw\xff\xe4\xb8\xad')
        self.assertTrue(any(d['severity']=='unknown' for d in r['diagnostics']))
    def test_live_backpressure_is_bounded_and_cleanup_still_confirmed(self):
        import toka_test_process as processes
        folder=self.base/'pressure';folder.mkdir()
        read,write=os.pipe()
        try:
            os.set_blocking(write,False)
            while True:
                try:os.write(write,b'x'*65536)
                except BlockingIOError:break
            class Destination:
                @property
                def buffer(self):return self
                def fileno(self):return write
            with patch.object(processes.sys,'stderr',Destination()):
                raw=processes.Supervisor().run([sys.executable,'-c','import os,time;os.write(1,b"READY");time.sleep(60)'],self.root,folder,'run',dict(os.environ),1000)
            self.assertLess(raw['duration_ms'],4000);self.assertTrue(raw['cleanup']['group_gone'])
            self.assertTrue(raw.get('supervision_error'))
        finally:os.close(read);os.close(write)

    def test_invalid_utf8_path_is_recoverable_configuration_error(self):
        import base64
        path=os.fsdecode(os.fsencode(self.root)+b'/tests/invalid\xff_test.tk')
        if sys.platform.startswith('linux'):
            Path(path).parent.mkdir();Path(path).write_text('exit')
        r,err=self.invoke([path]);self.assertEqual(r['exit_code'],2)
        self.assertIn(os.fsencode(path),[base64.b64decode(value) for value in r['raw_inputs_base64']])

    def test_json_finalization_includes_precommit_interrupt(self):
        source(self.root,content='exit');original=runner.packages.atomic_write;injected=False
        def writer(path,data):
            nonlocal injected
            if Path(path).name=='preview.json' and not injected:
                injected=True;os.kill(os.getpid(),signal.SIGINT)
            return original(path,data)
        with patch.object(runner.packages,'atomic_write',writer):r,err=self.invoke()
        self.assertEqual(r['exit_code'],130);self.assertEqual(r['termination']['signal'],signal.SIGINT)
        self.assertEqual(r['summary']['passed'],1)

    def test_json_mode_respects_option_values_and_entry_separator(self):
        self.assertFalse(runner.json_mode(['--filter','--json']))
        self.assertFalse(runner.json_mode(['--','--json']))
        self.assertTrue(runner.json_mode(['--bad','--json']))
        self.assertTrue(runner.json_mode(['--filter','--json','--json']))

    def graph(self):
        dep=self.root/'vendored';dep.mkdir();source(dep,'lib.tk','x');(self.sdk/'core').mkdir();source(self.sdk,'core/a.tk','x')
        source(self.root,'src/a.tk','x');external=self.base/'external.tk';external.write_text('x')
        return {'workspace_root':str(self.root),'workspace_node':'workspace-test',
                'dependencies':[{'root':str(dep),'node':'locked-test'}]},dep,external
    def test_provenance_uses_graph_before_workspace_prefix(self):
        graph,dep,external=self.graph()
        for path,origin,basis,node in [(self.root/'src/a.tk','user','workspace_node','workspace-test'),(dep/'lib.tk','dependency','locked_package_node','locked-test'),(self.sdk/'core/a.tk','sdk','sdk_root',None),(external,'unknown','none',None)]:
            value=reports.source_origin(str(path),graph,str(self.sdk.parent));self.assertEqual((value['origin'],value['classification_basis'],value['package_node_id']),(origin,basis,node))
    def test_conflicting_nodes_missing_and_nested_paths_are_unknown(self):
        graph,dep,external=self.graph();graph['dependencies'].append({'root':str(dep),'node':'different-node'})
        self.assertEqual(reports.source_origin(str(dep/'lib.tk'),graph,str(self.sdk.parent))['origin'],'unknown')
        for name in (None,str(self.root/'missing.tk')):self.assertEqual(reports.source_origin(name,graph,str(self.sdk.parent))['origin'],'unknown')
        nested=self.root/'nested';manifest(nested);source(nested,'a.tk','x')
        self.assertEqual(reports.source_origin(str(nested/'a.tk'),graph,str(self.sdk.parent))['origin'],'unknown')
    def test_plain_stderr_is_unknown_even_when_it_looks_like_a_code(self):
        out=self.base/'stdout';out.write_text('');err=self.base/'stderr';err.write_text('error[E0402] sdk/lib/user misleading text')
        rows=reports.diagnostics({'stdout':str(out),'stderr':str(err)},'run',None,None)
        self.assertEqual(rows[0]['severity'],'unknown');self.assertIsNone(rows[0]['code']);self.assertEqual(rows[0]['source']['origin'],'unknown')
    def test_structured_warning_note_and_unknown_severity(self):
        graph,dep,external=self.graph();out=self.base/'stdout';err=self.base/'stderr';err.write_text('')
        out.write_text(json.dumps({'schema':'toka.diagnostics','version':2,'diagnostics':[{'message':'m','code':'W0001','severity':severity,'primary':{'file':str(dep/'lib.tk')}} for severity in ('warning','note','arbitrary')]}))
        rows=reports.diagnostics({'stdout':str(out),'stderr':str(err)},'compile_link',graph,str(self.sdk.parent))
        self.assertEqual([x['severity'] for x in rows],['warning','note','unknown']);self.assertTrue(all(x['source']['origin']=='dependency' for x in rows))


def installed(sdk,output):
    output.mkdir(parents=True,exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='i2b-installed-') as temp:
        base=Path(temp);results=[]
        def run(name,content='fn main() -> i32 { return 0 }\n',args=(),active=None,env_extra=None,interrupt=False,setup=None):
            root=base/name;manifest(root);source(root,content=content)
            active=active or sdk
            if setup:setup(root)
            env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
            env['PATH']=str(active/'bin')+os.pathsep+os.environ['PATH'];env.update(env_extra or {})
            command=[str(active/'bin/toka'),'test',*args,'--json']
            child=subprocess.Popen(command,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
            observed=b''
            if interrupt:
                with selectors.DefaultSelector() as selector:
                    selector.register(child.stderr,selectors.EVENT_READ)
                    end=time.monotonic()+20
                    while b'READY' not in observed and time.monotonic()<end:
                        for key,event in selector.select(.1):observed+=os.read(key.fileobj.fileno(),65536)
                        if child.poll() is not None:break
                if b'READY' not in observed:
                    if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
                    stdout,stderr=child.communicate(timeout=10)
                    raise AssertionError('live output did not precede command completion: '+repr(observed+stderr))
                assert child.poll() is None
                os.kill(child.pid,signal.SIGINT)
            stdout,stderr=child.communicate(timeout=30);stderr=observed+stderr
            decoder=json.JSONDecoder();report,index=decoder.raw_decode(stdout.decode('utf-8'));assert not stdout.decode('utf-8')[index:].strip()
            validate(report);assert child.returncode==report['exit_code']
            if report['artifact_root']:
                assert json.loads((Path(report['artifact_root'])/'report.json').read_text())==report
            folder=output/name;folder.mkdir();(folder/'stdout').write_bytes(stdout);(folder/'stderr').write_bytes(stderr)
            if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
            record={'name':name,'command':command,'exit_code':child.returncode,'report':report,'live_handshake_observed':interrupt}
            (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');results.append(record)
            return report
        r=run('success');assert r['exit_code']==0 and r['identity']['sdk_revision'] is None
        r=run('compile-failure','fn main() -> i32 { return undeclared_name }\n');assert r['exit_code']==1
        assert any(d['code']=='E0402' and d['source']['origin']=='user' for d in r['diagnostics'])
        r=run('run-nonzero','fn main() -> i32 { return 7 }\n');assert r['tests'][0]['phases']['run']['exit_code']==7
        wait='import std/io::{println}\nextern fn libc_usleep(usec: u32) -> i32\nfn main() -> i32 {\n println("READY")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0\n}\n'
        r=run('live-interruption',wait,interrupt=True);assert r['exit_code']==130
        r=run('runtime-timeout',wait,args=['--run-timeout-ms','1000']);assert r['exit_code']==1
        r=run('configuration-error',args=['--unknown']);assert r['exit_code']==2 and r['summary']['total'] is None
        r=run('filter-no-match',args=['--filter','missing']);assert r['exit_code']==2 and r['summary']['total']==0
        r=run('allow-empty',args=['--filter','missing','--allow-empty']);assert r['result']=='empty'
        def dependency(root):
            dep=root/'dep';manifest(dep);source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return invalid_dependency_name }\n')
            manifest(root,'dep="./dep",');runner.packages.Resolver(root/'package.tk',root/'package.lock',root/'.toka',offline=False,refresh=False).run()
        r=run('dependency-source','import official/dep::{answer}\nfn main() -> i32 { return answer() }\n',setup=dependency)
        assert r['exit_code']==1 and any(d['source']['origin']=='dependency' and d['source']['package_node_id'] for d in r['diagnostics'])
        # Isolated installed SDK variants leave the staged candidate and original archives intact.
        def variant(name,missing):
            copy=base/name;shutil.copytree(sdk,copy)
            for path in missing:(copy/path).unlink()
            return copy
        absent=variant('no-runner-sdk',['lib/toolchain/toka_test.py'])
        r=run('missing-runner',active=absent);assert r['exit_code']==2 and r['identity']['status']=='not_checked'
        absent=variant('no-report-sdk',['lib/toolchain/toka_test_report.py'])
        r=run('missing-report-helper',active=absent);assert r['exit_code']==2
        # PATH intentionally has no Python; invocation uses absolute compiled manager path.
        empty=base/'empty-path';empty.mkdir()
        r=run('missing-python',env_extra={'PATH':str(empty)});assert r['exit_code']==2 and r['reason']=='python_unavailable'
        r=run('中文-outer-error',env_extra={'PATH':str(empty)});assert r['exit_code']==2 and '中文' in r['project_root']
        r=run('missing-python-existing-state',env_extra={'PATH':str(empty)},setup=lambda root:(root/'.toka').mkdir());assert r['exit_code']==2
        (output/'installed-result.json').write_text(json.dumps({'result':'pass','preview':True,'stage':'I2-B','scenarios':len(results),'formal_json':True,'stable_v1':False,'results':[{'name':x['name'],'exit_code':x['exit_code']} for x in results]},indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--sdk',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.sdk:installed(args.sdk.resolve(),args.output.resolve())
    else:unittest.main(argv=[sys.argv[0]],verbosity=2)
