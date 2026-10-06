#!/usr/bin/env python3
"""Consume a frozen archive in a fresh installation; record actual matrix evidence."""
import argparse,hashlib,json,os,shutil,subprocess,sys,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'lib/toolchain'))
from test_toka_test_i1 import manifest,source
from test_toka_test_i2b import validate

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def cases(sdk,output):
    output.mkdir(parents=True,exist_ok=False);records=[]
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
    env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
    with tempfile.TemporaryDirectory(prefix='i2c-projects-') as temp:
        base=Path(temp)
        def invoke(name,setup,args=(),cwd_part=None,machine=True,expected=0):
            root=base/name;manifest(root);setup(root)
            cwd=root/cwd_part if cwd_part else root
            command=[str(sdk/'bin/toka'),'test',*(['--json'] if machine else []),*args]
            result=subprocess.run(command,cwd=cwd,env=env,capture_output=True,timeout=45)
            folder=output/name;folder.mkdir();(folder/'stdout').write_bytes(result.stdout);(folder/'stderr').write_bytes(result.stderr)
            record={'name':name,'command':command,'cwd':str(cwd),'exit_code':result.returncode,'report':None}
            try:
                report=json.loads(result.stdout) if machine else None;record['report']=report
                if machine:validate(report);assert report['exit_code']==result.returncode
            except (AssertionError,ValueError,KeyError,TypeError) as failure:
                record['consumer_contract_error']={'phase':'consumer_report_validation','message':str(failure),
                    'stdout':str(folder/'stdout'),'stderr':str(folder/'stderr'),
                    'next_check':'Compare original CLI status and context-result.json against C6/P05.'}
                (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');raise
            if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
            (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');records.append(record)
            assert result.returncode==expected,record
            return report
        ok='fn main() -> i32 { return 0 }\n'
        def discovery(root):
            source(root,'tests/a_test.tk',ok);source(root,'tests/nested/b_test.tk',ok)
            for name in ('helper.tk','main.tk','OTHER_TEST.tk'):source(root,'tests/'+name,'invalid')
        r=invoke('discovery',discovery);assert r['summary']['passed']==2 and r['selection']['candidate_count']==2
        r=invoke('explicit',discovery,['tests/a_test.tk','tests/nested/b_test.tk','tests/a_test.tk']);assert r['summary']['total']==2
        r=invoke('filter-or',discovery,['--filter','a_','--filter','b_']);assert r['summary']['passed']==2
        r=invoke('filter-nested',discovery,['--filter','nested/']);assert r['summary']['total']==1 and r['selection']['candidate_count']==2
        r=invoke('literal-filter',discovery,['--filter','*'],expected=2);assert r['reason']=='no_matches'
        r=invoke('allow-no-matches',discovery,['--filter','absent','--allow-empty']);assert r['result']=='empty' and r['summary']['passed']==0
        r=invoke('no-tests',lambda root:None,expected=2);assert r['reason']=='no_tests'
        r=invoke('allow-no-tests',lambda root:None,['--allow-empty']);assert r['result']=='empty'
        def space(root):source(root,'tests/space name_test.tk',ok);source(root,'tests/中文_test.tk',ok)
        r=invoke('project with space',space);assert r['summary']['passed']==2
        def hidden(root):source(root,'tests/.hidden/a_test.tk',ok)
        r=invoke('hidden',hidden);assert r['tests'][0]['id']=='tests/.hidden/a_test.tk'
        def nested(root):
            source(root,'tests/a_test.tk',ok);manifest(root/'tests/inner');source(root,'tests/inner/b_test.tk','invalid')
        r=invoke('nested',nested);assert r['summary']['passed']==1 and any(e['reason']=='nested_project' for e in r['selection']['excluded'])
        r=invoke('nested-explicit',nested,['tests/inner/b_test.tk'],expected=2);assert r['summary']['total'] is None
        def links(root):
            path=source(root,'tests/a_test.tk',ok);(root/'tests/link_test.tk').symlink_to(path)
        r=invoke('symlink-discovery',links);assert r['summary']['passed']==1 and any(e['reason']=='symlink' for e in r['selection']['excluded'])
        r=invoke('symlink-explicit',links,['tests/link_test.tk','--allow-empty'],expected=2);assert r['summary']['total'] is None
        def subdir(root):source(root,content=ok);(root/'src').mkdir()
        r=invoke('subdirectory',subdir,['../tests/ok_test.tk'],cwd_part='src');assert r['tests'][0]['id']=='tests/ok_test.tk'
        r=invoke('invalid-before-filter',discovery,['tests/a_test.tk','missing.tk','--filter','a_'],expected=2);assert r['summary']['total'] is None
        r=invoke('missing-main',lambda root:source(root,content='pub fn helper() -> i32 { return 0 }\n'),expected=1);assert r['tests'][0]['phases']['run']['state']=='not_started'
        invoke('help',lambda root:None,['--help'],machine=False)
        for name,args in [('help-json',['--help']),('duplicate-json',['--json']),('duplicate-allow',['--allow-empty','--allow-empty'])]:invoke(name,lambda root:None,args,expected=2)
        r=invoke('dash-entry',lambda root:source(root,'-named_test.tk',ok),['--','-named_test.tk']);assert r['summary']['passed']==1
        invoke('literal-json-entry',lambda root:None,['--','--json'],machine=False,expected=2)
        def fail_then_ok(root):source(root,'tests/a_test.tk','fn main() -> i32 { return not_declared }\n');source(root,'tests/b_test.tk',ok)
        r=invoke('compile-failure-continues',fail_then_ok,expected=1);assert r['summary']['failed']==1 and r['summary']['passed']==1
        r=invoke('exit-130-is-test-failure',lambda root:source(root,content='fn main() -> i32 { return 130 }\n'),expected=1)
        assert r['tests'][0]['phases']['run']['exit_code']==130 and r['tests'][0]['trigger']=='none'
        r=invoke('signal-then-ok',lambda root:(source(root,'tests/a_test.tk','extern fn libc_abort() -> void\nfn main() -> i32 { unsafe { libc_abort() } return 0 }\n'),source(root,'tests/b_test.tk',ok)),expected=1)
        assert r['summary']['failed']==1 and r['summary']['passed']==1 and r['tests'][0]['phases']['run']['signal']==6
        (output/'result.json').write_text(json.dumps({'result':'pass','records':[{'name':r['name'],'exit_code':r['exit_code']} for r in records]},indent=2)+'\n')

def installed_command(script,sdk,output,p05_contract):
    assert p05_contract in ('legacy','a1'), 'explicit installed contract required'
    command=[sys.executable,str(ROOT/'tools/scripts'/script),'--sdk',str(sdk),'--output',str(output)]
    if script=='test_toka_test_i2b.py':command+=['--contract',p05_contract]
    return command


def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path,required=True);p.add_argument('--identity',type=Path,required=True)
    p.add_argument('--candidate-sha',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--contract',choices=('legacy','a1'),required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    identity=json.loads(a.identity.read_text());assert identity['candidate_sha']==a.candidate_sha and identity['preview']
    assert sha(a.archive)==identity['archive_sha256']
    with tempfile.TemporaryDirectory(prefix='i2c-clean-install-') as temp:
        install=Path(temp)
        with tarfile.open(a.archive) as package:package.extractall(install,filter='data')
        sdk=next(install.iterdir());descriptor=json.loads((sdk/'preview-sdk.json').read_text())
        assert descriptor['components']==identity['components'] and descriptor['candidate_sha']==a.candidate_sha
        for name,data in descriptor['components'].items():assert sha(sdk/name)==data['sha256'],name
        clean_env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
        clean_env['PYTHONDONTWRITEBYTECODE']='1'
        for script,folder in [('test_toka_test_i1.py','i1-installed'),('test_toka_test_i2a.py','i2a-installed'),('test_toka_test_i2b.py','i2b-installed')]:
            command=installed_command(script,sdk,a.output/folder,a.contract)
            with (a.output/(folder+'.stdout')).open('wb') as out,(a.output/(folder+'.stderr')).open('wb') as err:
                result=subprocess.run(command,cwd=install,env=clean_env,stdout=out,stderr=err,timeout=300)
            assert result.returncode==0,(script,result.returncode)
        cases(sdk,a.output/'matrix-cases')
        changes=[name for name,data in descriptor['components'].items() if sha(sdk/name)!=data['sha256']]
        assert not changes,changes
        (a.output/'install-result.json').write_text(json.dumps({'result':'pass','preview':True,'candidate_sha':a.candidate_sha,'p05_contract':a.contract,
            'archive_sha256':identity['archive_sha256'],'target':identity['target'],'all_components_verified':len(descriptor['components']),
            'all_original_installed_bytes_unchanged':True,'no_source_tree_overrides':True,'package_definition':identity['composition']},indent=2)+'\n')

if __name__=='__main__':main()
