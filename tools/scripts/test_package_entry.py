#!/usr/bin/env python3
"""B1 local CLI controls. Fixed expectations are independent of producers."""
import argparse, copy, datetime, hashlib, json, os, subprocess, time
from pathlib import Path

CODES={'renamed':'package.entry_alias_mismatch','missing':'package.entry_missing','wrong-import':'package.import_invalid'}

def validate(report, status, code=None):
    if report.get('schema')!='toka.test-report' or type(report.get('version')) is not int or report['version']!=1:
        raise ValueError('unsupported C6 protocol')
    for key in ('finalized','result','exit_code','tests','errors','termination'):
        if key not in report: raise ValueError('missing C6 field '+key)
    if type(status) is not int or type(report['exit_code']) is not int:
        raise ValueError('exit status must be integer')
    if type(report['finalized']) is not bool or report['finalized'] is not True:
        raise ValueError('completion flag must be boolean true')
    if type(report['result']) is not str:
        raise ValueError('result must be a string')
    expected_status={'passed':0,'empty':0,'failed':1,'configuration_error':2,'infrastructure_error':2,'interrupted':130}.get(report['result'])
    if expected_status is None or expected_status!=status:
        raise ValueError('result contradicts process status')
    if not report['finalized'] or report['exit_code']!=status:
        raise ValueError('unfinalized or contradictory receipt')
    for test in report['tests']:
        if test['result']!='not_run':continue
        for name in ('compile_link','compile','link','run'):
            phase=test['phases'][name]
            if phase['state']!='not_started':raise ValueError('not_run phase was started: '+name)
            for field in ('duration_ms','exit_code','signal','os_error','process'):
                if phase[field] is not None:raise ValueError('not_run contains execution facts: '+field)
    if code:
        if status!=2 or report['result']!='configuration_error' or report['termination']['phase']!='context':
            raise ValueError('entry failure is not context configuration_error/2')
        if not report['tests'] or any(t['result']!='not_run' for t in report['tests']):
            raise ValueError('entry failure launched selected tests')
        error=report['errors'][0]
        if error['code']!=code or error['category']!='configuration_error' or error['phase']!='context':
            raise ValueError('wrong entry code or phase')
        facts=error['package_entry']
        for key in ('alias','requested_entry','expected_entry','suggestion','source'):
            if key not in facts: raise ValueError('missing entry fact '+key)
        if not facts['alias'] or not facts['expected_entry'] or not facts['suggestion']:
            raise ValueError('empty entry facts')
    return report


def main():
    a=argparse.ArgumentParser();a.add_argument('--overlay',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    out=args.output.resolve();out.mkdir();sdk=args.overlay.resolve()
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'),TOKAC=str(sdk/'bin/tokac'),TOKA_OFFLINE='1',PYTHONDONTWRITEBYTECODE='1',PATH=str(sdk/'bin')+':/opt/homebrew/bin:/usr/bin:/bin')
    results=[]
    def run(name,argv,cwd):
        folder=out/name;folder.mkdir();start=time.monotonic_ns();wall=datetime.datetime.now(datetime.timezone.utc).isoformat()
        r=subprocess.run(list(map(str,argv)),cwd=cwd,env=env,capture_output=True,timeout=90)
        end=time.monotonic_ns();(folder/'stdout').write_bytes(r.stdout);(folder/'stderr').write_bytes(r.stderr)
        receipt={'argv':list(map(str,argv)),'cwd':str(cwd),'exit_code':r.returncode if r.returncode>=0 else None,'signal':-r.returncode if r.returncode<0 else None,'started':wall,'start_monotonic_ns':start,'result_monotonic_ns':end,'execution_ms':(end-start)/1e6}
        (folder/'receipt.json').write_text(json.dumps(receipt,indent=2));results.append(receipt)
        return r,folder
    for kind in ('valid','renamed','missing','wrong-import'):
        root=out/kind;root.mkdir()
        r,_=run(kind+'-new-lib',[sdk/'bin/toka','new','directory-digest','--lib'],root);assert r.returncode==0
        r,_=run(kind+'-new-cli',[sdk/'bin/toka','new','digest-cli'],root);assert r.returncode==0
        lib=root/'directory-digest';cli=root/'digest-cli';entry=lib/'lib/directory-digest/mod.tk'
        entry.write_text('pub fn answer()->i32 { return 42 }\n')
        if kind=='missing':entry.unlink()
        alias='dir_digest' if kind=='renamed' else 'directory-digest'
        request=alias+'/mod' if kind=='wrong-import' else alias
        body='import '+request+'::{answer}\nfn main()->i32 { if answer()!=42 { return 1 } return 0 }\n'
        (cli/'src/main.tk').write_text(body);(cli/'tests').mkdir(exist_ok=True);(cli/'tests/a_test.tk').write_text(body)
        r,_=run(kind+'-add',[sdk/'bin/toka','add','../directory-digest','--alias',alias],cli);assert r.returncode==0
        before=(cli/'package.lock').read_bytes()
        for command in ('check','build','test'):
            argv=[sdk/'bin/toka',command]+(['src/main.tk'] if command=='check' else ['--json'] if command=='test' else [])
            r,folder=run(kind+'-'+command,argv,cli)
            expected=0 if kind=='valid' else 2 if command=='test' else 1
            assert r.returncode==expected,(kind,command,r.returncode,folder)
            assert (cli/'package.lock').read_bytes()==before
            if command=='test':
                report=validate(json.loads(r.stdout),r.returncode,CODES.get(kind))
                if kind=='valid':
                    assert report['result']=='passed';locked_nodes=report['dependencies']['nodes']
                else:
                    facts=report['errors'][0]['package_entry'];assert facts['alias']==alias and facts['requested_entry']==request
                    assert facts['expected_entry'].endswith('/lib/'+alias+'/mod.tk')
                    (out/(kind+'-fixed-report.json')).write_text(json.dumps(report,indent=2))
            elif kind!='valid':
                output=(r.stdout+r.stderr).decode();assert alias in output and request in output and '/lib/'+alias+'/mod.tk' in output
        if kind=='valid':
            for command in ('check','build','test'):
                r,_=run('locked-repeat-'+command,[sdk/'bin/toka',command]+(['src/main.tk'] if command=='check' else ['--json'] if command=='test' else []),cli)
                assert r.returncode==0;assert (cli/'package.lock').read_bytes()==before
                if command=='test':assert json.loads(r.stdout)['dependencies']['nodes']==locked_nodes
            r,_=run('library-program',[cli/'target/debug/digest-cli'],cli);assert r.returncode==0
    base=json.loads((out/'wrong-import-fixed-report.json').read_text());validate(base,2,CODES['wrong-import'])
    negatives=[]
    for case in ('code','phase','exit','version','field','fact','launched','ordinary-error','code-zero','code-one','result'):
        r=copy.deepcopy(base)
        if case=='result':r['result']='passed'
        elif case=='code':r['errors'][0]['code']=None
        elif case=='phase':r['termination']['phase']='compile_link'
        elif case=='exit':r['exit_code']=1
        elif case=='version':r['version']=99
        elif case=='field':del r['finalized']
        elif case=='fact':del r['errors'][0]['package_entry']['expected_entry']
        elif case=='launched':r['tests'][0]['result']='compile_failed'
        elif case=='ordinary-error':r['errors'][0].pop('package_entry')
        elif case in ('code-zero','code-one'):r['exit_code']=0 if case=='code-zero' else 1
        try:validate(r,2,CODES['wrong-import'])
        except (ValueError,KeyError):negatives.append(case)
        else:raise AssertionError('negative accepted: '+case)
    # Legacy/unknown codes remain ordinary failures, never inferred entry success.
    for code in (None,'future.entry.code'):
        r=copy.deepcopy(base);r['errors'][0]['code']=code;validate(r,2)
        assert r['result']=='configuration_error' and r['errors'][0]['code']==code
    r=copy.deepcopy(base);r['errors'][0]['message']='Different readable wording';validate(r,2,CODES['wrong-import'])
    (out/'result.json').write_text(json.dumps({'result':'pass','CLI_commands':len(results),'negative_controls':negatives,'legacy_unknown_preserved':True,'message_independent':True,'SDK_qualification':False},indent=2))
    print('B1 local controls pass:',len(results),'commands;',len(negatives),'negative reports')

if __name__=='__main__':main()
