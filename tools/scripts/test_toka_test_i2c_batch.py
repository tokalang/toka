#!/usr/bin/env python3
"""Standalone SDK-only result/report/shared-state acceptance; no repository imports."""
import argparse,contextlib,hashlib,http.server,importlib,json,os,re,selectors,shutil,signal,subprocess,sys,tarfile,tempfile,threading,time
from pathlib import Path

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def manifest(root,deps=''):
    root.mkdir(parents=True,exist_ok=True);(root/'package.tk').write_text('pub const PACKAGE=(name="batch",version="1.0.0",dependencies=(%s))\n'%deps)
def source(root,name,content):
    path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content);return path

def check(report):
    assert report['schema']=='toka.test-report' and report['version']==1 and report['finalized'] and report['preview']
    assert report['identity']['status'] in ('not_checked','complete','failed')
    total=report['summary']['total'];assert total is None or total==len(report['tests'])==sum(report['summary'][k] for k in report['summary'] if k!='total')
    for test in report['tests']:
        for phase in test['phases'].values():
            if phase['state']=='not_started':assert all(phase[k] is None for k in ('duration_ms','process','exit_code','signal','os_error'))
    return report

class Batch:
    def __init__(self,sdk,output):
        self.sdk=sdk;self.output=output;output.mkdir(parents=True,exist_ok=False)
        self.work=output/'consumer-projects';self.work.mkdir();self.results=[]
        self.env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
        self.env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
        sys.path.insert(0,str(sdk/'lib/toolchain'))
        self.runner=importlib.import_module('toka_test');self.packages=importlib.import_module('toka_package')
        assert Path(self.runner.__file__).resolve().is_relative_to(sdk.resolve())
    def project(self,name,entries):
        root=self.work/name;manifest(root)
        for file,body in entries:source(root,'tests/'+file,body)
        return root
    def save(self,name,root,command,code,out,err,report,layer='installed CLI',rows=()):
        folder=self.output/name;folder.mkdir();(folder/'stdout').write_bytes(out);(folder/'stderr').write_bytes(err)
        if report:
            check(report);assert code==report['exit_code']
            if report['artifact_root']:
                persisted=Path(report['artifact_root'])/'report.json';assert json.loads(persisted.read_text())==report
        if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
        record={'name':name,'rows':list(rows),'evidence_layer':layer,'command':command,'exit_code':code,'report':report}
        (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');self.results.append(record)
        return report
    def cli(self,name,root,args=(),expected=0,rows=(),env=None,active=None):
        command=[str((active or self.sdk)/'bin/toka'),'test','--json',*args]
        r=subprocess.run(command,cwd=root,env=env or self.env,capture_output=True,timeout=45)
        text=r.stdout.decode();report,end=json.JSONDecoder().raw_decode(text);assert not text[end:].strip()
        self.save(name,root,command,r.returncode,r.stdout,r.stderr,report,rows=rows)
        assert r.returncode==expected,report
        return report
    def helper_fault(self,name,root,install,expected=2,rows=()):
        out,err=__import__('io').StringIO(),__import__('io').StringIO()
        with install(),contextlib.redirect_stdout(out),contextlib.redirect_stderr(err):
            code=self.runner.execute_preview(['--json'],self.sdk/'lib',self.sdk/'bin/tokac',root)
        report=json.loads(out.getvalue());self.save(name,root,['installed SDK helper','--json'],code,out.getvalue().encode(),err.getvalue().encode(),report,
            layer='installed SDK in-process fault injection with genuine compiler/runtime',rows=rows)
        assert code==expected,report
        return report
    def run(self):
        ok='fn main() -> i32 { return 0 }\n'
        # Genuine platform linker rejects a missing external symbol; later test runs.
        root=self.project('link-failure',[('a_test.tk','extern fn absent_link_symbol_i2c() -> i32\nfn main() -> i32 { return absent_link_symbol_i2c() }\n'),('b_test.tk',ok)])
        r=self.cli('R03',root,expected=1,rows=['R03'])
        assert [t['result'] for t in r['tests']]==['compile_failed','passed'] and r['tests'][0]['phases']['run']['state']=='not_started'
        assert r['tests'][0]['phases']['compile_link']['exit_code']!=0
        root=self.project('run-seven',[('a_test.tk','fn main() -> i32 { return 7 }\n'),('b_test.tk',ok)])
        r=self.cli('R04',root,expected=1,rows=['R04'])
        assert [t['result'] for t in r['tests']]==['run_failed','passed'] and r['tests'][0]['phases']['run']['exit_code']==7 and r['tests'][0]['phases']['run']['signal'] is None
        root=self.project('compiler-unavailable',[('a_test.tk',ok),('b_test.tk',ok)])
        # Actual exec failure for an installed compiler selection, without changing frozen SDK bytes.
        variant=self.output/'compiler-variant';(variant/'bin').mkdir(parents=True);(variant/'lib').symlink_to(self.sdk/'lib',target_is_directory=True)
        shutil.copyfile(self.sdk/'bin/toka',variant/'bin/toka');(variant/'bin/toka').chmod(0o755)
        (variant/'bin/tokac').write_text('not executable');(variant/'bin/tokac').chmod(0o600)
        command=[str(variant/'bin/toka'),'test','--json'];result=subprocess.run(command,cwd=root,env=self.env,capture_output=True,timeout=20)
        r=self.save('R08-compiler',root,command,result.returncode,result.stdout,result.stderr,json.loads(result.stdout),rows=['R08'])
        assert r['exit_code']==2 and r['result']=='infrastructure_error' and r['summary']['not_run']==2 and r['preparation']['probe']['phase']['os_error'] in (13,1)
        (self.output/'R08-compiler/linker-profile.json').write_text(json.dumps({'external_linker_start_variant':'requires_R08_driver_evidence' if sys.platform.startswith('linux') else 'not_applicable','basis':'Linux launches cc; macOS uses in-process LLD','actual_link_failure':'R03 genuine linker error retained'},indent=2)+'\n')
        from unittest.mock import patch
        root=self.project('executable-unavailable',[('a_test.tk',ok),('b_test.tk',ok)])
        original=self.runner.Supervisor.run
        @contextlib.contextmanager
        def exec_fault():
            def wrapped(instance,*args,**kwargs):
                if args[3]=='run':Path(args[0][0]).chmod(0o600)
                return original(instance,*args,**kwargs)
            with patch.object(self.runner.Supervisor,'run',wrapped):yield
        r=self.helper_fault('R09',root,exec_fault,rows=['R09']);assert [t['result'] for t in r['tests']]==['infrastructure_error','not_run']
        assert r['tests'][0]['phases']['run']['exit_code'] is None and r['tests'][0]['phases']['run']['os_error'] in (13,1)
        root=self.project('entry-disappeared',[('a_test.tk',ok),('b_test.tk',ok)])
        original_prepare=self.runner.run_preparation
        @contextlib.contextmanager
        def entry_fault():
            def wrapped(*args,**kwargs):
                result=original_prepare(*args,**kwargs)
                if args[1]=='native':(root/'tests/a_test.tk').unlink()
                return result
            with patch.object(self.runner,'run_preparation',wrapped):yield
        r=self.helper_fault('R10',root,entry_fault,rows=['R10']);assert [t['result'] for t in r['tests']]==['infrastructure_error','not_run']
        assert r['tests'][0]['phases']['compile_link']['state']=='not_started'
        variants=[['--unknown'],['--filter'],['--filter',''],['--run-timeout-ms'],['--run-timeout-ms','5','--run-timeout-ms','5']]
        for index,args in enumerate(variants):
            root=self.project('config-'+str(index),[]);r=self.cli('R11-'+str(index),root,args,2,['R11']);assert r['result']=='configuration_error' and r['summary']['total'] is None and r['tests']==[] and r['errors']
        root=self.work/'non-project';root.mkdir();r=self.cli('R11-non-project',root,expected=2,rows=['R11']);assert r['project_root'] is None and r['summary']['total'] is None
        root=self.project('failure-priority',[('a_test.tk','fn main() -> i32 { return not_declared }\n'),('b_test.tk',ok),('c_test.tk',ok),('d_test.tk',ok)])
        r=self.helper_fault('R13-J03',root,exec_fault,rows=['R13','J03'])
        assert [t['result'] for t in r['tests']]==['compile_failed','infrastructure_error','not_run','not_run']
        assert r['summary']=={'total':4,'passed':0,'failed':1,'infrastructure_error':1,'interrupted':0,'not_run':2}
        # Actual compiler warning volume plus actual runtime output. Successful JSON stays alone.
        warning_lines='\n'.join(' auto variable%d# = %d:i32'%(i,i) for i in range(350))
        body='import std/io::{println}\nfn main() -> i32 {\n'+warning_lines+'\n auto i# = 0:i32\n loop i < 10000 {\n println("0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz")\n i += 1\n }\n return 0\n}\n'
        root=self.project('large-output',[('a_test.tk',body)])
        r=self.cli('J01',root,rows=['J01']);test=r['tests'][0]
        expected=(b'0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz\n')*10000
        assert Path(test['logs']['run_stdout']).read_bytes()==expected
        assert test['live_output']['run']['status']=='complete'
        proxy=self.output/'diagnostic-volume-proxy';(proxy/'bin').mkdir(parents=True);(proxy/'lib').symlink_to(self.sdk/'lib',target_is_directory=True)
        shutil.copyfile(self.sdk/'bin/toka',proxy/'bin/toka');(proxy/'bin/toka').chmod(0o755)
        wrapper='#!'+sys.executable+'\nimport json,pathlib,subprocess,sys\noriginal='+repr(str(self.sdk/'bin/tokac'))+'\nargs=sys.argv[1:]\nr=subprocess.run([original,*args],capture_output=True)\nif args==["--version"]:sys.stdout.buffer.write(r.stdout);sys.stderr.buffer.write(r.stderr);sys.exit(r.returncode)\ndata=json.loads(r.stdout)\nentry=next(a for a in args if a.endswith(".tk") and "=" not in a)\ndata["diagnostics"] += [{"code":None,"message":"controlled compiler-channel volume "+("x"*100),"severity":"note","primary":{"file":entry}} for i in range(4000)]\nsys.stdout.write(json.dumps(data))\nsys.stderr.buffer.write(r.stderr)\nsys.exit(r.returncode)\n'
        (proxy/'bin/tokac').write_text(wrapper);(proxy/'bin/tokac').chmod(0o755)
        root=self.project('compiler-channel-volume',[('a_test.tk',ok)])
        r=self.cli('J01-compiler-volume',root,active=proxy,rows=['J01'])
        assert Path(r['tests'][0]['logs']['compile_stdout']).stat().st_size>500000
        assert r['tests'][0]['live_output']['compile_link']['status']=='complete'
        (self.output/'J01-compiler-volume/proxy-definition.json').write_text(json.dumps({'controlled_proxy':True,'actual_compilation':'delegates unmodified installed tokac','added_output':'synthetic note volume in the designated compiler channel','not_claimed_to_be_original_compiler_diagnostics':True,'wrapper_sha256':sha(proxy/'bin/tokac')},indent=2)+'\n')
        # A closed delivery channel is incomplete, never accept the persisted pass as delivered pass.
        root=self.project('closed-stdout',[('a_test.tk',ok)])
        command=[str(self.sdk/'bin/toka'),'test','--json'];child=subprocess.Popen(command,cwd=root,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        child.stdout.close();stderr=child.stderr.read();code=child.wait(timeout=20)
        folder=self.output/'J09';folder.mkdir();(folder/'stderr').write_bytes(stderr)
        if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
        record={'name':'J09','rows':['J09'],'command':command,'exit_code':code,'incomplete':True,'accepted_as_completed_pass':False,'report':None}
        (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');self.results.append(record);assert code!=0
        # Simultaneous calls use independent run IDs/directories; entries read stdin EOF and only expected env.
        env_body='import std/env\nimport std/io::{println}\nextern fn libc_getchar() -> i32\nfn main() -> i32 {\n unsafe { if libc_getchar() != -1 { return 1 } }\n println("{}",env::var(string::from("TOKA_TEST_RUN_DIR")).unwrap())\n println("{}",env::var(string::from("TOKA_TEST_CASE_DIR")).unwrap())\n return 0\n}\n'
        root=self.project('stdin-env',[('a_test.tk',env_body)])
        marker='I2C_SECRET_MUST_NOT_APPEAR_IN_REPORT';env=dict(self.env,I2C_ACCEPTANCE_SECRET=marker)
        r=self.cli('A05',root,env=env,rows=['A05']);assert marker not in json.dumps(r)
        actual=Path(r['tests'][0]['logs']['run_stdout']).read_text().splitlines();assert actual==[r['artifact_root'],str(Path(r['tests'][0]['logs']['run_stdout']).parent)]
        root=self.project('basename',[('one/same_test.tk',ok),('two/same_test.tk',ok)])
        r=self.cli('A02',root,rows=['A02']);assert [t['id'] for t in r['tests']]==['tests/one/same_test.tk','tests/two/same_test.tk']
        assert len({t['logs']['run_stdout'] for t in r['tests']})==2 and r['summary']['passed']==2
        root=self.project('concurrent',[('a_test.tk',ok)])
        command=[str(self.sdk/'bin/toka'),'test','--json'];children=[subprocess.Popen(command,cwd=root,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE) for _ in range(2)]
        reports=[]
        for index,child in enumerate(children):
            out,err=child.communicate(timeout=30);r=json.loads(out);self.save('A01-'+str(index),root,command,child.returncode,out,err,r,rows=['A01']);assert r['exit_code']==0;reports.append(r)
        assert reports[0]['run_id']!=reports[1]['run_id'] and reports[0]['artifact_root']!=reports[1]['artifact_root']
        self.shared_state(ok)
        self.boundary_faults(ok)
        (self.output/'result.json').write_text(json.dumps({'result':'pass','records':[{'name':r['name'],'rows':r['rows'],'exit_code':r['exit_code']} for r in self.results]},indent=2)+'\n')
    def shared_state(self,ok):
        package=self.output/'registry-source';manifest(package);source(package,'lib/official/reg.tk','pub fn answer() -> i32 { return 42 }\n')
        tar=self.output/'reg.tar.gz'
        with tarfile.open(tar,'w:gz') as stream:
            for path in sorted(package.rglob('*')):
                if path.is_file():stream.add(path,arcname=path.relative_to(package).as_posix())
        archive_hash=sha(tar);content_hash=self.packages.tree_sha256(package)
        gate=threading.Event();requested=threading.Event();first=[True];request_log=[]
        class Server(http.server.BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(handler):
                request_log.append({'path':handler.path,'monotonic_ns':time.monotonic_ns()})
                if handler.path=='/catalog.json':
                    payload=json.dumps({'packages':[{'name':'reg','version':'1.0.0','latest_version':'1.0.0','installable':True,'versions':[{'version':'1.0.0','tarball_url':'http://127.0.0.1:'+str(server.server_port)+'/archive','sha256':archive_hash}]}]}).encode()
                elif handler.path=='/archive':
                    if first[0]:first[0]=False;requested.set();assert gate.wait(15)
                    payload=tar.read_bytes()
                else:handler.send_error(404);return
                try:handler.send_response(200);handler.end_headers();handler.wfile.write(payload)
                except (BrokenPipeError,ConnectionResetError):request_log.append({'client_closed':True})
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            for row,interrupt in [('P13',False),('P14b',True)]:
                gate.clear();requested.clear();first[0]=True
                root=self.project('shared-'+row,[('a_test.tk','import official/reg::{answer}\nfn main() -> i32 { if answer() != 42 { return 1 } return 0 }\n')])
                manifest(root,'reg="reg:1.0.0",');entry=self.packages.LockEntry('reg','registry','reg','1.0.0',archive_hash,content_hash,[])
                (root/'package.lock').write_text(self.packages.encode_lock({'reg':entry}));before=(root/'package.lock').read_bytes()
                env=dict(self.env,TOKA_REGISTRY_URL='http://127.0.0.1:'+str(server.server_port))
                command=[str(self.sdk/'bin/toka'),'test','--json'];one=subprocess.Popen(command,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                assert requested.wait(10),'first writer did not reach archive request'
                two=subprocess.Popen(command,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                end=time.monotonic()+10
                while time.monotonic()<end:
                    if any(b'Waiting for project dependency lock' in p.read_bytes() for p in (root/'.toka/test-runs').glob('*/context.stdout')):break
                    time.sleep(.02)
                else:raise AssertionError('second writer did not demonstrate lock contention')
                assert not (root/'.toka/packages/reg-1.0.0').exists(),'half-written target published'
                if interrupt:os.kill(one.pid,signal.SIGINT)
                gate.set()
                output=[]
                for index,child in enumerate((one,two)):
                    out,err=child.communicate(timeout=30);r=json.loads(out);self.save(row+'-'+str(index),root,command,child.returncode,out,err,r,rows=[row]);output.append(r)
                assert output[0]['exit_code']==(130 if interrupt else 0) and output[1]['exit_code']==0
                assert (root/'package.lock').read_bytes()==before
                assert self.packages.tree_sha256(root/'.toka/packages/reg-1.0.0')==content_hash
                (self.output/(row+'-witness.json')).write_text(json.dumps({'lock_contended':True,'half_written_target_visible':False,'lock_unchanged':True,'cache_content_sha256':content_hash,'results':[r['exit_code'] for r in output],'requests':request_log},indent=2)+'\n')
        finally:gate.set();server.shutdown();server.server_close();thread.join(timeout=5)
    def boundary_faults(self,ok):
        from unittest.mock import patch
        root=self.project('interrupt-cleanup-failed',[('a_test.tk','fn main() -> i32 { return missing_name }\n'),('b_test.tk','import std/io::{println}\nextern fn libc_usleep(n:u32) -> i32\nfn main() -> i32 { println("READY")\n loop { unsafe { libc_usleep(10000:u32) } }\n return 0 }\n'),('c_test.tk',ok)])
        original_run=self.runner.Supervisor.run;original_exited=self.runner.Supervisor.exited;original_gone=self.runner.Supervisor.gone
        @contextlib.contextmanager
        def fault():
            def run(instance,*args,**kwargs):instance.active_run=args[3]=='run';instance.active_folder=Path(args[2]);return original_run(instance,*args,**kwargs)
            def exited(instance,pid):
                ended=original_exited(instance,pid)
                if getattr(instance,'active_run',False) and (instance.active_folder/'run.stdout').exists() and b'READY' in (instance.active_folder/'run.stdout').read_bytes():instance._interrupt(signal.SIGINT,None)
                return ended
            def gone(instance,pid):
                result=original_gone(instance,pid)
                if getattr(instance,'active_run',False):raise PermissionError(1,'controlled exit confirmation failure')
                return result
            with patch.object(self.runner.Supervisor,'run',run),patch.object(self.runner.Supervisor,'exited',exited),patch.object(self.runner.Supervisor,'gone',gone):yield
        r=self.helper_fault('T11e',root,fault,rows=['T11e']);assert [t['result'] for t in r['tests']]==['compile_failed','infrastructure_error','not_run']
        assert r['termination']['signal']==2 and r['tests'][1]['cleanup']['status']=='failed'
        root=self.project('managed-start-refused',[('a_test.tk',ok)])
        @contextlib.contextmanager
        def startup():
            original=self.runner.Supervisor.__module__;module=importlib.import_module(original)
            def denied(*args,**kwargs):raise PermissionError(1,'controlled managed start refusal')
            with patch.object(module.subprocess,'Popen',denied):yield
        r=self.helper_fault('T12-posix',root,startup,rows=['T12']);assert r['exit_code']==2 and r['summary']['not_run']==1
        assert r['preparation']['probe']['phase']['process'] is None
        (self.output/'T12-posix/platform-scope.json').write_text(json.dumps({'POSIX_start_refusal':'covered','Windows_managed_backend':'unsupported/not_applicable_to_core_SDK','no_unmanaged_entry_launched':True},indent=2)+'\n')

def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source-unavailable',action='store_true');a=p.parse_args();sdk=a.sdk.resolve()
    if a.source_unavailable:
        cwd=Path.cwd();assert not (cwd/'.git').exists() and not (cwd/'src/main.cpp').exists()
        forbidden=list(cwd.rglob('CMakeLists.txt'))+list(cwd.rglob('src/main.cpp'))
        assert not forbidden,forbidden
    batch=Batch(sdk,a.output.resolve());batch.run()
    (a.output/'source-visibility.json').write_text(json.dumps({'repository_checkout_present':False if a.source_unavailable else 'not asserted','harness_imports':'stdlib plus installed SDK helpers only','sdk_helper_module':str(batch.runner.__file__),'compiler_source_visible':False if a.source_unavailable else 'not asserted','SDK_standard_library_sources':'part of frozen SDK, explicitly allowed'},indent=2)+'\n')

if __name__=='__main__':main()
