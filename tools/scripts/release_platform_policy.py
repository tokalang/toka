"""Version-bound platform contracts. Never relax a historical qualification."""
import re
import json

LEGACY = ('linux-x64','linux-arm64','macos-x64','macos-arm64')
CORE = ('linux-x64','linux-arm64','macos-arm64')
OPTIONAL = 'macos-x64'
POLICY = 'toka.release-platforms.0.12.v1'
STATES = ('not_run','running','passed','failed')
CHECKS = ('install','versions','create','compile_link','run','test_pass','test_fail','test_timeout')
DIGEST = re.compile(r'[0-9a-f]{64}\Z')
SHA = re.compile(r'[0-9a-f]{40}\Z')


def modern(label):
    if label.startswith('v0.12.') and not re.fullmatch(r'v0\.12\.(0|[1-9][0-9]*)', label):
        raise ValueError('noncanonical 0.12 release label')
    return bool(re.fullmatch(r'v0\.12\.(0|[1-9][0-9]*)',label))


def core_targets(label):
    return CORE if modern(label) else LEGACY


def positive_integer(value):
    return isinstance(value,int) and not isinstance(value,bool) and value>0


def not_run(revision,label):
    return {'target':OPTIONAL,'status':'not_run','reason':'Independent Intel validation has not been included in this core qualification.',
            'candidate_revision':revision,'version_label':label,'policy_id':POLICY,
            'validation_level':None,'receipt':None}


def optional_errors(state,revision,label,digest=None):
    errors=[]
    if not isinstance(state,dict):return ['optional target status is missing']
    if state.get('target')!=OPTIONAL or state.get('status') not in STATES or \
            state.get('candidate_revision')!=revision or state.get('version_label')!=label or state.get('policy_id')!=POLICY:
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
            receipt.get('target')!=OPTIONAL or receipt.get('policy_id')!=POLICY or \
            receipt.get('candidate_revision')!=revision or receipt.get('version_label')!=label or receipt.get('source_dirty') is not False or \
            not positive_integer(receipt.get('source_run_id')) or not positive_integer(receipt.get('source_run_attempt')) or \
            not isinstance(receipt.get('archive_sha256'),str) or not DIGEST.fullmatch(receipt['archive_sha256']):
        errors.append('optional validation receipt does not bind this release/package/run attempt')
    if digest is not None and receipt.get('archive_sha256')!=digest:errors.append('optional receipt archive digest does not match')
    sdk=receipt.get('sdk_identity',{})
    if not isinstance(sdk,dict) or sdk.get('preview_composition') is not False or sdk.get('version_label')!=label or sdk.get('candidate_revision')!=revision:
        errors.append('optional SDK identity is not the actual release candidate')
    checks=receipt.get('checks')
    if not isinstance(checks,list) or len(checks)!=len(CHECKS) or any(not isinstance(c,dict) for c in checks) or tuple(c.get('name') for c in checks)!=CHECKS or any(c.get('result')!='pass' for c in checks):
        return errors+['optional basic validation checks are incomplete']
    for check in checks:
        expected=1 if check['name'] in ('test_fail','test_timeout') else 0
        if isinstance(check.get('exit_code'),bool) or check.get('exit_code')!=expected:errors.append('optional check exit code mismatch: '+check['name'])
        if check['name']=='test_timeout' and (check.get('trigger')!='timeout' or check.get('cleanup')!='confirmed'):
            errors.append('optional timeout did not confirm cleanup')
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
            summary.get('policy_id')!=POLICY or summary.get('expected_core_targets')!=list(CORE) or summary.get('expected_targets')!=list(CORE) or \
            not positive_integer(summary.get('source_run_id')) or not positive_integer(summary.get('source_run_attempt')):
        errors.append('three-core qualification identity/policy/run attempt does not match')
    for key,result in [('reports','pass'),('taskhandle_conformance','pass'),('restricted_cancellation_conformance','candidate-pass')]:
        rows=summary.get(key)
        if not isinstance(rows,list) or len(rows)!=len(CORE) or any(not isinstance(row,dict) or row.get('result')!=result for row in rows) or {row.get('target') for row in rows if isinstance(row,dict)}!=set(CORE):
            errors.append('core qualification evidence is incomplete: '+key)
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
