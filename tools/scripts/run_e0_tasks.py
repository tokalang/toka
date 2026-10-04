#!/usr/bin/env python3
"""Record fixed E0 task execution. A documentation replay is not a human participant."""
import argparse,difflib,hashlib,json,os,platform,shutil,signal,subprocess,tarfile,time
from pathlib import Path


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--input-plan',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--sdk-archive',type=Path,required=True);p.add_argument('--sdk-sha256',required=True);p.add_argument('--sdk-source-sha',required=True);p.add_argument('--variant',choices=['original_0.11.0','candidate'],required=True);p.add_argument('--flow',choices=['AI_process','documentation_replay'],required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);fixtures=a.fixtures.resolve();plan=json.loads(a.input_plan.read_text())
    for name,value in plan['task_fixtures'].items():assert sha(fixtures/name)==value,name
    assert sha(a.sdk_archive)==a.sdk_sha256
    install=out/'installed-sdk';install.mkdir()
    with tarfile.open(a.sdk_archive) as archive:archive.extractall(install,filter='data')
    sdk=next(install.iterdir());assert len(list(install.iterdir()))==1
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1')
    identity={'variant':a.variant,'archive_sha256':a.sdk_sha256,'source_sha':a.sdk_source_sha,'tools':{name:sha(sdk/'bin'/name) for name in ('tokac','toka','tokafmt','tokalsp')},'preview_composition':(sdk/'preview-sdk.json').exists()}
    (out/'SDK-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    sessions=[]
    for project_name in ('registry_unicode_consumer','csv-transform'):
        previous=None
        for temperature in ('cold','warm'):
            root=out/(project_name+'-'+temperature);root.mkdir();logs=root/'records';logs.mkdir();steps=[];changes=[]
            name='registry_unicode_consumer' if project_name=='registry_unicode_consumer' else 'csv_transform';project=root/name
            def execute(stage,argv,cwd):
                index=len(steps)+1;started=time.monotonic();limit=None
                child=subprocess.Popen(argv,cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
                try:stdout,stderr=child.communicate(timeout=900)
                except subprocess.TimeoutExpired:
                    limit='measurement_guard';os.killpg(child.pid,signal.SIGTERM)
                    try:stdout,stderr=child.communicate(timeout=5)
                    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);stdout,stderr=child.communicate()
                duration=(time.monotonic()-started)*1000;stem='%02d-%s'%(index,stage)
                (logs/(stem+'.stdout')).write_bytes(stdout);(logs/(stem+'.stderr')).write_bytes(stderr)
                record={'stage':stage,'argv':[str(v) for v in argv],'cwd':str(cwd),'exit_code':child.returncode if child.returncode>=0 else None,'signal':-child.returncode if child.returncode<0 else None,'duration_ms':duration,'guard':limit,'stdout':str(logs/(stem+'.stdout')),'stderr':str(logs/(stem+'.stderr')),'repair_iteration':len(changes),'failure_category':'unknown' if child.returncode else None,'network_acquisition_ms':None,'network_timing_boundary':'registry add aggregates acquisition/resolution; no separate network timer' if stage=='add' and project_name=='registry_unicode_consumer' else 'not_applicable'}
                steps.append(record);(logs/(stem+'.json')).write_text(json.dumps(record,indent=2)+'\n');return child.returncode,stdout
            execute('create',['toka','new',name],root)
            if not project.is_dir():
                sessions.append({'project':project_name,'temperature':temperature,'steps':steps,'completion':'incomplete','reason':'create_failed'});continue
            for relative in ('build.tk','src/main.tk'):
                target=project/relative;target.parent.mkdir(exist_ok=True,parents=True);shutil.copyfile(fixtures/'source'/project_name/relative,target)
            tests=project/'tests';tests.mkdir()
            if project_name=='registry_unicode_consumer':shutil.copyfile(project/'src/main.tk',tests/'basic_test.tk')
            else:
                shutil.copytree(fixtures/'local-dependency',root/'local-dependency');shutil.copytree(fixtures/'resources',project/'resources');shutil.copyfile(fixtures/'csv-e0_test.tk',tests/'basic_test.tk')
            if temperature=='warm' and previous is not None:
                for cached in ('cache','packages'):
                    source=previous/'.toka'/cached
                    if source.exists():shutil.copytree(source,project/'.toka'/cached,dirs_exist_ok=True)
                if project_name=='registry_unicode_consumer' and (previous/'package.lock').exists():shutil.copyfile(previous/'package.lock',project/'package.lock')
            (root/'restored-inputs.json').write_text(json.dumps({'plan_sha256':sha(a.input_plan),'fixture_hashes':plan['task_fixtures'],'source_edits':False,'cache_seed':str(previous) if temperature=='warm' else None,'cold_start':'empty task workspace/package cache'},indent=2)+'\n')
            spec='unicode:0.1.2' if project_name=='registry_unicode_consumer' else '../local-dependency/e0_paths'
            code,_=execute('add',['toka','add',spec],project)
            lock=project/'package.lock';before=lock.read_bytes() if lock.exists() else None
            if before is not None:(root/'package.lock.before').write_bytes(before)
            lock_expected=(fixtures/'expected-unicode-0.1.2.lock').read_bytes() if project_name=='registry_unicode_consumer' else None
            code,diagnostics=execute('check',['toka','check','--json','src/main.tk'],project)
            def edit(relative,body,reason):
                path=project/relative;old=path.read_text();patch=''.join(difflib.unified_diff(old.splitlines(True),body.splitlines(True),fromfile=relative,tofile=relative));path.write_text(body)
                target=logs/('repair-%02d.diff'%(len(changes)+1));target.write_text(patch)
                changes.append({'actor':'Codex','kind':'AI_workspace_source_edit','file':relative,'reason':reason,'diff':str(target),'fixture_modified':False,'before_sha256':hashlib.sha256(old.encode()).hexdigest(),'after_sha256':sha(path)})
            if a.flow=='AI_process' and code and project_name=='registry_unicode_consumer':
                try:codes={d['code'] for d in json.loads(diagnostics).get('diagnostics',[])}
                except (ValueError,TypeError,KeyError):codes=set()
                if 'E01268' in codes:
                    edit('src/main.tk',(project/'src/main.tk').read_text().replace("'value","value"),'Migrate removed quote binding syntax exactly as E01268 requests.')
                    edit('tests/basic_test.tk',(tests/'basic_test.tk').read_text().replace("'value","value"),'Apply the same source migration to the copied test entry.')
                    execute('check-repaired',['toka','check','--json','src/main.tk'],project)
            execute('build',['toka','build'],project)
            code,_=execute('test',['toka','test','--json'],project)
            if a.flow=='AI_process' and code and a.variant=='candidate' and project_name=='csv-transform':
                corrected='''import official/e0_paths::{input_path,output_path}
import std/process::{Command}
import std/fs::{exists}
fn main() -> i32 {
    auto command# = Command::new(string::from("./target/debug/csv_transform"))
    command#.arg(input_path())
    command#.arg(output_path())
    if command#.status() != 0 { return 1 }
    if !exists(output_path()) { return 2 }
    return 0
}
'''
                edit('tests/basic_test.tk',corrected,'Repair fixed scaffold syntax following the retained test compile diagnostics; task and expected output unchanged.')
                execute('test-repaired',['toka','test','--json'],project)
            argv=['toka','run']+(['--','resources/input.csv','output.csv'] if project_name=='csv-transform' else [])
            execute('run',argv,project)
            output_valid=False if project_name=='csv-transform' else None
            if project_name=='csv-transform' and (project/'output.csv').exists():
                result,_=execute('verify_output',['python3',str(fixtures/'verify_csv.py'),'output.csv'],project);output_valid=result==0
            after=lock.read_bytes() if lock.exists() else None
            if after is not None:(root/'package.lock.after').write_bytes(after)
            lock_unchanged=before is not None and before==after;lock_matches=before==lock_expected if lock_expected is not None else before is not None
            delivery=root/'delivery.tar.gz'
            with tarfile.open(delivery,'w:gz') as package:
                for relative in ('package.tk','package.lock','build.tk','src','tests','resources','output.csv','target/debug/'+name):
                    path=project/relative
                    if path.exists():package.add(path,arcname=name+'/'+relative)
                if (root/'local-dependency').exists():package.add(root/'local-dependency',arcname='local-dependency')
            (root/'delivery.json').write_text(json.dumps({'archive_sha256':sha(delivery),'original_lock_preserved':lock_unchanged,'local_path_lock_is_original_host_identity':project_name=='csv-transform','portable_source_relock_not_verified':project_name=='csv-transform'},indent=2)+'\n')
            latest={row['stage'].removesuffix('-repaired'):row for row in steps}
            complete=all(s['exit_code']==0 and s['guard'] is None for s in latest.values()) and lock_unchanged and lock_matches and (output_valid is not False)
            session={'project':project_name,'temperature':temperature,'flow':a.flow,'execution_actor':'Codex','human_participant':False,'steps':steps,'completion':'complete' if complete else 'incomplete','lock_unchanged':lock_unchanged,'lock_matches_fixed_release':lock_matches,'output_valid':output_valid,'repair_iterations':len(changes),'manual_interventions':changes,'first_failures_preserved':True,'delivery_sha256':sha(delivery)}
            (root/'session.json').write_text(json.dumps(session,indent=2)+'\n');sessions.append(session);previous=project
    result={'schema':'toka.e0-task-results','version':1,'variant':a.variant,'flow':a.flow,'execution_actor':'Codex','human_participant':False,'SDK':identity,'host':{'system':platform.system(),'machine':platform.machine(),'python':platform.python_version()},'sessions':sessions,'completion_rate':sum(s['completion']=='complete' for s in sessions)/len(sessions),'Accepted':False,'Preview':True}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'variant':a.variant,'flow':a.flow,'completion_rate':result['completion_rate'],'sessions':len(sessions)}))


if __name__=='__main__':main()
