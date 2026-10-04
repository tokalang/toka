"""0.12 exact byte/attempt binding shared by draft creation and promotion."""
import hashlib
import json
from pathlib import Path
import zipfile
import release_platform_policy as policy


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def candidate_archives(args,summary,run):
    targets=policy.included_targets(summary,args.candidate_sha,args.tag_name)
    if run.get('id')!=args.qualification_run_id or run.get('run_attempt')!=summary['source_run_attempt'] or \
            run.get('status')!='completed' or run.get('conclusion')!='success' or run.get('head_sha')!=args.candidate_sha or \
            run.get('path','').split('@',1)[0]!='.github/workflows/release.yml' or \
            run.get('repository',{}).get('full_name')!=getattr(args,'repository','tokalang/toka'):
        raise ValueError('qualified run identity/attempt/repository does not match')
    if summary['source_run_id']!=args.qualification_run_id:raise ValueError('summary source run does not match')
    source={'workflow_dispatch':'candidate_run','push':'qualified_run'}.get(run.get('event'))
    if source is None:raise ValueError('unsupported qualification event')
    prefix='candidate-archive-' if source=='candidate_run' else 'release-archive-'
    directories=list(args.qualified_archives_dir.iterdir())
    if len(directories)!=len(targets) or {p.name for p in directories}!={prefix+t for t in targets}:
        raise ValueError('archive set does not match core plus validated optional targets')
    archives={}
    for target in targets:
        name='toka-%s-%s.tar.gz'%(args.tag_name,target);directory=args.qualified_archives_dir/(prefix+target);path=directory/name
        if directory.is_symlink() or not directory.is_dir() or path.is_symlink() or not path.is_file() or {p.name for p in directory.iterdir()}!={name}:
            raise ValueError('missing/ambiguous original archive: '+target)
        archives[name]=digest(path)
    metadata_path=getattr(args,'qualification_artifacts_json',None)
    zips=getattr(args,'artifact_zips_dir',None)
    if metadata_path is None or zips is None:raise ValueError('0.12 requires authenticated artifact identities and retained ZIP bytes')
    metadata=json.loads(metadata_path.read_text()).get('artifacts')
    if not isinstance(metadata,list):raise ValueError('artifact metadata is missing')
    for target in targets:
        expected_run=args.qualification_run_id
        if target==policy.OPTIONAL:expected_run=summary['optional_targets'][target]['receipt']['source_run_id']
        rows=[row for row in metadata if isinstance(row,dict) and row.get('name')==prefix+target]
        if len(rows)!=1:raise ValueError('missing/duplicate artifact identity: '+target)
        row=rows[0];binding=row.get('workflow_run',{})
        if row.get('expired') is not False or not policy.positive_integer(row.get('id')) or binding.get('id')!=expected_run or binding.get('head_sha')!=args.candidate_sha:
            raise ValueError('artifact source does not bind candidate: '+target)
        packed=zips/(str(row['id'])+'.zip');name='toka-%s-%s.tar.gz'%(args.tag_name,target)
        if packed.is_symlink() or not packed.is_file() or row.get('digest')!='sha256:'+digest(packed):raise ValueError('artifact ZIP digest differs: '+target)
        with zipfile.ZipFile(packed) as archive:
            if archive.namelist()!=[name] or hashlib.sha256(archive.read(name)).hexdigest()!=archives[name]:raise ValueError('archive bytes differ from authenticated ZIP: '+target)
    if policy.OPTIONAL in targets:
        errors=policy.optional_errors(summary['optional_targets'][policy.OPTIONAL],args.candidate_sha,args.tag_name,archives['toka-%s-%s.tar.gz'%(args.tag_name,policy.OPTIONAL)])
        if errors:raise ValueError('; '.join(errors))
        optional_run_path=getattr(args,'optional_run_json',None)
        if optional_run_path is None:raise ValueError('included Intel asset requires its independent source run')
        optional_run=json.loads(optional_run_path.read_text());receipt=summary['optional_targets'][policy.OPTIONAL]['receipt']
        if optional_run.get('id')!=receipt['source_run_id'] or optional_run.get('run_attempt')!=receipt['source_run_attempt'] or optional_run.get('head_sha')!=args.candidate_sha or optional_run.get('conclusion')!='success' or optional_run.get('status')!='completed' or optional_run.get('path','').split('@',1)[0]!='.github/workflows/optional_macos_x64.yml' or optional_run.get('event')!='workflow_dispatch':raise ValueError('Intel source run/attempt/workflow does not match')
    return archives,targets,source


def draft_assets(args,archives):
    expected=set(archives)|{'SHA256SUMS'};draft=json.loads(args.draft_json.read_text())
    rows=draft.get('assets',[]);names=[r.get('name') for r in rows if isinstance(r,dict)]
    if draft.get('tagName')!=args.tag_name or draft.get('isDraft') is not True or draft.get('isPrerelease') is not False or draft.get('isLatest') is True or len(names)!=len(expected) or set(names)!=expected:
        raise ValueError('draft state/exact asset set does not match')
    directory=getattr(args,'draft_assets_dir',None) or args.assets_dir
    if {p.name for p in directory.iterdir()}!=expected:raise ValueError('downloaded assets differ from the declared set')
    for name,value in archives.items():
        path=directory/name
        if path.is_symlink() or not path.is_file() or digest(path)!=value:raise ValueError('asset differs from original bytes: '+name)
    manifest=''.join('%s  %s\n'%(archives[name],name) for name in sorted(archives))
    sums=directory/'SHA256SUMS'
    if sums.is_symlink() or not sums.is_file() or sums.read_text()!=manifest:raise ValueError('exact checksums do not match')


def validate_draft(args):
    summary=json.loads(args.qualification_summary.read_text());run=json.loads(args.qualification_run_json.read_text())
    if run.get('event')!='workflow_dispatch' or run.get('head_repository',{}).get('full_name')!=args.repository:raise ValueError('draft requires same-repository candidate dispatch')
    archives,_,_=candidate_archives(args,summary,run)
    if args.draft_json is not None:draft_assets(args,archives)
    return archives


def validate_promotion(args,observed):
    try:
        summary=json.loads(args.qualification_summary.read_text());run=json.loads(args.qualification_run_json.read_text())
        archives,targets,source=candidate_archives(args,summary,run);draft_assets(args,archives)
        replay_run=json.loads(args.replay_run_json.read_text());receipt=json.loads(args.replay_receipt.read_text())
        if replay_run.get('id')!=args.replay_run_id or replay_run.get('status')!='completed' or replay_run.get('conclusion')!='success' or replay_run.get('event')!='workflow_dispatch' or replay_run.get('path','').split('@',1)[0]!='.github/workflows/qualified_artifact_replay.yml':
            raise ValueError('replay run identity does not match')
        if receipt.get('schema')!='toka.qualified-artifact-replay-summary' or receipt.get('version')!=2 or receipt.get('policy_id')!=policy.POLICY or receipt.get('result')!='pass' or receipt.get('errors')!=[] or receipt.get('candidate_revision')!=args.candidate_sha or receipt.get('version_label')!=args.tag_name or receipt.get('qualification_run_id')!=args.qualification_run_id or receipt.get('qualification_run_attempt')!=run['run_attempt'] or receipt.get('replay_run_id')!=args.replay_run_id or receipt.get('replay_run_attempt')!=replay_run.get('run_attempt'):
            raise ValueError('replay summary identity/policy/attempt does not match')
        rows=receipt.get('receipts')
        if not isinstance(rows,list) or len(rows)!=len(targets) or any(not isinstance(r,dict) for r in rows) or {r.get('target') for r in rows}!=set(targets):raise ValueError('replay must cover the exact included asset set')
        for row in rows:
            name='toka-%s-%s.tar.gz'%(args.tag_name,row['target'])
            if row.get('result')!='pass' or row.get('archive_sha256')!=archives[name] or row.get('candidate_revision')!=args.candidate_sha or row.get('version_label')!=args.tag_name or row.get('policy_id')!=policy.POLICY or row.get('asset_source')!=source or row.get('qualification_run_id')!=args.qualification_run_id or row.get('qualification_run_attempt')!=run['run_attempt']:
                raise ValueError('replay bytes/source do not match: '+row['target'])
        observed['archive_source']=source
        observed['archives']={t:{'archive_name':'toka-%s-%s.tar.gz'%(args.tag_name,t),'draft_sha256':archives['toka-%s-%s.tar.gz'%(args.tag_name,t)],'qualification_sha256':archives['toka-%s-%s.tar.gz'%(args.tag_name,t)]} for t in targets}
        return []
    except (ValueError,OSError,TypeError,KeyError) as error:return [str(error)]
