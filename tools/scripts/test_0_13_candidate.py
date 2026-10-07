#!/usr/bin/env python3
"""Installed standard SDK controls. No source compiler or private evidence inputs."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys,time
from pathlib import Path

SCRIPTS=('test_toka_test_lock_codes_cli.py','toka_test_lock_contract.py','test_package_entry.py','test_package_entry_p2.py','test_package_entry_relative.py','test_d1_d2_diagnostics.py')

def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--revision',required=True);p.add_argument('--version',default='v0.13.0');p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir();sdk=a.sdk.resolve();root=Path(__file__).resolve().parents[2]
    if not re.fullmatch(r'[0-9a-f]{40}',a.revision) or not re.fullmatch(r'v0\.13\.(0|[1-9][0-9]*)',a.version):raise ValueError('invalid candidate identity')
    identity=json.loads((sdk/'sdk.json').read_text())
    if identity.get('schema')!='toka.sdk-identity' or identity.get('version')!=1 or identity.get('version_label')!=a.version or identity.get('candidate_revision')!=a.revision or identity.get('source_dirty') is not False or identity.get('build_testing') is not False:raise ValueError('SDK metadata does not bind a standard frozen candidate')
    env=dict(os.environ,PATH=str(sdk/'bin')+':/opt/homebrew/bin:/usr/bin:/bin',PYTHONDONTWRITEBYTECODE='1');env.pop('TOKA_LIB',None);env.pop('TOKAC',None)
    receipts=[]
    def run(name,argv,cwd):
        d=out/name;d.mkdir();t=time.monotonic_ns()
        try:r=subprocess.run(list(map(str,argv)),cwd=cwd,env=env,capture_output=True,timeout=600)
        except (OSError,subprocess.TimeoutExpired) as error:
            record={'argv':list(map(str,argv)),'cwd':str(cwd),'exit_code':None,'termination':'not_started' if isinstance(error,OSError) else 'protection_timeout','error':str(error)}
            for key in ('stdout','stderr'):
                raw=getattr(error,key,None);record[key+'_available']=raw is not None
                if raw is not None:(d/key).write_bytes(raw)
            (d/'receipt.json').write_text(json.dumps(record,indent=2));raise
        (d/'stdout').write_bytes(r.stdout);(d/'stderr').write_bytes(r.stderr);record={'argv':list(map(str,argv)),'cwd':str(cwd),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'execution_ms':(time.monotonic_ns()-t)/1e6};(d/'receipt.json').write_text(json.dumps(record,indent=2));receipts.append(record)
        if r.returncode:raise RuntimeError(name+' failed; inspect '+str(d))
        return r
    versions={}
    for name in ('tokac','toka','tokafmt','tokalsp'):
        binary=sdk/'bin'/name
        if hashlib.sha256(binary.read_bytes()).hexdigest()!=identity['tools'][name]['sha256']:raise ValueError('binary identity changed: '+name)
        r=run('version-'+name,[binary,'--version'],out);text=(r.stdout+r.stderr).decode()
        if not re.search(r'(?<![0-9.])'+re.escape(a.version[1:])+r'(?![0-9.])',text):raise ValueError('version mismatch: '+name)
        versions[name]=text
    # Freeze a standalone script/fixture bundle with mandatory sender/receiver identities.
    bundle=out/'bundle';(bundle/'tools/scripts').mkdir(parents=True);(bundle/'tests/tooling').mkdir(parents=True)
    for name in SCRIPTS:shutil.copyfile(root/'tools/scripts'/name,bundle/'tools/scripts'/name)
    shutil.copytree(root/'tests/tooling/release_013',bundle/'tests/tooling/release_013')
    hashes={str(f.relative_to(bundle)):hashlib.sha256(f.read_bytes()).hexdigest() for f in bundle.rglob('*') if f.is_file()};(bundle/'files-sha256.json').write_text(json.dumps(hashes,indent=2))
    for name in SCRIPTS:
        relative='tools/scripts/'+name
        if relative not in hashes or hashlib.sha256((bundle/relative).read_bytes()).hexdigest()!=hashes[relative]:raise ValueError('mandatory script identity missing: '+relative)
    scripts=bundle/'tools/scripts';fixture=bundle/'tests/tooling/release_013'
    run('A1',[sys.executable,scripts/'test_toka_test_lock_codes_cli.py','--sdk',sdk,'--output',out/'a1'],out)
    run('B1',[sys.executable,scripts/'test_package_entry.py','--overlay',sdk,'--output',out/'b1'],out)
    run('B1-boundaries',[sys.executable,scripts/'test_package_entry_p2.py','--overlay',sdk,'--fixtures',fixture,'--output',out/'b1-boundaries'],out)
    # Bind a generated same-name fixture without private absolute evidence paths.
    project=out/'b1/valid/digest-cli';manifest=project/'package.tk';manifest.write_text(manifest.read_text().replace('../directory-digest',str(project.parent/'directory-digest')))
    run('B1-relative',[sys.executable,scripts/'test_package_entry_relative.py','--overlay',sdk,'--fixture',project,'--output',out/'b1-relative'],out)
    run('D1-D2',[sys.executable,scripts/'test_d1_d2_diagnostics.py','--tokac',sdk/'bin/tokac','--sdk',sdk,'--fixtures',fixture,'--output',out/'d1-d2'],out)
    report={'schema':'toka.0.13-candidate-controls','version':1,'result':'pass','candidate_revision':a.revision,'version_label':a.version,'build_testing':False,'four_tool_versions':versions,'groups':['A1','B1','B1-boundaries','B1-relative','D1-D2'],'SDK_root':str(sdk),'receipts':receipts}
    (out/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))

if __name__=='__main__':main()
