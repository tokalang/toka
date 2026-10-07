#!/usr/bin/env python3
"""B1 review boundary controls; local loopback registry, no remote writes."""
import argparse,copy,hashlib,http.server,json,os,shutil,subprocess,sys,threading,time
from pathlib import Path
from test_package_entry import validate

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--overlay',type=Path,required=True);parser.add_argument('--review','--fixtures',dest='review',type=Path,default=Path(__file__).resolve().parents[2]/'tests/tooling/release_013');parser.add_argument('--output',type=Path,required=True);a=parser.parse_args();out=a.output.resolve();out.mkdir();sdk=a.overlay.resolve();review=a.review.resolve()
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'),TOKA_OFFLINE='1',PYTHONDONTWRITEBYTECODE='1',PATH=str(sdk/'bin')+':/opt/homebrew/bin:/usr/bin:/bin')
    commands=[]
    def run(name,argv,cwd,environment=env):
        d=out/name;d.mkdir();argv=list(map(str,argv));start=time.monotonic_ns()
        try:r=subprocess.run(argv,cwd=cwd,env=environment,capture_output=True,timeout=90)
        except (OSError,subprocess.TimeoutExpired) as error:
            data={'argv':argv,'cwd':str(cwd),'exit_code':None,'actual_termination':'not_started' if isinstance(error,OSError) else 'protection_timeout','error':str(error)}
            for key in ('stdout','stderr'):
                raw=getattr(error,key,None);data[key+'_available']=raw is not None
                if raw is not None:(d/key).write_bytes(raw)
            (d/'receipt.json').write_text(json.dumps(data,indent=2));raise
        end=time.monotonic_ns();(d/'stdout').write_bytes(r.stdout);(d/'stderr').write_bytes(r.stderr);(d/'receipt.json').write_text(json.dumps({'argv':argv,'cwd':str(cwd),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'phase':'CLI','start_monotonic_ns':start,'result_monotonic_ns':end,'execution_ms':(end-start)/1e6},indent=2));commands.append(name);return r,d
    def expect(r,d,status,contract):
        if r.returncode!=status:
            (d/'finding.json').write_text(json.dumps({'contract':contract,'expected_exit':status,'actual_exit':r.returncode,'phase':'CLI','logs':str(d),'next_check':'Check same invocation stdout/stderr and entry selection or locked cache request trace.'},indent=2));raise RuntimeError('contract failed: '+contract+'; inspect '+str(d))
    # Fresh source fixture: only src/selected and tests/a_test are roots.
    workspace=out/'workspace';workspace.mkdir()
    for args in (['new','dep','--lib'],['new','app']):
        r,d=run('create-'+args[1],[sdk/'bin/toka',*args],workspace);expect(r,d,0,'fixture creation')
    dep=workspace/'dep';app=workspace/'app';(dep/'lib/dep/mod.tk').write_text('pub fn answer()->i32 { return 42 }\n')
    r,d=run('add',[sdk/'bin/toka','add','../dep','--alias','dep'],app);expect(r,d,0,'same-name lock')
    good='import dep::{answer}\nfn main()->i32 { if answer()!=42 { return 1 } return 0 }\n'
    (app/'src/selected.tk').write_text(good);(app/'src/main.tk').write_text('import dep/mod::{answer}\nfn main()->i32 { return 0 }')
    (app/'lib').mkdir();(app/'lib/not_imported.tk').write_text('import dep/mod::{answer}')
    (app/'tests').mkdir();(app/'tests/a_test.tk').write_text(good)
    (app/'build.tk').write_text('import build::{Executable, run_build}\nfn main()->i32 { auto app#=Executable::make(c"app",c"src/selected.tk")\n return run_build(app) }')
    lock=(app/'package.lock').read_bytes()
    for name,args in [('selected-check',['check','src/selected.tk','--json']),('selected-test',['test','--json','tests/a_test.tk']),('custom-build',['build'])]:
        r,d=run(name,[sdk/'bin/toka',*args],app);expect(r,d,0,'unreachable files must not change selected program');assert (app/'package.lock').read_bytes()==lock
    # A reachable local module with the same spelling must still be rejected.
    (app/'lib/not_imported.tk').write_text('import dep/mod::{answer}\npub fn forwarded()->i32 { return answer() }')
    bad='import lib/not_imported::{forwarded}\nfn main()->i32 { return forwarded() }'
    (app/'src/selected.tk').write_text(bad);(app/'tests/a_test.tk').write_text(bad)
    for name,args,status in [('reachable-check',['check','src/selected.tk','--json'],1),('reachable-test',['test','--json','tests/a_test.tk'],2),('reachable-build',['build'],1)]:
        r,d=run(name,[sdk/'bin/toka',*args],app);expect(r,d,status,'reachable wrong import must be rejected');assert (app/'package.lock').read_bytes()==lock
        if name.endswith('test'):
            v=validate(json.loads(r.stdout),2,'package.import_invalid');assert v['errors'][0]['package_entry']['source']['file'].endswith('/lib/not_imported.tk')
    # Reuse immutable review archive/lock bytes; supply a live loopback URL.
    archive=(review/'locked-cache-fixtures/registry/1.0.0.tar.gz').read_bytes();requests=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            requests.append(self.path)
            if self.path=='/catalog.json':
                body=json.dumps({'packages':[{'name':'reg','version':'2.0.0','latest_version':'2.0.0','installable':True,'versions':[{'version':'1.0.0','tarball_url':url+'/1.0.0.tar.gz','sha256':hashlib.sha256(archive).hexdigest()},{'version':'2.0.0','tarball_url':url+'/2.0.0.tar.gz','sha256':'0'*64}]}]}).encode()
            elif self.path=='/1.0.0.tar.gz':body=archive
            else:self.send_error(404);return
            self.send_response(200);self.end_headers();self.wfile.write(body)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);url='http://127.0.0.1:'+str(server.server_port);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();cache_results=[]
    try:
        for action in ('check','test'):
            root=out/('cache-'+action);shutil.copytree(review/'locked-cache-fixtures/consumer-projects/candidate-test',root,ignore=shutil.ignore_patterns('.toka','target'));before=(root/'package.lock').read_bytes();args=[action]+(['tests/a_test.tk','--json'] if action=='check' else ['--json'])
            offline=dict(env,TOKA_REGISTRY_URL=url);start=len(requests);r,d=run('offline-missing-'+action,[sdk/'bin/toka',*args],root,offline);expect(r,d,1 if action=='check' else 2,'offline missing cache fails without fetching');assert len(requests)==start and (root/'package.lock').read_bytes()==before
            online=dict(offline);online.pop('TOKA_OFFLINE');start=len(requests);r,d=run('online-locked-'+action,[sdk/'bin/toka',*args],root,online);expect(r,d,0,'non-offline locked cache fill succeeds');new=requests[start:];assert '/1.0.0.tar.gz' in new and '/2.0.0.tar.gz' not in new;assert (root/'package.lock').read_bytes()==before
            start=len(requests);r,d=run('offline-cached-'+action,[sdk/'bin/toka',*args],root,offline);expect(r,d,0,'offline installed locked content succeeds');assert len(requests)==start and (root/'package.lock').read_bytes()==before
            cache_results.append({'command':action,'locked_version':'1.0.0','catalog_latest':'2.0.0','download_requests':new,'lock_unchanged':True})
    finally:server.shutdown();thread.join();server.server_close();(out/'registry-requests.json').write_text(json.dumps(requests,indent=2))
    # Independent mutations of a real entry failure, including null/unknown compatibility.
    base=json.loads((out/'reachable-test/stdout').read_text());negative=[]
    mutations=[('version-bool',lambda v:v.update(version=True)),('finalized-string',lambda v:v.update(finalized='false')),('finalized-false',lambda v:v.update(finalized=False)),('exit-bool',lambda v:v.update(exit_code=True))]
    for phase in ('compile_link','compile','link','run'):
        mutations.append((phase+'-completed',lambda v,p=phase:v['tests'][0]['phases'][p].update(state='completed')))
        for field,value in [('duration_ms',0),('exit_code',0),('signal',15),('os_error',1),('process',{'leader_pid':1})]:
            mutations.append((phase+'-'+field,lambda v,p=phase,f=field,x=value:v['tests'][0]['phases'][p].update({f:x})))
    for name,mutate in mutations:
        value=copy.deepcopy(base);mutate(value)
        try:validate(value,2,'package.import_invalid')
        except (ValueError,KeyError):negative.append(name)
        else:raise RuntimeError('contradiction accepted: '+name)
    for code in (None,'future.entry.code'):
        v=copy.deepcopy(base);v['errors'][0]['code']=code;validate(v,2)
        v['tests'][0]['phases']['run']['state']='completed'
        try:validate(v,2)
        except ValueError:negative.append('unknown/null-execution-'+str(code))
        else:raise RuntimeError('legacy compatibility bypassed execution consistency')
    (out/'result.json').write_text(json.dumps({'result':'pass','CLI_commands':len(commands),'reachable_unreachable_pairs':3,'cache':cache_results,'report_negatives':negative,'D1_D2_modified':False},indent=2));print('P2 pass:',len(commands),'commands;',len(negative),'report counterexamples')

if __name__=='__main__':main()
