#!/usr/bin/env python3
"""Relative/rooted same-file B1 controls; does not reopen R2/R3."""
import argparse,json,os,shutil,subprocess,time
from pathlib import Path
from test_package_entry import validate

def main():
    p=argparse.ArgumentParser();p.add_argument('--overlay',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir();sdk=a.overlay.resolve()
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'),TOKA_OFFLINE='1',PYTHONDONTWRITEBYTECODE='1',PATH=str(sdk/'bin')+':/opt/homebrew/bin:/usr/bin:/bin');records=[]
    project=out/'project';shutil.copytree(a.fixture,project,ignore=shutil.ignore_patterns('.toka','target','.toka_build_exe'))
    bridge=project/'src/bridge.tk';lock=(project/'package.lock').read_bytes()
    # Unselected bad source must remain outside traversal for every spelling.
    (project/'lib').mkdir(exist_ok=True);(project/'lib/unselected.tk').write_text('import directory-digest/mod::{answer}')
    good='import directory-digest::{answer}\npub fn forwarded()->i32 { return answer() }\n'
    bad=good.replace('directory-digest::{','directory-digest/mod::{')
    body='fn main()->i32 { if forwarded()!=42 { return 1 } return 0 }\n'
    for state,text in [('valid',good),('invalid',bad)]:
        bridge.write_text(text)
        for label,request in [('dot','./bridge'),('parent','../src/bridge'),('rooted','src/bridge')]:
            (project/'src/main.tk').write_text('import '+request+'::{forwarded}\n'+body)
            (project/'tests/a_test.tk').write_text('import ../src/bridge::{forwarded}\n'+body)
            for action,args in [('check',['check','src/main.tk','--json']),('build',['build']),('test',['test','--json','src/main.tk'])]:
                command=[str(sdk/'bin/toka'),*args];name=state+'-'+label+'-'+action;folder=out/name;folder.mkdir();t=time.monotonic_ns()
                try:r=subprocess.run(command,cwd=project,env=env,capture_output=True,timeout=90)
                except (OSError,subprocess.TimeoutExpired) as error:
                    record={'argv':command,'cwd':str(project),'exit_code':None,'actual_termination':'not_started' if isinstance(error,OSError) else 'protection_timeout','message':str(error)}
                    for stream in ('stdout','stderr'):
                        raw=getattr(error,stream,None);record[stream+'_available']=raw is not None
                        if raw is not None:(folder/stream).write_bytes(raw)
                    (folder/'receipt.json').write_text(json.dumps(record,indent=2));raise
                end=time.monotonic_ns();(folder/'stdout').write_bytes(r.stdout);(folder/'stderr').write_bytes(r.stderr)
                expected=0 if state=='valid' else 2 if action=='test' else 1
                record={'argv':command,'cwd':str(project),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'expected_exit_code':expected,'phase':'CLI','start_monotonic_ns':t,'result_monotonic_ns':end,'source':{'src/main.tk':(project/'src/main.tk').read_text(),'src/bridge.tk':text},'lock_unchanged':(project/'package.lock').read_bytes()==lock}
                (folder/'receipt.json').write_text(json.dumps(record,indent=2));records.append(record)
                if r.returncode!=expected or not record['lock_unchanged']:
                    (folder/'finding.json').write_text(json.dumps({'contract':'same reachable bridge gets same entry classification for relative/rooted callers','expected_exit':expected,'actual_exit':r.returncode,'logs':str(folder),'next_check':'Inspect import_requests and source-file-relative target resolution.'},indent=2));raise RuntimeError('see '+str(folder))
                if state=='invalid':
                    if action=='test':report=validate(json.loads(r.stdout),2,'package.import_invalid');facts=report['errors'][0]['package_entry']
                    elif action=='check':report=json.loads(r.stdout);assert report['schema']=='toka.resolve-report' and report['errors'][0]['code']=='package.import_invalid';facts=report['errors'][0]['details']['package_entry']
                    else:
                        output=(r.stdout+r.stderr).decode();assert str(bridge) not in output or 'directory-digest/mod' in output
                        assert 'directory-digest/mod' in output and 'E0901' not in output;continue
                    assert facts['requested_entry']=='directory-digest/mod' and facts['source']=={'file':str(bridge),'line':1}
            # Ordinary tests-dir caller uses ../src/bridge and selects only this entry.
        args=[str(sdk/'bin/toka'),'test','--json','tests/a_test.tk'];folder=out/(state+'-tests-parent');folder.mkdir();t=time.monotonic_ns();r=subprocess.run(args,cwd=project,env=env,capture_output=True,timeout=90);end=time.monotonic_ns();(folder/'stdout').write_bytes(r.stdout);(folder/'stderr').write_bytes(r.stderr);expected=0 if state=='valid' else 2
        record={'argv':args,'cwd':str(project),'exit_code':r.returncode,'expected_exit_code':expected,'start_monotonic_ns':t,'result_monotonic_ns':end,'lock_unchanged':(project/'package.lock').read_bytes()==lock};(folder/'receipt.json').write_text(json.dumps(record,indent=2));records.append(record);assert r.returncode==expected and record['lock_unchanged']
        if state=='invalid':validate(json.loads(r.stdout),2,'package.import_invalid')
    (out/'result.json').write_text(json.dumps({'result':'pass','CLI_commands':len(records),'same_bridge_forms':['./bridge','../src/bridge','src/bridge'],'positive_negative':True,'unselected_file_ignored':True,'lock_unchanged':True},indent=2));print('relative same-file controls pass:',len(records),'commands')

if __name__=='__main__':main()
