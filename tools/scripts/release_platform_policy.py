"""Version-bound platform contracts. Never relax a historical qualification."""
import re
import json
import hashlib

LEGACY = ('linux-x64','linux-arm64','macos-x64','macos-arm64')
CORE = ('linux-x64','linux-arm64','macos-arm64')
OPTIONAL = 'macos-x64'
POLICY = 'toka.release-platforms.0.12.v1'
STATES = ('not_run','running','passed','failed')
TOOLS = ('tokac','toka','tokafmt','tokalsp')
CHECKS = ('install','versions','create','locked_dependency','compile_link','run','test_pass','test_fail','test_timeout')
DEPENDENCY_FILES = {'package.tk':b'pub const PACKAGE=(name="basic_dep",version="0.1.0",dependencies=())\n',
                    'lib/official/basic_dep.tk':b'pub fn answer()->i32 { return 42 }\n'}


def dependency_digest():
    value=hashlib.sha256()
    for name,body in sorted(DEPENDENCY_FILES.items()):
        encoded=name.encode();value.update(len(encoded).to_bytes(8,'big'));value.update(encoded);value.update(len(body).to_bytes(8,'big'));value.update(body)
    return value.hexdigest()


def dependency_errors(facts):
    if not isinstance(facts,dict):return ['locked dependency facts are missing']
    errors=[];fields=facts.get('lock_entry')
    if not isinstance(fields,list) or len(fields)!=8 or any(not isinstance(v,str) for v in fields):return ['locked dependency entry is invalid']
    if fields[:3]!=['package','basic_dep','path'] or fields[5]!='-' or fields[6]!=dependency_digest() or fields[7]!='-' or fields[3]!=fields[4]:errors.append('fixed dependency content/kind/identity does not match')
    node='pkg-v1-'+hashlib.sha256('\0'.join(('toka.package-node.v1',fields[2],fields[3],fields[4],fields[5],fields[6],'')).encode()).hexdigest()
    if facts.get('package_node_id')!=node:errors.append('locked dependency node does not match')
    expected=hashlib.sha256(('toka-lock-v1\n'+'\t'.join(fields)+'\n').encode()).hexdigest()
    if facts.get('lock_sha256_before')!=expected or facts.get('lock_sha256_after')!=expected:errors.append('lock bytes changed or digest does not match the exact entry')
    if facts.get('used_by')!=['build','run','test_pass','test_fail','test_timeout']:errors.append('locked dependency is not used throughout basic validation')
    return errors


def test_fact_errors(facts,name,dependency):
    if not isinstance(facts,dict):return ['test phase facts are missing: '+name]
    errors=[];code=0 if name=='test_pass' else 1
    result={'test_pass':'passed','test_fail':'run_failed','test_timeout':'timed_out'}[name]
    if facts.get('schema')!='toka.test-report' or type(facts.get('version')) is not int or facts['version']!=1 or facts.get('finalized') is not True or type(facts.get('report_exit_code')) is not int or facts['report_exit_code']!=code or facts.get('test_id')!='tests/basic_test.tk' or facts.get('test_result')!=result or type(facts.get('test_count')) is not int or facts['test_count']!=1:errors.append('test report identity/result is invalid: '+name)
    if facts.get('report_result')!=('passed' if code==0 else 'failed'):errors.append('test report aggregate result does not match: '+name)
    summary=facts.get('summary');expected_summary={'total':1,'passed':1 if code==0 else 0,'failed':0 if code==0 else 1,'infrastructure_error':0,'interrupted':0,'not_run':0}
    if not isinstance(summary,dict) or any(type(summary.get(k)) is not int or summary[k]!=v for k,v in expected_summary.items()):errors.append('test summary is not the exact single result: '+name)
    compile=facts.get('compile_link');run=facts.get('run')
    if not isinstance(compile,dict) or compile.get('state')!='completed' or type(compile.get('exit_code')) is not int or compile['exit_code']!=0 or compile.get('signal') is not None or compile.get('os_error') is not None or not isinstance(compile.get('process'),dict) or not positive_integer(compile['process'].get('leader_pid')):errors.append('compile/link was not successful: '+name)
    if not isinstance(run,dict) or not isinstance(run.get('process'),dict) or not positive_integer(run['process'].get('leader_pid')):return errors+['run was never started: '+name]
    if name=='test_timeout':
        if run.get('state')!='aborted' or run.get('exit_code') is not None or not positive_integer(run.get('signal')) or facts.get('trigger')!='timeout':errors.append('timeout did not occur during running test')
    elif run.get('state')!='completed' or type(run.get('exit_code')) is not int or run['exit_code']!=(7 if name=='test_fail' else 0) or run.get('signal') is not None or facts.get('trigger')!='none':errors.append('run did not produce the expected raw exit: '+name)
    if run.get('os_error') is not None:errors.append('run infrastructure error: '+name)
    cleanup=facts.get('cleanup')
    if not isinstance(cleanup,dict) or cleanup.get('status')!='confirmed' or any(cleanup.get(k) is not True for k in ('leader_reaped','group_absent','output_complete')):errors.append('test cleanup was not confirmed: '+name)
    nodes=facts.get('dependency_nodes')
    if not isinstance(nodes,list) or len(nodes)!=1 or not isinstance(nodes[0],dict) or nodes[0].get('alias')!='basic_dep' or nodes[0].get('package_node_id')!=dependency.get('package_node_id') or nodes[0].get('content_sha256')!=dependency_digest() or facts.get('lock_sha256')!=dependency.get('lock_sha256_before'):errors.append('test does not confirm the locked dependency identity: '+name)
    entry=dependency.get('lock_entry')
    if isinstance(nodes,list) and len(nodes)==1 and isinstance(nodes[0],dict) and isinstance(entry,list) and len(entry)==8:
        if any(nodes[0].get(k)!=v for k,v in zip(('kind','locator','resolved','archive_sha256','content_sha256'),entry[2:7])):errors.append('test dependency lock fields do not match: '+name)
    return errors
DIGEST = re.compile(r'[0-9a-f]{64}\Z')
SHA = re.compile(r'[0-9a-f]{40}\Z')


def modern(label):
    if label.startswith(('v0.12.', 'v0.13.')) and not re.fullmatch(r'v0\.(?:12|13)\.(0|[1-9][0-9]*)', label):
        raise ValueError('noncanonical three-core release label')
    return bool(re.fullmatch(r'v0\.(?:12|13)\.(0|[1-9][0-9]*)',label))


def policy_id(label):
    return 'toka.release-platforms.0.13.v1' if label.startswith('v0.13.') else POLICY


def core_targets(label):
    return CORE if modern(label) else LEGACY


def positive_integer(value):
    return isinstance(value,int) and not isinstance(value,bool) and value>0


def not_run(revision,label):
    return {'target':OPTIONAL,'status':'not_run','reason':'Independent Intel validation has not been included in this core qualification.',
            'candidate_revision':revision,'version_label':label,'policy_id':policy_id(label),
            'validation_level':None,'receipt':None}


def optional_errors(state,revision,label,digest=None):
    errors=[]
    if not isinstance(state,dict):return ['optional target status is missing']
    if state.get('target')!=OPTIONAL or state.get('status') not in STATES or \
            state.get('candidate_revision')!=revision or state.get('version_label')!=label or state.get('policy_id')!=policy_id(label):
        errors.append('optional target identity/status/policy does not match')
    if not isinstance(state.get('reason'),str) or not state['reason'].strip():errors.append('optional target reason is missing')
    if state.get('status')!='passed':
        if state.get('receipt') is not None or state.get('validation_level') is not None or digest is not None:
            errors.append('unvalidated optional target cannot provide an asset or receipt')
        return errors
    receipt=state.get('receipt')
    if state.get('validation_level') not in ('basic','full') or not isinstance(receipt,dict):
        return errors+['optional passed target has no basic/full receipt']
    if receipt.get('schema')!='toka.sdk-basic-validation' or receipt.get('version')!=1 or receipt.get('result')!='pass' or \
            receipt.get('target')!=OPTIONAL or receipt.get('policy_id')!=policy_id(label) or \
            receipt.get('candidate_revision')!=revision or receipt.get('version_label')!=label or receipt.get('source_dirty') is not False or \
            not positive_integer(receipt.get('source_run_id')) or not positive_integer(receipt.get('source_run_attempt')) or \
            not isinstance(receipt.get('archive_sha256'),str) or not DIGEST.fullmatch(receipt['archive_sha256']):
        errors.append('optional validation receipt does not bind this release/package/run attempt')
    if digest is not None and receipt.get('archive_sha256')!=digest:errors.append('optional receipt archive digest does not match')
    sdk=receipt.get('sdk_identity',{})
    if not isinstance(sdk,dict) or sdk.get('preview_composition') is not False or sdk.get('version_label')!=label or sdk.get('candidate_revision')!=revision:
        errors.append('optional SDK identity is not the actual release candidate')
    tools=sdk.get('tools') if isinstance(sdk,dict) else None
    if not isinstance(tools,dict) or set(tools)!=set(TOOLS):errors.append('all four tool version facts are required')
    else:
        for name,fact in tools.items():
            if not isinstance(fact,dict) or fact.get('version')!=label.removeprefix('v') or type(fact.get('exit_code')) is not int or fact['exit_code']!=0 or any(not isinstance(fact.get(k),str) or not DIGEST.fullmatch(fact[k]) for k in ('sha256','stdout_sha256','stderr_sha256')):errors.append('tool version/identity does not match: '+name)
    dependency=receipt.get('dependencies',{})
    errors.extend(dependency_errors(dependency))
    checks=receipt.get('checks')
    if not isinstance(checks,list) or len(checks)!=len(CHECKS) or any(not isinstance(c,dict) for c in checks) or tuple(c.get('name') for c in checks)!=CHECKS or any(c.get('result')!='pass' for c in checks):
        return errors+['optional basic validation checks are incomplete']
    for check in checks:
        expected=1 if check['name'] in ('test_fail','test_timeout') else 0
        if isinstance(check.get('exit_code'),bool) or check.get('exit_code')!=expected:errors.append('optional check exit code mismatch: '+check['name'])
        if check['name']=='test_timeout' and (check.get('trigger')!='timeout' or check.get('cleanup')!='confirmed'):
            errors.append('optional timeout did not confirm cleanup')
        if check['name'] in ('test_pass','test_fail','test_timeout'):
            errors.extend(test_fact_errors(check.get('facts'),check['name'],dependency if isinstance(dependency,dict) else {}))
    if state.get('validation_level')=='full':
        import verify_release_qualification as qualification
        full=receipt.get('full_qualification')
        if not isinstance(full,dict) or full.get('target')!=OPTIONAL or qualification.report_errors(full,revision,label):
            errors.append('optional full label has no complete release-gate proof')
        contract=receipt.get('taskhandle_conformance')
        restricted=receipt.get('restricted_cancellation_conformance')
        if not isinstance(contract,dict) or qualification.conformance_errors(contract,OPTIONAL,revision):errors.append('optional full label lacks TaskHandle conformance')
        if not isinstance(restricted,dict) or qualification.restricted_cancellation_errors(restricted,OPTIONAL,revision,json.loads(qualification.PROFILE_PATH.read_text())):errors.append('optional full label lacks cancellation conformance')
    return errors


def summary_errors(summary,revision,label):
    errors=[]
    if summary.get('schema')!='toka.release-qualification-summary' or summary.get('version')!=2 or \
            summary.get('result')!='pass' or summary.get('errors')!=[] or summary.get('candidate_revision')!=revision or summary.get('version_label')!=label or \
            summary.get('policy_id')!=policy_id(label) or summary.get('expected_core_targets')!=list(CORE) or summary.get('expected_targets')!=list(CORE) or \
            not positive_integer(summary.get('source_run_id')) or not positive_integer(summary.get('source_run_attempt')):
        errors.append('three-core qualification identity/policy/run attempt does not match')
    for key,result in [('reports','pass'),('taskhandle_conformance','pass'),('restricted_cancellation_conformance','candidate-pass')]:
        rows=summary.get(key)
        if not isinstance(rows,list) or len(rows)!=len(CORE) or any(not isinstance(row,dict) or row.get('result')!=result for row in rows) or {row.get('target') for row in rows if isinstance(row,dict)}!=set(CORE):
            errors.append('core qualification evidence is incomplete: '+key)
    if label.startswith('v0.13.'):
        for row in summary.get('reports',[]):
            control=row.get('candidate_013',{}) or {}
            if control.get('schema')!='toka.0.13-candidate-controls' or control.get('result')!='pass' or control.get('candidate_revision')!=revision or control.get('version_label')!=label or control.get('build_testing') is not False or control.get('groups')!=['A1','B1','B1-boundaries','B1-relative','D1-D2']:
                errors.append('0.13 installed feature proof missing: '+str(row.get('target')))
    optional=summary.get('optional_targets')
    if not isinstance(optional,dict) or set(optional)!={OPTIONAL}:errors.append('optional target status must be explicit')
    else:errors+=optional_errors(optional[OPTIONAL],revision,label)
    return errors


def included_targets(summary,revision,label):
    errors=summary_errors(summary,revision,label)
    if errors:raise ValueError('; '.join(errors))
    return CORE+(OPTIONAL,) if summary['optional_targets'][OPTIONAL]['status']=='passed' else CORE


def archive_names(label,targets):
    return tuple('toka-%s-%s.tar.gz'%(label,target) for target in targets)
