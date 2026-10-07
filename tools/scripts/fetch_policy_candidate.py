#!/usr/bin/env python3
"""Read authenticated run/artifact identities and freeze exact 0.12 source bytes."""
import argparse,hashlib,json,subprocess,zipfile
from pathlib import Path
import release_platform_policy as policy


def api(repository,path):
    return json.loads(subprocess.check_output(['gh','api','repos/'+repository+'/'+path]))


def retain(repository,row,directory):
    directory.mkdir(exist_ok=True,parents=True);path=directory/(str(row['id'])+'.zip')
    with path.open('wb') as output:subprocess.run(['gh','api','repos/'+repository+'/actions/artifacts/'+str(row['id'])+'/zip'],stdout=output,check=True)
    if row.get('digest')!='sha256:'+hashlib.sha256(path.read_bytes()).hexdigest():raise ValueError('artifact transport digest differs')
    return zipfile.ZipFile(path)


def choose(rows,name):
    candidates=[row for row in rows if row.get('name')==name and row.get('expired') is False]
    if len(candidates)!=1:raise ValueError('missing/duplicate artifact: '+name)
    return candidates[0]


def main():
    p=argparse.ArgumentParser();p.add_argument('--repository',required=True);p.add_argument('--revision',required=True);p.add_argument('--tag-name',required=True);p.add_argument('--qualification-run-id',type=int,required=True);p.add_argument('--optional-run-id',type=int);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
    if not policy.modern(a.tag_name):raise ValueError('policy fetch is only for canonical 0.12/0.13')
    root=a.output_dir;root.mkdir(parents=True,exist_ok=False);zips=root/'artifact-zips';run=api(a.repository,'actions/runs/'+str(a.qualification_run_id))
    if run.get('status')!='completed' or run.get('conclusion')!='success' or run.get('head_sha')!=a.revision or run.get('path','').split('@',1)[0]!='.github/workflows/release.yml' or run.get('repository',{}).get('full_name')!=a.repository:raise ValueError('qualification source is not this candidate')
    rows=api(a.repository,'actions/runs/'+str(a.qualification_run_id)+'/artifacts?per_page=100')['artifacts']
    with retain(a.repository,choose(rows,'release-qualification-summary'),zips) as archive:
        if archive.namelist()!=['release-qualification-summary.json']:raise ValueError('ambiguous qualification summary')
        summary=json.loads(archive.read('release-qualification-summary.json'))
    if summary.get('source_run_id')!=run['id'] or summary.get('source_run_attempt')!=run['run_attempt']:raise ValueError('summary attempt differs')
    if a.optional_run_id:
        optional_run=api(a.repository,'actions/runs/'+str(a.optional_run_id))
        if optional_run.get('status')!='completed' or optional_run.get('conclusion')!='success' or optional_run.get('head_sha')!=a.revision or optional_run.get('path','').split('@',1)[0]!='.github/workflows/optional_macos_x64.yml':raise ValueError('optional source is not this candidate')
        optional_rows=api(a.repository,'actions/runs/'+str(a.optional_run_id)+'/artifacts?per_page=100')['artifacts']
        with retain(a.repository,choose(optional_rows,'optional-intel-validation'),zips) as archive:
            state=json.loads(archive.read('optional-status.json'))
        receipt=state.get('receipt',{})
        if receipt.get('source_run_id')!=optional_run['id'] or receipt.get('source_run_attempt')!=optional_run['run_attempt']:raise ValueError('optional source attempt differs')
        summary['optional_targets']={policy.OPTIONAL:state};rows+=optional_rows
        (root/'optional-run.json').write_text(json.dumps(optional_run,indent=2)+'\n')
    targets=policy.included_targets(summary,a.revision,a.tag_name)
    prefix={'workflow_dispatch':'candidate-archive-','push':'release-archive-'}.get(run['event'])
    if prefix is None:raise ValueError('unsupported source event')
    for target in targets:
        row=choose(rows,prefix+target);name='toka-%s-%s.tar.gz'%(a.tag_name,target)
        expected_run=a.optional_run_id if target==policy.OPTIONAL else a.qualification_run_id
        if row.get('workflow_run',{}).get('id')!=expected_run or row['workflow_run'].get('head_sha')!=a.revision:raise ValueError('artifact source identity differs')
        with retain(a.repository,row,zips) as archive:
            if archive.namelist()!=[name]:raise ValueError('ambiguous package artifact')
            destination=root/'qualification-archives'/(prefix+target);destination.mkdir(parents=True);(destination/name).write_bytes(archive.read(name))
    (root/'qualification-run.json').write_text(json.dumps(run,indent=2)+'\n')
    (root/'qualification-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (root/'qualification-artifacts.json').write_text(json.dumps({'artifacts':rows},indent=2)+'\n')


if __name__=='__main__':main()
