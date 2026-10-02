"""C6 report schema and graph-based diagnostic provenance. No text heuristics."""
import hashlib
import json
import re
from pathlib import Path
import time
import uuid

TIMINGS = ('argument_parse', 'project', 'artifact_setup', 'selection', 'identity',
           'dependencies', 'execution', 'report_preparation', 'total')
IDENTITY = ('tokac_path','tokac_version','tokac_sha256','runtime_object_path',
            'runtime_object_sha256','sdk_root','sdk_version','sdk_revision','lock_path','lock_sha256')


def phase(name):
    return {'name':name,'state':'not_started','duration_ms':None,'exit_code':None,
            'signal':None,'os_error':None,'process':None}


def cleanup(raw=None):
    if raw is None:
        return {'status':'not_needed','scope':'none','requested_signals':[],
                'leader_reaped':None,'group_absent':None,'output_complete':None,'duration_ms':None}
    data=raw['cleanup']
    return {'status':data['status'],'scope':raw['scope'],'requested_signals':raw.get('requested_signals',[]),
            'leader_reaped':data['direct_child_reaped'],'group_absent':data['group_gone'],
            'output_complete':data['logs_closed'],'duration_ms':data.get('duration_ms')}


def new_report():
    return {'schema':'toka.test-report','version':1,'run_id':str(uuid.uuid4()),'preview':True,
            'result':'infrastructure_error','reason':None,'exit_code':2,'finalized':False,
            'project_root':None,'artifact_root':None,'identity':dict(status='not_checked',**dict.fromkeys(IDENTITY)),
            'selection':{'mode':None,'filters':[],'candidate_count':None,'selected_count':None,'excluded':[]},
            'supervision':{'backend':'not_checked','scope':'none'},
            'timeouts':{'compile_ms':None,'run_ms':None,'compile_source':None,'run_source':None,
                        'terminate_grace_ms':2000,'kill_wait_ms':5000},
            'summary':{'total':None,'passed':0,'failed':0,'infrastructure_error':0,'interrupted':0,'not_run':0},
            'tests':[],'errors':[],'diagnostics':[],
            'termination':{'reason':None,'phase':None,'trigger':'none','signal':None,'cleanup':None},
            'timings':{name:phase(name) for name in TIMINGS},
            'timing_boundary':'total and report_preparation stop at final serialization snapshot'}


def observe(report, name, started, raw=None):
    record=phase(name);record['state']='completed';record['duration_ms']=(time.monotonic()-started)*1000
    if raw is not None:
        record.update(raw_phase(raw,name,'helper'))
    report['timings'][name]=record


def raw_phase(raw,name,role):
    record=phase(name)
    record.update(state='aborted' if raw.get('trigger') or raw.get('launch_error') else 'completed',
                  duration_ms=raw.get('execution_duration_ms',raw['duration_ms']),
                  exit_code=raw.get('exit_code'),signal=raw.get('signal'),os_error=raw.get('os_error'))
    if raw.get('pid') is not None:
        record['process']={'leader_pid':raw['pid'],'pgid':raw['pgid'],
                           'target_pid':raw['pid'],'target_role':role}
    return record


def source_origin(file, graph, sdk_root):
    result={'origin':'unknown','path':None,'package_node_id':None,'classification_basis':'none'}
    if not isinstance(file,str) or not file or not graph:
        return result
    try:
        path=Path(file)
        if not path.is_absolute():path=Path(graph['workspace_root'])/path
        path=path.resolve(strict=True)
        if not path.is_file():return result
        result['path']=str(path)
    except (OSError,ValueError):return result
    def inside(root):
        canonical=Path(root).resolve()
        return path==canonical or canonical in path.parents
    nodes={item['node'] for item in graph['dependencies'] if inside(item['root'])}
    if len(nodes)>1:return result
    if nodes and sdk_root and inside(sdk_root):return result
    if nodes:
        result.update(origin='dependency',package_node_id=next(iter(nodes)),classification_basis='locked_package_node')
    elif sdk_root and inside(sdk_root):
        result.update(origin='sdk',classification_basis='sdk_root')
    elif inside(graph['workspace_root']):
        # Nested projects are not members of the selected workspace graph.
        for parent in path.parents:
            if parent==Path(graph['workspace_root']).resolve():break
            if (parent/'package.tk').is_file():return result
        result.update(origin='user',package_node_id=graph['workspace_node'],classification_basis='workspace_node')
    return result


def diagnostics(raw, name, graph, sdk_root):
    collected=[]
    try:
        data=json.loads(Path(raw['stdout']).read_text())
    except (OSError,ValueError,UnicodeError):data=None
    if isinstance(data,dict) and data.get('schema')=='toka.diagnostics' and data.get('version') in (1,2) and isinstance(data.get('diagnostics'),list):
        for item in data['diagnostics']:
            if not isinstance(item,dict) or not isinstance(item.get('message'),str):continue
            primary=item.get('primary') or {}
            file=primary.get('file') if isinstance(primary,dict) else None
            collected.append({'code':item.get('code') if isinstance(item.get('code'),str) else None,
                              'message':item['message'],'severity':item.get('severity') if item.get('severity') in ('error','warning','note') else 'unknown',
                              'phase':name,'source':source_origin(file,graph,sdk_root)})
    # Unstructured text remains unknown; never infer severity, code or origin from it.
    for stream in ('stderr',):
        try:text=Path(raw[stream]).read_bytes().decode('utf-8',errors='replace')
        except OSError:continue
        if text:
            collected.append({'code':None,'message':text,'severity':'unknown','phase':name,
                              'source':source_origin(None,graph,sdk_root)})
    return collected


def compiler_identity(report, tokac, sdk_lib, probe):
    identity=report['identity'];identity['status']='failed'
    identity['tokac_path']=str(tokac)
    identity['sdk_root']=str(sdk_lib.parent.resolve())
    identity['runtime_object_path']=str(sdk_lib/'sys/toka_rt.o')
    identity['tokac_sha256']=hashlib.sha256(tokac.read_bytes()).hexdigest()
    identity['runtime_object_sha256']=hashlib.sha256((sdk_lib/'sys/toka_rt.o').read_bytes()).hexdigest()
    text=Path(probe['stdout']).read_text().strip()
    if not text:raise ValueError('compiler version probe produced no identity')
    identity['tokac_version']=text
    match=re.search(r'\b(?:Toka|tokac)\s+(?:version\s+)?v?(\d+\.\d+\.\d+(?:[-+][\w.-]+)?)',text,re.I)
    identity['sdk_version']=match.group(1) if match else None
    # Published SDK has no reliable revision record; never use the source checkout HEAD.
    identity['status']='checked'


def materialize(report, receipt):
    report['exit_code']=receipt['exit_code']
    report['result']={'infrastructure_or_configuration_error':'infrastructure_error'}.get(receipt.get('result'),receipt.get('result','infrastructure_error'))
    report['reason']=receipt.get('reason') or receipt.get('error') or receipt.get('persistence_error')
    report['project_root']=receipt.get('project_root');report['artifact_root']=receipt.get('artifact_root')
    if 'selection' in receipt:report['selection']=receipt['selection']
    if receipt.get('selection',{}).get('selected_count',0) and report['supervision']['backend']!='unsupported':
        report['supervision']={'backend':'posix_process_group','scope':'direct_child_and_process_group'}
    graph=receipt.get('provenance');sdk_root=report['identity']['sdk_root'] if report['identity']['status']=='checked' else None
    report['tests']=[];report['diagnostics']=[]
    report['preparation']={name:{'phase':raw_phase(raw,name,'helper'),'cleanup':cleanup(raw),
                                'logs':{key:str(Path(raw[key]).resolve()) if Path(raw[key]).is_file() else None for key in ('stdout','stderr')}}
                           for name,raw in receipt.get('preparation',{}).items()}
    last=None;last_name=None
    for raw_test in receipt.get('tests',[]):
        test={'id':raw_test['id'],'entry':raw_test['entry'],'result':raw_test['result'],'reason':None,
              'trigger':'none','interrupt_signal':None,'phases':{key:phase(key) for key in ('compile_link','compile','link','run')},
              'compile_mode':'unknown','cleanup':cleanup(),'logs':{key:None for key in ('compile_stdout','compile_stderr','run_stdout','run_stderr')},'diagnostics':[]}
        for key,role,log in [('compile_link','compiler','compile'),('run','test','run')]:
            if key not in raw_test:continue
            raw=raw_test[key]
            if raw_test['result'] in ('infrastructure_error','interrupted'):last,last_name=raw,key
            test['phases'][key]=raw_phase(raw,key,role);test['cleanup']=cleanup(raw)
            if key=='compile_link':test['compile_mode']='combined'
            test['trigger']=raw.get('trigger') if raw.get('trigger') in ('timeout','interrupt','residual_process') else 'none'
            test['reason']=raw.get('launch_error') or raw.get('supervision_error') or raw.get('trigger')
            test['interrupt_signal']=raw.get('interrupt_signal')
            for stream in ('stdout','stderr'):
                path=Path(raw[stream]);test['logs'][log+'_'+stream]=str(path.resolve()) if path.is_file() else None
            test['diagnostics']+=diagnostics(raw,key,graph,sdk_root)
        report['tests'].append(test);report['diagnostics']+=test['diagnostics']
    for name,raw in receipt.get('preparation',{}).items():
        report['diagnostics']+=diagnostics(raw,name,graph,sdk_root)
        if raw.get('trigger') or raw.get('launch_error') or raw.get('exit_code') not in (0,None) or raw.get('supervision_error'):
            last,last_name=raw,name
    total=receipt.get('selection',{}).get('selected_count')
    summary={'total':total,'passed':0,'failed':0,'infrastructure_error':0,'interrupted':0,'not_run':0}
    for test in report['tests']:
        category='failed' if test['result'] in ('compile_failed','run_failed','timed_out') else test['result']
        summary[category]+=1
    report['summary']=summary
    if receipt['exit_code'] in (2,130):
        kind='interrupt' if receipt['exit_code']==130 else ('configuration_error' if report['result']=='configuration_error' or total is None else 'infrastructure_error')
        report['termination']={'reason':kind,'phase':last_name or receipt.get('active_stage'),
                               'trigger':last.get('trigger') if last and last.get('trigger') in ('timeout','interrupt','residual_process') else ('interrupt' if receipt['exit_code']==130 else 'none'),
                               'signal':receipt.get('interrupt_signal'),'cleanup':cleanup(last)}
        if receipt['exit_code']==2:
            report['errors']=[{'code':None,'message':receipt.get('error') or receipt.get('persistence_error') or receipt.get('reason') or 'test preparation failed',
                               'phase':report['termination']['phase'],'os_error':receipt.get('os_error') if receipt.get('os_error') is not None else (last.get('os_error') if last else None),
                               'source':source_origin(None,graph,sdk_root)}]
    report['interrupt_signal']=receipt.get('interrupt_signal');report['interrupt_count']=receipt.get('interrupt_count',0)
    report['finalized']=receipt.get('finalized',False)
    return report
