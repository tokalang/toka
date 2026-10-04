#!/usr/bin/env python3
"""S01-S04 with locked projects, a complete installed SDK and real compiler checks."""
import argparse,base64,hashlib,json,os,subprocess
from pathlib import Path

LIB='''pub shape Data(val: i32)
pub fn read_pair(a: Data, b: Data) -> i32 { return a.val + b.val }
pub fn mut_and_read(target#: Data, view: Data) -> i32 { target.val = 10; return view.val }
pub fn dep_call(a: Data, b: Data) -> i32 { return read_pair(a,b) }
'''
MAIN='''import std/io::{println}
import official/dep::{Data, read_pair, mut_and_read}
fn main() -> i32 { auto x# = Data(val = 2); auto v = read_pair(x, x); println("evidence"); return v - 4 }
'''


def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
    records=[]
    def execute(name,argv,cwd,expected):
        folder=out/name;folder.mkdir();child=subprocess.run(argv,cwd=cwd,env=env,capture_output=True,timeout=90)
        (folder/'stdout').write_bytes(child.stdout);(folder/'stderr').write_bytes(child.stderr)
        assert child.returncode==expected,(name,child.returncode,child.stderr)
        data=json.loads(child.stdout);r={'name':name,'argv':[os.fsencode(value).decode('utf-8',errors='replace') for value in argv],
                                      'argv_base64':[base64.b64encode(os.fsencode(value)).decode('ascii') for value in argv],
                                      'cwd':str(cwd),'exit_code':child.returncode,'report':data};(folder/'result.json').write_text(json.dumps(r,indent=2)+'\n');return r
    def setup(name,body=LIB,source=MAIN):
        root=out/name;root.mkdir();(root/'package.tk').write_text('pub const PACKAGE=(name="scope",version="1.0.0",dependencies=())\n');(root/'main.tk').write_text(source)
        dep=out/(name+'-dep');(dep/'lib/official').mkdir(parents=True);(dep/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=())\n');(dep/'lib/official/dep.tk').write_text(body)
        execute(name+'-add',[str(sdk/'bin/toka'),'add',str(dep),'--alias','dep','--json'],root,0)
        return root,dep/'lib/official/dep.tk'
    def run(name,root,options=(),expected=0):
        before=(root/'package.lock').read_bytes();r=execute(name,[str(sdk/'bin/toka'),'evidence','main.tk','--json',*options],root,expected);assert (root/'package.lock').read_bytes()==before
        d=r['report'];assert d['schema']=='toka.semantic-evidence-view' and d['version']==1 and d['exit_code']==expected and d['scope']['input']
        if expected==2:assert d['result']=='configuration_error' and d['errors'] and d['records']==[]
        else:
            assert d['analysis']['scope']=='full' and d['analysis']['exit_code']==expected and d['result']==('passed' if expected==0 else 'failed')
            for record in d['records']:assert d['reasons'][record['reason_id']]['origin_location']==record['origin_location']
            assert base64.b64decode(d['compiler']['stderr_base64'])==(out/name/'stderr').read_bytes()
        records.append(r);return d
    root,dep=setup('pass-project');full=run('S01-all',root,['--scope','all']);file=run('S01-default',root);explicit=run('S01-explicit',root,['--scope','file','--target','main.tk']);dep_view=run('S01-dependency',root,['--target',str(dep)])
    assert file['records']==explicit['records'] and len(file['records'])<len(full['records']) and file['scope']['output_filtered']
    assert full['compiler']['argv']==file['compiler']['argv']==dep_view['compiler']['argv'] and full['compiler']['stdout_sha256']==file['compiler']['stdout_sha256']==dep_view['compiler']['stdout_sha256']
    assert any(r['source']['origin']=='dependency' and r['source']['package_node_id'].startswith('pkg-v1-') for r in dep_view['records'])
    raw_env=dict(env,TOKA_LIB=str(sdk/'lib'));raw=subprocess.run(full['compiler']['argv'],cwd=root,env=raw_env,capture_output=True,timeout=90);(out/'raw-compiler.stdout').write_bytes(raw.stdout);(out/'raw-compiler.stderr').write_bytes(raw.stderr);assert raw.returncode==0 and hashlib.sha256(raw.stdout).hexdigest()==full['compiler']['stdout_sha256']
    original=json.loads(raw.stdout);assert [{k:v for k,v in r.items() if k not in ('decision_id','reason_id','source','origin_source')} for r in full['records']]==original['records']
    cross=next(r for r in full['records'] if r['origin_location']['file'] and r['origin_location']['file']!=r['primary_location']['file'])
    decision=run('S03-cross-file-decision',root,['--scope','decision','--decision',cross['decision_id']]);assert decision['records']==[cross] and decision['compiler']['stdout_sha256']==full['compiler']['stdout_sha256'] and decision['scope']['output_filtered']
    local=next(r for r in file['records'] if r['decision']=='Allow');selected=run('S03-entry-decision',root,['--scope','decision','--decision',local['decision_id']]);assert selected['records']==[local]
    badroot,baddep=setup('dependency-failure',LIB+'pub fn invalid() -> i32 { auto x# = Data(val = 1); return mut_and_read(x,x) }\n')
    all_failure=run('S02-dependency-all',badroot,['--scope','all'],1);file_failure=run('S02-dependency-file',badroot,expected=1)
    assert all_failure['compiler']['stdout_sha256']==file_failure['compiler']['stdout_sha256'] and all_failure['compiler']['argv']==file_failure['compiler']['argv']
    rejects=[r for r in all_failure['records'] if r['decision']!='Allow'];assert rejects and all(r in file_failure['records'] for r in rejects) and any(r['source']['origin']=='dependency' for r in rejects)
    choice=next(r for r in all_failure['records'] if r['decision']=='Allow');failure_decision=run('S02-dependency-decision',badroot,['--scope','decision','--decision',choice['decision_id']],1);assert all(r in failure_decision['records'] for r in rejects)
    callroot,_=setup('call-failure',source=MAIN.replace('read_pair(x, x)','mut_and_read(x, x)'));callall=run('S02-call-all',callroot,['--scope','all'],1);callfile=run('S02-call-file',callroot,expected=1);assert callall['compiler']['stdout_sha256']==callfile['compiler']['stdout_sha256'] and any(r['decision']=='Reject' for r in callfile['records'])
    foreign=out/'foreign.tk';foreign.write_text('fn main()->i32 { return 0 }\n')
    for name,options in [('scope',['--scope','bogus']),('missing-target',['--target','absent.tk']),('foreign-target',['--target',str(foreign)]),('decision',['--scope','decision','--decision','decision-v1-'+'0'*64]),('missing-decision',['--scope','decision']),('conflict',['--scope','all','--target','main.tk']),('missing-value',['--scope']),('unexpected',['--unknown'])]:
        run('S04-'+name,root,options,2)
    for name,options in [('scope',['--scope',os.fsdecode(b'\xff')]),('target',['--target',os.fsdecode(b'\xff')]),('decision',['--scope','decision','--decision',os.fsdecode(b'\xff')])]:
        report=run('S04-encoding-'+name,root,options,2);assert base64.b64decode(report['scope']['input_base64'][-1])==b'\xff' and report['analysis']['result']=='not_started'
    result={'result':'pass','scenarios':len(records),'records':records,'rows':['S01','S02','S03','S04'],'source_checkout_required':False,'SDK':str(sdk),'Preview':True}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'result':'pass','scenarios':len(records)}))


if __name__=='__main__':main()
