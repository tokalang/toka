import argparse,json,os,shutil,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--overlay',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir();sdk=a.overlay.resolve()
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'),TOKAC=str(sdk/'bin/tokac'),TOKA_OFFLINE='1',PYTHONDONTWRITEBYTECODE='1',PATH=str(sdk/'bin')+':/opt/homebrew/bin:/usr/bin:/bin')
    records=[]
    def run(name,args,root,env_override=None):
        folder=out/name;folder.mkdir();cmd=[str(sdk/'bin/toka'),*args]
        if name=='probe-missing':cmd[0]=str(out/'isolated-bin/toka')
        start=time.monotonic_ns();r=subprocess.run(cmd,cwd=root,env=env_override or env,capture_output=True,timeout=90);end=time.monotonic_ns()
        (folder/'stdout').write_bytes(r.stdout);(folder/'stderr').write_bytes(r.stderr);(folder/'receipt.json').write_text(json.dumps({'argv':cmd,'cwd':str(root),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'phase':'CLI','start_monotonic_ns':start,'result_monotonic_ns':end,'execution_ms':(end-start)/1e6},indent=2));records.append(name);return r,folder
    for kind in ('valid-json','wrong-json','semantic','probe-missing'):
        cli=out/(kind+'-project');shutil.copytree(a.base,cli,ignore=shutil.ignore_patterns('.toka','target','.toka_build_exe'))
        manifest=cli/'package.tk';manifest.write_text(manifest.read_text().replace('../directory-digest',str(a.base.resolve().parent/'directory-digest')))
        before=(cli/'package.lock').read_bytes()
        if kind=='valid-json':
            r,d=run(kind,['check','src/main.tk','--json'],cli);v=json.loads(r.stdout);assert r.returncode==0 and v['schema']=='toka.diagnostics'
        if kind=='wrong-json':
            (cli/'src/main.tk').write_text('import directory-digest/mod::{answer}\nfn main()->i32 { return 0 }')
            r,d=run(kind,['check','src/main.tk','--json'],cli);v=json.loads(r.stdout);assert r.returncode==1 and v['schema']=='toka.resolve-report' and v['version']==1 and v['result']=='failed' and v['exit_code']==1
            assert v['errors'][0]['code']=='package.import_invalid' and v['errors'][0]['details']['package_entry']['requested_entry']=='directory-digest/mod'
        if kind=='semantic':
            (cli/'tests/a_test.tk').write_text('fn main()->i32 { return missing_identifier }')
            r,d=run(kind,['test','--json'],cli);v=json.loads(r.stdout);assert r.returncode==1 and v['result']=='failed' and v['tests'][0]['result']=='compile_failed' and not v['errors']
            assert v['tests'][0]['phases']['run']['state']=='not_started'
        if kind=='probe-missing':
            (out/'isolated-bin').mkdir();shutil.copy2(sdk/'bin/toka',out/'isolated-bin/toka')
            altered=dict(env,PATH='/opt/homebrew/bin:/usr/bin:/bin')
            r,d=run(kind,['test','--json'],cli,altered);v=json.loads(r.stdout);assert r.returncode==2 and v['result']=='infrastructure_error'
            assert all(e['code'] not in ('package.entry_missing','package.entry_alias_mismatch','package.import_invalid') for e in v['errors'])
        assert (cli/'package.lock').read_bytes()==before
    (out/'result.json').write_text(json.dumps({'result':'pass','commands':records},indent=2));print('CLI JSON/semantic/tool fault edge controls pass',len(records))

if __name__=='__main__':main()
