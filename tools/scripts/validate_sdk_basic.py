#!/usr/bin/env python3
"""Run package-level basic checks without compiler sources or release qualification."""
import argparse,hashlib,json,os,re,subprocess,tarfile,tempfile
from pathlib import Path
import release_platform_policy as policy


def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path,required=True);p.add_argument('--target',required=True);p.add_argument('--revision',required=True);p.add_argument('--version-label',required=True);p.add_argument('--source-run-id',type=int,required=True);p.add_argument('--source-run-attempt',type=int,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--control-preview',action='store_true');a=p.parse_args()
    out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False);checks=[]
    def run(name,argv,cwd,expected=0):
        r=subprocess.run(argv,cwd=cwd,env=env,capture_output=True,timeout=180);(out/(name+'.stdout')).write_bytes(r.stdout);(out/(name+'.stderr')).write_bytes(r.stderr)
        row={'name':name,'argv':[str(x) for x in argv],'exit_code':r.returncode,'result':'pass' if r.returncode==expected else 'fail'};checks.append(row)
        if r.returncode!=expected:raise ValueError(name+' returned '+str(r.returncode))
        return r
    receipt={'schema':'toka.sdk-basic-validation','version':1,'policy_id':policy.policy_id(a.version_label),'target':a.target,'candidate_revision':a.revision,'version_label':a.version_label,
             'source_dirty':False,'source_run_id':a.source_run_id,'source_run_attempt':a.source_run_attempt,'archive_sha256':hashlib.sha256(a.archive.read_bytes()).hexdigest(),'result':'fail','checks':checks}
    try:
        with tempfile.TemporaryDirectory(prefix='toka-basic-sdk-') as tmp:
            root=Path(tmp)
            with tarfile.open(a.archive) as package:package.extractall(root,filter='data')
            directories=list(root.iterdir())
            if len(directories)!=1 or not directories[0].is_dir():raise ValueError('archive root is ambiguous')
            sdk=directories[0]
            if not a.control_preview and sdk.name!='toka-%s-%s'%(a.version_label,a.target):raise ValueError('archive root version/target differs')
            for tool in ('tokac','toka','tokafmt','tokalsp'):
                if not (sdk/'bin'/tool).is_file():raise ValueError('missing SDK tool '+tool)
            preview=(sdk/'preview-sdk.json').exists()
            if preview and not a.control_preview:raise ValueError('Preview composite cannot be a formal release receipt')
            checks.append({'name':'install','result':'pass','exit_code':0})
            env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
            expected=a.version_label.removeprefix('v')
            pattern=r'(?<![0-9A-Za-z])v?'+re.escape(expected)+r'(?![0-9A-Za-z.+-])'
            tools={}
            for name in policy.TOOLS:
                binary=sdk/'bin'/name;version=subprocess.run([str(binary),'--version'],cwd=root,env=env,capture_output=True,timeout=30)
                (out/(name+'-version.stdout')).write_bytes(version.stdout);(out/(name+'-version.stderr')).write_bytes(version.stderr)
                tools[name]={'path':str(binary),'version':expected if re.search(pattern,version.stdout.decode()) else None,'exit_code':version.returncode,'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'stdout_sha256':hashlib.sha256(version.stdout).hexdigest(),'stderr_sha256':hashlib.sha256(version.stderr).hexdigest()}
                if version.returncode or tools[name]['version'] is None:raise ValueError('installed tool version does not match: '+name)
            checks.append({'name':'versions','result':'pass','exit_code':0})
            receipt['sdk_identity']={'version_label':a.version_label,'candidate_revision':a.revision,'preview_composition':preview,'tokac_sha256':tools['tokac']['sha256'],'tools':tools}
            run('create',['toka','new','smoke'],root);project=root/'smoke'
            dependency=root/'basic-dependency'
            for name,body in policy.DEPENDENCY_FILES.items():
                path=dependency/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body)
            added=run('locked_dependency',['toka','add',str(dependency),'--alias','basic_dep','--json'],project)
            add=json.loads(added.stdout);lock=project/'package.lock';before=lock.read_bytes();lines=before.decode().splitlines()
            if len(lines)!=2 or lines[0]!='toka-lock-v1':raise ValueError('basic dependency lock is not an exact single entry')
            facts={'lock_entry':lines[1].split('\t'),'package_node_id':add['package_node_id'],'lock_sha256_before':hashlib.sha256(before).hexdigest(),'lock_sha256_after':hashlib.sha256(before).hexdigest(),'used_by':['build','run','test_pass','test_fail','test_timeout']}
            if policy.dependency_errors(facts) or add['content_sha256']!=policy.dependency_digest():raise ValueError('resolved basic dependency identity differs')
            receipt['dependencies']=facts;(out/'package.lock.before').write_bytes(before)
            def unchanged():
                after=lock.read_bytes();(out/'package.lock.after').write_bytes(after)
                if after!=before:raise ValueError('basic operation changed the locked dependency')
            prefix='import official/basic_dep::{answer}\n'
            (project/'src/main.tk').write_text(prefix+'fn main()->i32 { return answer() - 42 }\n')
            run('compile_link',['toka','build'],project);unchanged()
            run('run',['toka','run'],project);unchanged()
            tests=project/'tests';tests.mkdir()
            def test(name,body,expected,options=()):
                (tests/'basic_test.tk').write_text(prefix+body+'\n');child=run(name,['toka','test','--json',*options],project,expected);unchanged();report=json.loads(child.stdout)
                items=report.get('tests',[]);item=items[0] if len(items)==1 else {}
                data={'schema':report.get('schema'),'version':report.get('version'),'finalized':report.get('finalized'),'report_exit_code':report.get('exit_code'),'report_result':report.get('result'),'summary':report.get('summary'),'test_count':len(items),'test_id':item.get('id'),'test_result':item.get('result'),'compile_link':item.get('phases',{}).get('compile_link'),'run':item.get('phases',{}).get('run'),'trigger':item.get('trigger'),'cleanup':item.get('cleanup'),'dependency_nodes':report.get('dependencies',{}).get('nodes'),'lock_sha256':report.get('identity',{}).get('lock_sha256')}
                checks[-1]['facts']=data
                errors=policy.test_fact_errors(data,name,facts)
                if errors:checks[-1].update(result='fail',errors=errors);raise ValueError('; '.join(errors))
                if name=='test_timeout':checks[-1].update(trigger='timeout',cleanup='confirmed')
            test('test_pass','fn main()->i32 { return answer() - 42 }',0)
            test('test_fail','fn main()->i32 { if answer() != 42 { return 3 }\n return 7 }',1)
            test('test_timeout','fn main()->i32 { if answer() != 42 { return 3 }\n loop {} return 0 }',1,['--run-timeout-ms','100'])
            receipt['result']='pass'
    except (OSError,ValueError,KeyError,AssertionError,subprocess.TimeoutExpired) as error:receipt['error']=str(error)
    if a.control_preview:receipt.update(schema='toka.sdk-basic-control',not_a_release_receipt=True)
    (out/'basic-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'result':receipt['result'],'checks':len(checks),'not_a_release_receipt':a.control_preview}))
    raise SystemExit(0 if receipt['result']=='pass' else 1)


if __name__=='__main__':main()
