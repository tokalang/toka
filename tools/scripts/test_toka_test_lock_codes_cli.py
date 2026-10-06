"""Local A1 real-CLI checks with lossless receipts; no remote workflow or measurement."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys

from toka_test_lock_contract import validate, require_lock_failure, ReportContractError

EXPECTED = {'missing':'test.lock_missing','malformed':'test.lock_invalid','stale':'test.lock_mismatch'}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--legacy',action='store_true')
    args=parser.parse_args();sdk=args.sdk.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
    env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
    work=out/'projects';work.mkdir();dep=work/'dep';(dep/'lib/official').mkdir(parents=True)
    (dep/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=())\n')
    (dep/'lib/official/dep.tk').write_text('pub fn value()->i32 { return 42 }\n')
    records=[]

    def command(name,argv,root,environment=env):
        folder=out/name;folder.mkdir();before=(root/'package.lock').read_bytes() if (root/'package.lock').is_file() else None
        record={'argv':list(map(str,argv)),'cwd':str(root),'stage':'CLI invocation','expected':None}
        try:
            child=subprocess.run(argv,cwd=root,env=environment,capture_output=True,timeout=60)
        except (OSError,subprocess.TimeoutExpired) as error:
            record.update(exit_code=None,actual_termination='not_started' if isinstance(error,OSError) else 'protection_timeout; final status unavailable',error=str(error))
            for key in ('stdout','stderr'):
                raw=getattr(error,key,None)
                if raw is not None:(folder/key).write_bytes(raw)
                record[key+'_available']=raw is not None
            (folder/'receipt.json').write_text(json.dumps(record,indent=2));raise
        (folder/'stdout').write_bytes(child.stdout);(folder/'stderr').write_bytes(child.stderr)
        record.update(exit_code=child.returncode,signal=-child.returncode if child.returncode<0 else None,
                      stdout=str(folder/'stdout'),stderr=str(folder/'stderr'))
        (folder/'receipt.json').write_text(json.dumps(record,indent=2));records.append(record)
        return child,folder,before

    def project(name,deps=True,body='fn main()->i32 { return 0 }\n'):
        root=work/name;(root/'tests').mkdir(parents=True)
        declaration='dep="../dep",' if deps else ''
        (root/'package.tk').write_text('pub const PACKAGE=(name="a1",version="1.0.0",dependencies=('+declaration+'))\n')
        (root/'tests/a_test.tk').write_text(body)
        return root

    def fetch(name,root):
        child,folder,_=command(name+'-explicit-fetch',[str(sdk/'bin/toka'),'fetch'],root)
        if child.returncode!=0:
            raise AssertionError('explicit fixture fetch failed; inspect '+str(folder/'stderr'))

    def check(name,root,code,lock_code=None,environment=env,options=()):
        child,folder,before=command(name,[str(sdk/'bin/toka'),'test','--json',*options],root,environment)
        record=records[-1];record['expected']={'exit_code':code,'lock_code':lock_code}
        try:
            report=json.loads(child.stdout);validate(report,child.returncode)
            assert child.returncode==code,('expected CLI code',code,'actual',child.returncode)
            after=(root/'package.lock').read_bytes() if (root/'package.lock').is_file() else None
            assert before==after,'test implicitly changed lock bytes'
            if lock_code:
                if args.legacy:
                    assert report['errors'][0]['code'] is None
                    try:require_lock_failure(report,lock_code,child.returncode)
                    except ReportContractError as failure:record['expected_A1_rejection']=str(failure)
                    else:raise AssertionError('legacy report unexpectedly passes new code requirement')
                else:require_lock_failure(report,lock_code,child.returncode)
            elif code==2:
                assert report['result']=='infrastructure_error'
                assert all(e['code'] not in EXPECTED.values() for e in report['errors'])
            elif code==1:
                assert report['result']=='failed' and report['tests'][0]['result']=='compile_failed'
                assert report['tests'][0]['phases']['run']['state']=='not_started' and not report['errors']
            else:assert report['result']=='passed'
            record.update(result='pass',report=report,lock_unchanged=True)
        except (AssertionError,ValueError,KeyError,TypeError) as failure:
            record.update(result='fail',failure_phase='consumer_report_validation',error=str(failure),
                          next_check='Compare context-result.json and preparation/context phase with the fixed C6/P05 contract.')
            raise
        finally:(folder/'receipt.json').write_text(json.dumps(record,indent=2))

    for kind,expected in EXPECTED.items():
        root=project(kind);fetch(kind,root)
        if kind=='missing':(root/'package.lock').unlink()
        elif kind=='malformed':(root/'package.lock').write_bytes(b'invalid lock\n')
        else:(root/'package.tk').write_text('pub const PACKAGE=(name="a1",version="1.0.0",dependencies=(dep="../dep",another="../dep",))\n')
        check(kind,root,2,expected)
    root=project('valid-lock',body='import official/dep::{value}\nfn main()->i32 { if value()!=42 { return 1 } return 0 }\n');fetch('valid',root);check('valid-lock',root,0)
    root=project('legal-no-lock',False);check('legal-no-lock',root,0);assert not (root/'package.lock').exists()
    root=project('semantic-rejection',body='fn main()->i32 { return undeclared_a1_symbol }\n');fetch('semantic',root);check('semantic-rejection',root,1)
    if not args.legacy:
        for name in ('offline-missing','offline-corrupt','network-failure'):
            root=project(name);(root/'package.tk').write_text('pub const PACKAGE=(name="a1",version="1.0.0",dependencies=(reg="reg:1.0.0",))\n')
            (root/'package.lock').write_text('toka-lock-v1\npackage\treg\tregistry\treg\t1.0.0\t'+'1'*64+'\t'+'2'*64+'\t-\n')
            if name=='offline-corrupt':
                cache=root/'.toka/cache/archives';cache.mkdir(parents=True);(cache/('1'*64+'.tar.gz')).write_bytes(b'corrupt-cache')
            flags={'TOKA_REGISTRY_URL':'http://127.0.0.1:9'} if name=='network-failure' else {'TOKA_OFFLINE':'1'}
            check(name,root,2,environment=dict(env,**flags))
        root=project('lock-wait',False);(root/'.toka').mkdir();stream=(root/'.toka/test-context.lock').open('w');fcntl.flock(stream,fcntl.LOCK_EX)
        try:check('lock-wait',root,2,options=('--compile-timeout-ms','2000'))
        finally:stream.close()
        empty=out/'empty-path';empty.mkdir();root=project('missing-python',False)
        check('missing-python',root,2,environment=dict(env,PATH=str(empty)))
    (out/'summary.json').write_text(json.dumps({'result':'pass','kind':'legacy baseline' if args.legacy else 'local A1 CLI',
        'sdk':str(sdk),'records':records,'measurement_round_started':False},indent=2))
    print(json.dumps({'result':'pass','commands':len(records),'sdk':str(sdk),'legacy':args.legacy}))


if __name__=='__main__':main()
