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
    receipt={'schema':'toka.sdk-basic-validation','version':1,'policy_id':policy.POLICY,'target':a.target,'candidate_revision':a.revision,'version_label':a.version_label,
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
            version=subprocess.run([str(sdk/'bin/tokac'),'--version'],cwd=root,env=env,capture_output=True,timeout=30)
            manager=subprocess.run([str(sdk/'bin/toka'),'--version'],cwd=root,env=env,capture_output=True,timeout=30)
            (out/'tokac-version.stdout').write_bytes(version.stdout);(out/'toka-version.stdout').write_bytes(manager.stdout)
            expected=a.version_label.removeprefix('v')
            pattern=r'(?<![0-9A-Za-z])v?'+re.escape(expected)+r'(?![0-9A-Za-z.+-])'
            if version.returncode or manager.returncode or not re.search(pattern,version.stdout.decode()) or not re.search(pattern,manager.stdout.decode()):raise ValueError('installed tool versions do not match')
            checks.append({'name':'versions','result':'pass','exit_code':0})
            receipt['sdk_identity']={'version_label':a.version_label,'candidate_revision':a.revision,'preview_composition':preview,'tokac_sha256':hashlib.sha256((sdk/'bin/tokac').read_bytes()).hexdigest()}
            run('create',['toka','new','smoke'],root);project=root/'smoke'
            run('compile_link',['toka','build'],project)
            run('run',['toka','run'],project)
            tests=project/'tests';tests.mkdir()
            (tests/'basic_test.tk').write_text('fn main()->i32 { return 0 }\n')
            run('test_pass',['toka','test','--json'],project)
            (tests/'basic_test.tk').write_text('fn main()->i32 { return 7 }\n')
            failed=run('test_fail',['toka','test','--json'],project,1);assert json.loads(failed.stdout)['summary']['failed']==1
            (tests/'basic_test.tk').write_text('fn main()->i32 { loop {} return 0 }\n')
            timed=run('test_timeout',['toka','test','--json','--run-timeout-ms','100'],project,1);report=json.loads(timed.stdout);item=report['tests'][0]
            if item['trigger']!='timeout' or item['cleanup']['status']!='confirmed':raise ValueError('timeout cleanup was not confirmed')
            checks[-1].update(trigger='timeout',cleanup='confirmed')
            receipt['result']='pass'
    except (OSError,ValueError,KeyError,AssertionError,subprocess.TimeoutExpired) as error:receipt['error']=str(error)
    if a.control_preview:receipt.update(schema='toka.sdk-basic-control',not_a_release_receipt=True)
    (out/'basic-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'result':receipt['result'],'checks':len(checks),'not_a_release_receipt':a.control_preview}))
    raise SystemExit(0 if receipt['result']=='pass' else 1)


if __name__=='__main__':main()
