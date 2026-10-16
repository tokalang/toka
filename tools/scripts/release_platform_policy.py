"""Version-bound platform contracts. Never relax a historical qualification."""
from pathlib import Path
import re
import json
import hashlib
import math
import os

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
    if label.startswith(('v0.12.', 'v0.13.', 'v0.14.', 'v0.15.', 'v0.16.', 'v0.17.')) and not re.fullmatch(r'v0\.(?:12|13|14|15|16|17)\.(0|[1-9][0-9]*)', label):
        raise ValueError('noncanonical three-core release label')
    return bool(re.fullmatch(r'v0\.(?:12|13|14|15|16|17)\.(0|[1-9][0-9]*)',label))


def policy_id(label):
    if label.startswith('v0.17.'):
        return 'toka.release-platforms.0.17.v1'
    if label.startswith('v0.16.'):
        return 'toka.release-platforms.0.16.v1'
    if label.startswith('v0.15.'):
        return 'toka.release-platforms.0.15.v1'
    if label.startswith('v0.14.'):
        return 'toka.release-platforms.0.14.v1'
    return 'toka.release-platforms.0.13.v1' if label.startswith('v0.13.') else POLICY


def core_targets(label):
    if not re.fullmatch(r'v0\.(?:11|12|13|14|15|16|17)\.(0|[1-9][0-9]*)',label):
        raise ValueError('unsupported or noncanonical release label')
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


EXPECTED_R3_R5_FIXTURES_DIGEST = 'dd7315ca33ee61a8303b392c57834178da27dcccd52d4a5ad9d812dba56ebd90'
AUTHORITATIVE_R3_R5_FIXTURES = {
    'fail/r3_task_direct_escape.tk': '130fd9d55b2352fa73d9053a31853af312cf344348fdd342b18b51c76071a231',
    'fail/r3_task_explicit_dep_free_escape.tk': 'e047b184e92371772cdca9bc4fe4af79ad312b6fa628c963e1c56fd1bc882759',
    'fail/r3_task_explicit_dep_local_escape.tk': 'd257ac12aa77155b1c3bf8b2f375d26d2ccdee39df95d6f415f28c95e84f3c24',
    'fail/r3_task_helper_annotated_escape.tk': '990ad6557dc597289263212bfb9d2425c654ca6333b79de96a6e391223b7fcdc',
    'fail/r3_task_helper_unannotated_escape.tk': 'c8fd1e53ce6f2bad471ca8be35fb809c06883e429c4ec2f1af2e567e4d76de15',
    'fail/r5_private_shape_rejected.tk': '7f894a9368b9f8b4e3d3317b86e555cda280f986812a1a4024654c0ce295b7b9',
    'pass/r3_task_explicit_dep_safe_await.tk': 'ffcaa4e742a804be7080e210294c9d44244f651b64e65f74d7b9bdb7db081eb8',
    'pass/r3_task_return_positive.tk': '2d943eec88d84da016b02141bd2348b2d9ef0c80d4ffcf807ec032c8fd21388f',
    'pass/r5_trait_cross_module.tk': '51f9d4d891d2788c313e78a2edca0aba8d8fba69487bfa6e51d45b16c9beec2a',
    'pass/r5_trait_generic_param.tk': 'd24653367390f2277b11586d9ff34223bcb1dd5998b7ecd154db8b447d6d187f',
    'pass/r5_trait_module/mod.tk_lib': 'a341b90504367d05363fa016766a1635209f9ae3e994b7b82566f9466f7ad409',
}
R3_R5_NEGATIVE_TEST_NAMES = ('r3_task_direct_escape', 'r3_task_explicit_dep_local_escape', 'r3_task_helper_unannotated_escape', 'r3_task_explicit_dep_free_escape', 'r3_task_helper_annotated_escape', 'r5_private_shape_rejected')
R3_R5_EXPECTED_DIAGNOSTICS = {
    'r3_task_direct_escape': 'E0455',
    'r3_task_explicit_dep_local_escape': 'E0455',
    'r3_task_helper_unannotated_escape': 'E0454',
    'r3_task_explicit_dep_free_escape': 'E0455',
    'r3_task_helper_annotated_escape': 'E0455',
    'r5_private_shape_rejected': 'E0405',
}
R3_R5_POSITIVE_TEST_NAMES = ('r3_task_return_positive', 'r3_task_explicit_dep_safe_await', 'r5_trait_generic_param', 'r5_trait_cross_module')


REPLAY_BUNDLE_MANIFEST_PATH = Path(__file__).resolve().parent / 'replay_bundle_manifest.json'


def get_bundle_manifest():
    if REPLAY_BUNDLE_MANIFEST_PATH.is_file():
        try:
            return json.loads(REPLAY_BUNDLE_MANIFEST_PATH.read_text(encoding='utf-8'))
        except Exception:
            return None
    return None


def expected_r3_r5_script_sha256():
    m = get_bundle_manifest()
    if m and isinstance(m.get('modules'), dict):
        return m['modules'].get('test_r3_r5_installed.py')
    return None


def r3_r5_receipt_errors(receipt, revision, label, target=None):
    if not isinstance(receipt, dict):
        return ['R3/R5 receipt is missing or not a dict']
    errors = []
    if receipt.get('schema') != 'toka.r3-r5-installed-controls':
        errors.append('R3/R5 receipt schema mismatch')
    version = receipt.get('version')
    if type(version) is not int or isinstance(version, bool) or version != 1:
        errors.append('R3/R5 receipt version must be int 1')
    if receipt.get('result') != 'pass':
        errors.append('R3/R5 receipt result is not pass')
    if receipt.get('candidate_revision') != revision:
        errors.append('R3/R5 candidate revision mismatch')
    if receipt.get('version_label') != label:
        errors.append('R3/R5 version label mismatch')
    if target is not None and receipt.get('target') != target:
        errors.append('R3/R5 target mismatch: expected %s, got %s' % (target, receipt.get('target')))
    if receipt.get('fixtures_digest') != EXPECTED_R3_R5_FIXTURES_DIGEST:
        errors.append('R3/R5 fixtures digest mismatch')

    if receipt.get('control_script_name') != 'test_r3_r5_installed.py':
        errors.append('R3/R5 control script name mismatch')
    control_sha = receipt.get('control_script_sha256')
    expected_control_sha = expected_r3_r5_script_sha256()
    if not isinstance(control_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', control_sha):
        errors.append('R3/R5 control script sha256 missing or invalid')
    elif expected_control_sha and control_sha != expected_control_sha:
        errors.append('R3/R5 control script digest mismatch')

    counts = receipt.get('counts')
    expected_counts = {'total_checks': 22, 'negative_checks': 18, 'positive_checks': 4, 'passed': 22, 'failed': 0}
    if not isinstance(counts, dict) or any(type(counts.get(k)) is not int or isinstance(counts.get(k), bool) or counts[k] != v for k, v in expected_counts.items()):
        errors.append('R3/R5 counts mismatch or invalid types')

    checks = receipt.get('checks')
    if not isinstance(checks, list) or len(checks) != 22:
        errors.append('R3/R5 checks list must have exactly 22 entries')
    else:
        expected_negative_checks = {}
        for name in R3_R5_NEGATIVE_TEST_NAMES:
            expected_negative_checks['neg-%s-check_only' % name] = ('check_only', R3_R5_EXPECTED_DIAGNOSTICS[name], False)
            expected_negative_checks['neg-%s-object' % name] = ('object', R3_R5_EXPECTED_DIAGNOSTICS[name], True)
            expected_negative_checks['neg-%s-ir' % name] = ('ir', R3_R5_EXPECTED_DIAGNOSTICS[name], True)
        expected_positive_checks = {'pos-%s-run' % name: ('positive', 0) for name in R3_R5_POSITIVE_TEST_NAMES}

        seen_checks = set()
        for c in checks:
            if not isinstance(c, dict):
                errors.append('R3/R5 check item is not a dict')
                continue
            c_name = c.get('name')
            if not isinstance(c_name, str):
                errors.append('R3/R5 check name invalid')
                continue
            seen_checks.add(c_name)
            if c.get('result') != 'pass':
                errors.append('R3/R5 check result is not pass: ' + c_name)
            if c_name in expected_negative_checks:
                exp_mode, exp_diag, req_artifact_absent = expected_negative_checks[c_name]
                if c.get('mode') != exp_mode or c.get('diagnostic') != exp_diag:
                    errors.append('R3/R5 negative check mode or diagnostic mismatch: ' + c_name)
                if req_artifact_absent and c.get('artifact_absent') is not True:
                    errors.append('R3/R5 negative check did not confirm artifact absence: ' + c_name)
            elif c_name in expected_positive_checks:
                if type(c.get('exit_code')) is not int or isinstance(c.get('exit_code'), bool) or c['exit_code'] != 0:
                    errors.append('R3/R5 positive check exit code mismatch: ' + c_name)
            else:
                errors.append('Unexpected R3/R5 check: ' + c_name)

        missing = (set(expected_negative_checks) | set(expected_positive_checks)) - seen_checks
        if missing:
            errors.append('Missing R3/R5 checks: ' + ', '.join(sorted(missing)))

    receipts = receipt.get('receipts')
    if not isinstance(receipts, list) or len(receipts) != 26:
        errors.append('R3/R5 command receipts list must have exactly 26 entries')
    else:
        expected_receipt_names = set()
        for name in R3_R5_NEGATIVE_TEST_NAMES:
            expected_receipt_names.add('neg-%s-check_only' % name)
            expected_receipt_names.add('neg-%s-object' % name)
            expected_receipt_names.add('neg-%s-ir' % name)
        for name in R3_R5_POSITIVE_TEST_NAMES:
            expected_receipt_names.add('pos-%s-compile' % name)
            expected_receipt_names.add('pos-%s-run' % name)

        seen_receipts = set()
        for r in receipts:
            if not isinstance(r, dict):
                errors.append('R3/R5 command receipt is not a dict')
                continue
            r_name = str(r.get('name', ''))
            seen_receipts.add(r_name)

            if 'signal' not in r or r['signal'] is not None:
                errors.append('R3/R5 command receipt missing signal key or abnormal signal: ' + r_name)

            if r.get('termination') is not None or r.get('error') is not None:
                errors.append('R3/R5 command receipt indicates abnormal termination or error: ' + r_name)

            argv = r.get('argv')
            if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
                errors.append('R3/R5 command receipt argv invalid: ' + r_name)
                continue

            cwd = r.get('cwd')
            if not isinstance(cwd, str) or not cwd:
                errors.append('R3/R5 command receipt cwd missing or invalid: ' + r_name)

            ms = r.get('execution_ms')
            if type(ms) not in (int, float) or isinstance(ms, bool) or ms <= 0:
                errors.append('R3/R5 command receipt execution_ms missing or invalid: ' + r_name)

            if r_name.startswith('neg-'):
                if type(r.get('exit_code')) is not int or isinstance(r.get('exit_code'), bool) or r['exit_code'] != 1:
                    errors.append('R3/R5 negative command exit code must be 1: ' + r_name)
                parts = r_name.split('-')
                if len(parts) == 3:
                    _, test_name, mode = parts
                    prog = argv[0].replace('\\', '/')
                    if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                        errors.append('R3/R5 negative command must execute tokac: ' + r_name)
                    if not any(arg.endswith('/' + test_name + '.tk') or arg == (test_name + '.tk') for arg in argv):
                        errors.append('R3/R5 command does not match named fixture: ' + r_name)
                    if mode == 'check_only':
                        if '--check-only' not in argv:
                            errors.append('R3/R5 check_only command missing --check-only: ' + r_name)
                    elif mode == 'object':
                        if '-c' not in argv or '-o' not in argv:
                            errors.append('R3/R5 object command missing -c or -o: ' + r_name)
                        else:
                            idx = argv.index('-o')
                            if idx + 1 >= len(argv) or not argv[idx + 1].endswith('.o'):
                                errors.append('R3/R5 object command output is not .o: ' + r_name)
                    elif mode == 'ir':
                        if '--emit-llvm' not in argv or '-o' not in argv:
                            errors.append('R3/R5 ir command missing --emit-llvm or -o: ' + r_name)
                        else:
                            idx = argv.index('-o')
                            if idx + 1 >= len(argv) or not argv[idx + 1].endswith('.ll'):
                                errors.append('R3/R5 ir command output is not .ll: ' + r_name)
                    else:
                        errors.append('Unknown negative mode: ' + r_name)
                else:
                    errors.append('Malformed negative command receipt name: ' + r_name)

            elif r_name.startswith('pos-'):
                if type(r.get('exit_code')) is not int or isinstance(r.get('exit_code'), bool) or r['exit_code'] != 0:
                    errors.append('R3/R5 positive command exit code must be 0: ' + r_name)
                parts = r_name.split('-')
                if len(parts) == 3:
                    _, test_name, action = parts
                    if action == 'compile':
                        prog = argv[0].replace('\\', '/')
                        if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                            errors.append('R3/R5 positive compile command must execute tokac: ' + r_name)
                        if not any(arg.endswith('/' + test_name + '.tk') or arg == (test_name + '.tk') for arg in argv):
                            errors.append('R3/R5 positive compile does not match named fixture: ' + r_name)
                        if '-o' not in argv:
                            errors.append('R3/R5 positive compile command missing -o: ' + r_name)
                    elif action == 'run':
                        prog = argv[0].replace('\\', '/')
                        expected_bin = 'pos-' + test_name + '.bin'
                        expected_exe = 'pos-' + test_name + '.exe'
                        expected_bare = 'pos-' + test_name
                        if not (prog.endswith('/' + expected_bin) or prog == expected_bin or
                                prog.endswith('/' + expected_exe) or prog == expected_exe or
                                prog.endswith('/' + expected_bare) or prog == expected_bare):
                            errors.append('R3/R5 positive run command must execute compiled binary: ' + r_name)
                    else:
                        errors.append('Unknown positive action: ' + r_name)
                else:
                    errors.append('Malformed positive command receipt name: ' + r_name)
            else:
                errors.append('Unknown R3/R5 command receipt: ' + r_name)

        missing_receipts = expected_receipt_names - seen_receipts
        if missing_receipts:
            errors.append('Missing R3/R5 command receipts: ' + ', '.join(sorted(missing_receipts)))

    return errors


EXPECTED_014_FIXTURES_DIGEST = 'e5615185f9ec1b297a34b4d07dca868ef7f2ef440736ad05cd3e7bfbe1f24df1'
AUTHORITATIVE_014_FIXTURES = {
    'g1/fail/dyn_trait_async_param.tk': '14ed468697dcc14ac4839d5acf0d588fb113f3d5ae1a457b01fe7aba7d5dfaca',
    'g1/fail/trait_async_plain_self.tk': '3787c6308e638270a618f1880397856370bd842bd102c6dd2daaddc64a7cb4d9',
    'g1/g14_static_async_trait_lifecycle.tk': '2099a9947e09f0ee0a3981b0e6aed3e543d033564c74e587bc24ca706ba93061',
    'g1/sourceless/main.tk': '59ca0992069c05324823c07e5b8baa670ad74257f0bb4effdbf5dc4d4547722c',
    'g1/sourceless/mod_a.tk': '788d9cb2f8cfb6a0b2acdf1cadc296daeeba40f0895c9864f367fc90d7d21ee9',
    'g1/sourceless/mod_b.tk': 'dd3c9e1607ad14bb586338712636acabf8c7b0b9894b17a2bd0c4dc44c2ae4c3',
    'g2/closed_empty.tk': '7271c92f2b4fb4328e0da98c5f44c924490cfb5b08dd53c26c8949bd4f0f86a2',
    'g2/framed_reader_read_short_buffer_1.tk': '0537f2196b96a8e7da43f2666e57aa33ca7e491de17d3a8f61d6c3c057cc1f73',
    'g2/framed_reader_read_short_buffer_2.tk': '6056873ab8f4ff038e21127e47af2abd46deefc4e0b709189ac3e7ad460999cc',
    'g2/framed_stream_cancel.tk': '71e1f756894db747851dd13953dccd726e4f0a31285552f11b7c407ba94d0984',
    'g2/framed_stream_ioerror.tk': 'eccdf17a91db0eafe28be6ec75ef92af90439e735e840cfc3beb4dbcfad91681',
    'g2/framed_stream_overreported_count.tk': 'ae92c54e646311551af29c1f1bee436843a66466107a08b309fda6de305d1930',
    'g2/framed_stream_partial.tk': '73cb1aa9c20e00c572109c1d48fb7683adfc7a3e3d479d23a3a00e7e3a70c0bc',
    'g2/framed_stream_read_short_buffer_1.tk': 'b28d726ad8bef9e69bd0a8a959504c8a98f329689f4adc4f03266a3e9f52297b',
    'g2/framed_stream_read_short_buffer_2.tk': '139f944999e7cdac7fa9bdabce319d8029d32aa6f9cfac38e95aa78c49928ce3',
    'g2/framed_stream_zero.tk': '02840a8c384cba12da590993f1f91944128e4aa46ad4f26ca3784b2e0d53e198',
    'g2/framed_writer_cancel.tk': '75c422867acc5c3cc8a53fff3a763c4107e4c1758e911ffcbf3ae45ebd86dfeb',
    'g2/framed_writer_ioerror.tk': 'e9e9b16079d6ad54c1ba5172e532e683801aecbfd037787daa8555c00053a11d',
    'g2/framed_writer_overreported_count.tk': '274b375439f39b42e2abd18d832a421669b2dfe2d1db2a1dbf555e63c6eee448',
    'g2/framed_writer_partial.tk': '1f48b40c31d6a9a7f2b30ccd75dabc5140ed2c0dc05af2dbf1236141b2939f48',
    'g2/framed_writer_zero.tk': '0e55418fbabd36d69dc03c120b6cc48b89cb9699f01cfb5dc0f332037f5a5398',
    'g2/given_11_branch.tk': '63cc151d36caf8049e30f8a7f21c5431b802180535f292a2cfbeea27f123d872',
    'g2/lifecycle_pass_suite.tk': '54ad3b5dc53ef3abef791cc850a324e4e77ad8a2b2a2dbd21b2ce55f42c27630',
    'g2/read_cancel.tk': 'dea2f69a216d824c1c467bb85cdd0b1293706e0e5135cb05addd94bcb05f4ed7',
    'g3/fail/unwrap_unsupported_mangled_multi_payload_option.tk': '7998fa366bfbf8dd2a93c90e8f9a318c31403afbc4b5bfd51a9c9dd4841b59b3',
    'g3/fail/unwrap_unsupported_mangled_multi_payload_result.tk': '13988a9822bbf1e9d95d6b2859b2fc10bc73f89341563c0b706f7787e28e83f1',
    'g3/fail/unwrap_unsupported_prefix_extra_tag.tk': '8064ad85a699b12a134b021220da611c9b21789c42c5dad14f0108babf95b85d',
    'g3/fail/unwrap_unsupported_prefix_wrong_tag.tk': '0babc344c184b722af10e1d1c979aad94473b703093367b67612a6ab6640e57a',
    'g3/fail/unwrap_unsupported_result_prefix_wrong_tag.tk': '10b547340486a7553772c7c8844aac33634e33e582f931175b58ed441a20b490',
    'g3/pass/enum_tag_255_valid_test.tk': '19c979ad14fbe59fd3616d35051d162d98bb3dff0b77a7f42312b8bd063a2ab5',
    'g3/pass/g14_unwrap_propagation_lifecycle_test.tk': '2a0393f8cc57c85b21a731ca34eae3ec2b9d7a73c473c911e17988538c53fd5a',
    'g3/pass/unwrap_aggregate_payload_test.tk': 'fa28999a9268d00f48d4d1deed9297515d58415092bbd21da6aff598fcd82319',
}
G2_014_CASES = (
    'closed_empty', 'framed_reader_read_short_buffer_1', 'framed_reader_read_short_buffer_2',
    'framed_stream_cancel', 'framed_stream_ioerror', 'framed_stream_overreported_count',
    'framed_stream_partial', 'framed_stream_read_short_buffer_1', 'framed_stream_read_short_buffer_2',
    'framed_stream_zero', 'framed_writer_cancel', 'framed_writer_ioerror',
    'framed_writer_overreported_count', 'framed_writer_partial', 'framed_writer_zero',
    'given_11_branch', 'lifecycle_pass_suite', 'read_cancel',
)
G2_EXPECTED_FACTS = {
    'closed_empty': [
        'closed-empty write ok=false',
    ],
    'framed_reader_read_short_buffer_1': [
        'begin reported count within need but buffer len zero',
        'TRANSPORT CLOSED',
        'returned Err=true,state=5',
    ],
    'framed_reader_read_short_buffer_2': [
        'begin reported count within need but buffer len zero',
        'TRANSPORT CLOSED',
        'returned Err=true,state=5',
    ],
    'framed_stream_cancel': [
        'outer outcome None; state=5, closed=true, frames=0',
    ],
    'framed_stream_ioerror': [
        'ok=false, wire=4, calls=2, frames=0, state=5, closed=true',
    ],
    'framed_stream_overreported_count': [
        'ok=false, wire=1, calls=1, frames=0, state=5, closed=true',
    ],
    'framed_stream_partial': [
        'ok=true, wire=7, calls=7, frames=1, state=0, closed=false',
        'reported bytes=3, returned len=3',
    ],
    'framed_stream_read_short_buffer_1': [
        'begin reported count within need but buffer len zero',
        'TRANSPORT CLOSED',
        'returned Err=true,state=5',
    ],
    'framed_stream_read_short_buffer_2': [
        'begin reported count within need but buffer len zero',
        'TRANSPORT CLOSED',
        'returned Err=true,state=5',
    ],
    'framed_stream_zero': [
        'ok=false, wire=0, calls=16, frames=0, state=5, closed=true',
    ],
    'framed_writer_cancel': [
        'outer outcome None; state=5, closed=true, frames=0',
    ],
    'framed_writer_ioerror': [
        'ok=false, wire=4, calls=2, frames=0, state=5, closed=true',
    ],
    'framed_writer_overreported_count': [
        'ok=false, wire=1, calls=1, frames=0, state=5, closed=true',
    ],
    'framed_writer_partial': [
        'ok=true, wire=7, calls=7, frames=1, state=0, closed=false',
        'reported bytes=3, returned len=3',
    ],
    'framed_writer_zero': [
        'ok=false, wire=0, calls=16, frames=0, state=5, closed=true',
    ],
    'given_11_branch': [
        'All 11 G2 framed transport verification branches passed successfully!',
    ],
    'lifecycle_pass_suite': [
        'All 10 lifecycle and cancellation controls passed with zero TCB residue and exact-once drop!',
    ],
    'read_cancel': [
        'None=true, state=5, closed=true, waits=0',
    ],
}
G3_014_NEG_CASES = (
    'unwrap_unsupported_prefix_wrong_tag',
    'unwrap_unsupported_prefix_extra_tag',
    'unwrap_unsupported_result_prefix_wrong_tag',
    'unwrap_unsupported_mangled_multi_payload_option',
    'unwrap_unsupported_mangled_multi_payload_result',
)


def expected_014_script_sha256():
    m = get_bundle_manifest()
    if m and isinstance(m.get('modules'), dict):
        return m['modules'].get('test_0_14_candidate.py')
    return None


def candidate_014_receipt_errors(receipt, revision, label, target=None):
    if not isinstance(receipt, dict):
        return ['0.14 candidate receipt is missing or not a dict']
    errors = []
    if receipt.get('schema') != 'toka.0.14-candidate-controls':
        errors.append('0.14 receipt schema mismatch')
    version = receipt.get('version')
    if type(version) is not int or isinstance(version, bool) or version != 1:
        errors.append('0.14 receipt version must be int 1')
    if receipt.get('result') != 'pass':
        errors.append('0.14 receipt result is not pass')
    if receipt.get('candidate_revision') != revision:
        errors.append('0.14 candidate revision mismatch')
    if receipt.get('version_label') != label:
        errors.append('0.14 version label mismatch')
    target_val = receipt.get('target')
    if not isinstance(target_val, str) or not target_val:
        errors.append('0.14 target missing or invalid')
    elif target is not None and target_val != target:
        errors.append('0.14 target mismatch: expected %s, got %s' % (target, target_val))
    if receipt.get('build_testing') is not False:
        errors.append('0.14 receipt build_testing must be False')
    if receipt.get('groups') != ['G1', 'G2', 'G3-unwrap']:
        errors.append('0.14 receipt groups mismatch')
    if receipt.get('fixtures_digest') != EXPECTED_014_FIXTURES_DIGEST:
        errors.append('0.14 fixtures digest mismatch')

    if receipt.get('control_script_name') != 'test_0_14_candidate.py':
        errors.append('0.14 control script name mismatch')
    control_sha = receipt.get('control_script_sha256')
    expected_control_sha = expected_014_script_sha256()
    if not isinstance(control_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', control_sha):
        errors.append('0.14 control script sha256 missing or invalid')
    elif expected_control_sha and control_sha != expected_control_sha:
        errors.append('0.14 control script digest mismatch')

    tools = receipt.get('four_tool_versions')
    if not isinstance(tools, dict) or set(tools.keys()) != {'tokac', 'toka', 'tokafmt', 'tokalsp'}:
        errors.append('0.14 four_tool_versions missing or invalid')
    else:
        for t_name, t_str in tools.items():
            if not isinstance(t_str, str) or label.removeprefix('v') not in t_str:
                errors.append('0.14 tool version mismatch: ' + t_name)

    counts = receipt.get('counts')
    expected_groups = {'version': 4, 'G1': 8, 'G2': 36, 'G3-unwrap': 26}
    if not isinstance(counts, dict) or type(counts.get('total_receipts')) is not int or \
            counts.get('total_receipts') != 74 or counts.get('passed') != 74 or counts.get('failed') != 0 or \
            counts.get('groups') != expected_groups:
        errors.append('0.14 counts mismatch or invalid')

    receipts = receipt.get('receipts')
    if not isinstance(receipts, list) or len(receipts) != 74:
        errors.append('0.14 command receipts list must have exactly 74 entries')
        return errors

    expected_receipt_names = set()
    for name in ('tokac', 'toka', 'tokafmt', 'tokalsp'):
        expected_receipt_names.add('version-' + name)
    expected_receipt_names.update([
        'g1-lifecycle-compile', 'g1-lifecycle-run',
        'g1-sourceless-compile-mod-a', 'g1-sourceless-compile-mod-b',
        'g1-sourceless-compile-main', 'g1-sourceless-run-main',
        'g1-reject-dyn-trait', 'g1-reject-plain-self',
    ])
    for case in G2_014_CASES:
        expected_receipt_names.add('g2-compile-' + case)
        expected_receipt_names.add('g2-run-' + case)
    for tag in ('lifecycle', 'aggregate', 'tag255'):
        expected_receipt_names.add('g3-compile-' + tag)
        expected_receipt_names.add('g3-run-' + tag)
    for case in G3_014_NEG_CASES:
        for mode in ('check', 'object', 'ir', 'compile'):
            expected_receipt_names.add(f'g3-neg-{case}-{mode}')

    expected_g3_pos_fixtures = {
        'lifecycle': 'g14_unwrap_propagation_lifecycle_test.tk',
        'aggregate': 'unwrap_aggregate_payload_test.tk',
        'tag255': 'enum_tag_255_valid_test.tk',
    }

    compile_outputs = {}
    for r in receipts:
        if isinstance(r, dict):
            name = str(r.get('name', ''))
            argv = r.get('argv')
            cwd = r.get('cwd', '')
            if isinstance(argv, list) and isinstance(cwd, str) and cwd and '-o' in argv:
                idx = argv.index('-o')
                if idx + 1 < len(argv) and isinstance(argv[idx + 1], str) and argv[idx + 1]:
                    raw_out = argv[idx + 1]
                    norm_out = os.path.normpath(raw_out if os.path.isabs(raw_out) else os.path.join(cwd, raw_out))
                    compile_outputs[name] = norm_out

    run_to_compile = {
        'g1-lifecycle-run': ('g1-lifecycle-compile', 'g1_lifecycle'),
        'g1-sourceless-run-main': ('g1-sourceless-compile-main', 'sourceless_main'),
    }
    for case in G2_014_CASES:
        run_to_compile[f'g2-run-{case}'] = (f'g2-compile-{case}', case)
    for tag in ('lifecycle', 'aggregate', 'tag255'):
        run_to_compile[f'g3-run-{tag}'] = (f'g3-compile-{tag}', f'g3_{tag}')

    compile_expected_bases = {
        'g1-lifecycle-compile': ('g1_lifecycle.bin', 'g1_lifecycle.exe', 'g1_lifecycle'),
        'g1-sourceless-compile-mod-a': ('mod_a.o',),
        'g1-sourceless-compile-mod-b': ('mod_b.o',),
        'g1-sourceless-compile-main': ('sourceless_main.bin', 'sourceless_main.exe', 'sourceless_main'),
    }
    for case in G2_014_CASES:
        compile_expected_bases[f'g2-compile-{case}'] = (f'{case}.bin', f'{case}.exe', case)
    for tag in ('lifecycle', 'aggregate', 'tag255'):
        compile_expected_bases[f'g3-compile-{tag}'] = (f'g3_{tag}.bin', f'g3_{tag}.exe', f'g3_{tag}')
    for case in G3_014_NEG_CASES:
        compile_expected_bases[f'g3-neg-{case}-object'] = (f'{case}.o',)
        compile_expected_bases[f'g3-neg-{case}-ir'] = (f'{case}.ll',)
        compile_expected_bases[f'g3-neg-{case}-compile'] = (f'{case}.bin', f'{case}.exe', case)

    seen_receipts = set()
    for r in receipts:
        if not isinstance(r, dict):
            errors.append('0.14 command receipt is not a dict')
            continue
        r_name = str(r.get('name', ''))
        seen_receipts.add(r_name)

        if r.get('result') != 'pass':
            errors.append('0.14 command receipt result is not pass: ' + r_name)

        if 'signal' not in r or r['signal'] is not None:
            errors.append('0.14 command receipt missing signal key or abnormal signal: ' + r_name)
        if r.get('termination') is not None or r.get('error') is not None:
            errors.append('0.14 command receipt indicates abnormal termination or error: ' + r_name)

        argv = r.get('argv')
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
            errors.append('0.14 command receipt argv invalid: ' + r_name)
            continue

        cwd = r.get('cwd')
        if not isinstance(cwd, str) or not cwd:
            errors.append('0.14 command receipt cwd missing or invalid: ' + r_name)
            cwd = ''

        prog = argv[0].replace('\\', '/')
        if prog in ('/bin/true', '/bin/false', 'true', 'false') or prog.endswith('/true') or prog.endswith('/false'):
            errors.append('0.14 command receipt program cannot be no-op dummy: ' + r_name)

        norm_prog = os.path.normpath(prog if os.path.isabs(prog) else os.path.join(cwd, prog))
        prog_base = os.path.basename(norm_prog)

        ms = r.get('execution_ms')
        if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
            errors.append('0.14 command receipt execution_ms missing or invalid: ' + r_name)

        exit_code = r.get('exit_code')
        if type(exit_code) is not int or isinstance(exit_code, bool):
            errors.append('0.14 command receipt exit_code missing or not int: ' + r_name)
            continue

        stdout_val = r.get('stdout')
        if not isinstance(stdout_val, str):
            errors.append('0.14 command receipt stdout missing or not a string: ' + r_name)
            out_s = ''
        else:
            out_s = stdout_val

        stderr_val = r.get('stderr')
        if not isinstance(stderr_val, str):
            errors.append('0.14 command receipt stderr missing or not a string: ' + r_name)
            err_s = ''
        else:
            err_s = stderr_val

        if 'failure instead of assertions' in out_s or 'assertion failed' in out_s.lower():
            errors.append('0.14 command receipt stdout indicates failure: ' + r_name)

        is_neg = (r_name in ('g1-reject-dyn-trait', 'g1-reject-plain-self') or r_name.startswith('g3-neg-'))

        if not is_neg:
            if r.get('diagnostic') is not None:
                errors.append('0.14 unexpected diagnostic on positive command: ' + r_name)
            if err_s.strip() and 'warning' not in err_s.lower():
                errors.append('0.14 unexpected stderr in positive command: ' + r_name)

        # Check compile commands for expected -o output basename
        if r_name in compile_expected_bases:
            if '-o' not in argv:
                errors.append(f'0.14 compile command missing -o: {r_name}')
            else:
                out_path = compile_outputs.get(r_name)
                if not out_path:
                    errors.append(f'0.14 compile command missing output path after -o: {r_name}')
                else:
                    out_base = os.path.basename(out_path)
                    if out_base not in compile_expected_bases[r_name]:
                        errors.append(f'0.14 compile command output basename mismatch: {r_name} (got {out_base}, expected one of {compile_expected_bases[r_name]})')

        # Check run commands for exact match with corresponding compile output
        if r_name in run_to_compile:
            comp_name, exp_base = run_to_compile[r_name]
            expected_compiled_out = compile_outputs.get(comp_name)
            if not expected_compiled_out:
                errors.append(f'0.14 missing matching compile output for run command: {r_name} (needs {comp_name})')
            elif norm_prog != expected_compiled_out:
                errors.append(f'0.14 run program does not match compile output: {r_name} (executes {norm_prog}, compiled {expected_compiled_out})')
            valid_bases = (exp_base, exp_base + '.bin', exp_base + '.exe')
            if prog_base not in valid_bases:
                errors.append(f'0.14 run program basename mismatch: {r_name} (got {prog_base}, expected one of {valid_bases})')

        # Per-command specific assertions
        if r_name.startswith('version-'):
            if exit_code != 0:
                errors.append('0.14 tool version command exit code must be 0: ' + r_name)
            t_name = r_name.removeprefix('version-')
            if t_name == 'toka':
                if prog_base not in ('toka', 'toka.exe'):
                    errors.append('0.14 tool version command program mismatch: ' + r_name)
            else:
                if prog_base not in (t_name, t_name + '.exe'):
                    errors.append('0.14 tool version command program mismatch: ' + r_name)
            ver = label.removeprefix('v')
            if ver not in out_s:
                errors.append(f'0.14 tool version {t_name} stdout missing {ver}: {r_name}')

        elif r_name == 'g1-lifecycle-compile':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G1 compile command program must execute tokac: ' + r_name)
            if not any(arg.endswith('/g14_static_async_trait_lifecycle.tk') or arg == 'g14_static_async_trait_lifecycle.tk' for arg in argv):
                errors.append('0.14 G1 lifecycle compile fixture mismatch: ' + r_name)

        elif r_name == 'g1-lifecycle-run':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if 'All 4 lifecycle branches passed successfully!' not in out_s:
                errors.append('0.14 G1 lifecycle run stdout assertion missing: ' + r_name)

        elif r_name.startswith('g1-sourceless-compile-'):
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G1 compile command program must execute tokac: ' + r_name)
            if r_name == 'g1-sourceless-compile-mod-a':
                if not any(arg.endswith('/mod_a.tk') or arg == 'mod_a.tk' for arg in argv):
                    errors.append('0.14 G1 sourceless mod_a fixture mismatch: ' + r_name)
            elif r_name == 'g1-sourceless-compile-mod-b':
                if not any(arg.endswith('/mod_b.tk') or arg == 'mod_b.tk' for arg in argv):
                    errors.append('0.14 G1 sourceless mod_b fixture mismatch: ' + r_name)
            elif r_name == 'g1-sourceless-compile-main':
                if not any(arg.endswith('/main.tk') or arg == 'main.tk' for arg in argv):
                    errors.append('0.14 G1 sourceless main fixture mismatch: ' + r_name)

        elif r_name == 'g1-sourceless-run-main':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)

        elif r_name == 'g1-reject-dyn-trait':
            if exit_code != 1:
                errors.append('0.14 G1 negative command exit code must be 1: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G1 negative command must execute tokac: ' + r_name)
            if not any(arg.endswith('/dyn_trait_async_param.tk') or arg == 'dyn_trait_async_param.tk' for arg in argv):
                errors.append('0.14 G1 reject dyn trait fixture mismatch: ' + r_name)
            if '--check-only' not in argv:
                errors.append('0.14 G1 negative command missing --check-only: ' + r_name)
            if r.get('diagnostic') != 'E0617':
                errors.append('0.14 G1 reject dyn trait diagnostic mismatch: ' + r_name)
            if 'E0617' not in err_s:
                errors.append('0.14 G1 reject dyn trait stderr missing E0617: ' + r_name)

        elif r_name == 'g1-reject-plain-self':
            if exit_code != 1:
                errors.append('0.14 G1 negative command exit code must be 1: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G1 negative command must execute tokac: ' + r_name)
            if not any(arg.endswith('/trait_async_plain_self.tk') or arg == 'trait_async_plain_self.tk' for arg in argv):
                errors.append('0.14 G1 reject plain self fixture mismatch: ' + r_name)
            if '--check-only' not in argv:
                errors.append('0.14 G1 negative command missing --check-only: ' + r_name)
            if r.get('diagnostic') != 'E0618':
                errors.append('0.14 G1 reject plain self diagnostic mismatch: ' + r_name)
            if 'E0618' not in err_s:
                errors.append('0.14 G1 reject plain self stderr missing E0618: ' + r_name)

        elif r_name.startswith('g2-compile-'):
            if exit_code != 0:
                errors.append('0.14 G2 compile command exit code must be 0: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G2 compile command program must execute tokac: ' + r_name)
            case = r_name.removeprefix('g2-compile-')
            if not any(arg.endswith('/' + case + '.tk') or arg == (case + '.tk') for arg in argv):
                errors.append('0.14 G2 compile fixture mismatch: ' + r_name)

        elif r_name.startswith('g2-run-'):
            if exit_code != 0:
                errors.append('0.14 G2 run command exit code must be 0: ' + r_name)
            case = r_name.removeprefix('g2-run-')
            if case not in G2_EXPECTED_FACTS:
                errors.append('0.14 unknown G2 case in run command: ' + r_name)
            else:
                for fact in G2_EXPECTED_FACTS[case]:
                    if fact not in out_s:
                        errors.append(f'0.14 G2 {case} missing expected fact {fact!r} in stdout: {r_name}')

        elif r_name.startswith('g3-compile-'):
            if exit_code != 0:
                errors.append('0.14 G3 positive compile command exit code must be 0: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G3 compile command program must execute tokac: ' + r_name)
            tag = r_name.removeprefix('g3-compile-')
            fix_name = expected_g3_pos_fixtures.get(tag)
            if fix_name and not any(arg.endswith('/' + fix_name) or arg == fix_name for arg in argv):
                errors.append('0.14 G3 positive compile fixture mismatch: ' + r_name)

        elif r_name.startswith('g3-run-'):
            if exit_code != 0:
                errors.append('0.14 G3 positive run command exit code must be 0: ' + r_name)
            tag = r_name.removeprefix('g3-run-')
            if tag == 'lifecycle' and 'All 16 unwrap propagation lifecycle controls passed with zero TCB residue and exact-once drop!' not in out_s:
                errors.append('0.14 G3 lifecycle run stdout assertion missing: ' + r_name)
            elif tag == 'aggregate' and 'PASS' not in out_s:
                errors.append('0.14 G3 aggregate run stdout assertion missing: ' + r_name)

        elif r_name.startswith('g3-neg-'):
            if exit_code != 1:
                errors.append('0.14 G3 negative command exit code must be 1: ' + r_name)
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.14 G3 negative command program must execute tokac: ' + r_name)
            parts = r_name.removeprefix('g3-neg-').rsplit('-', 1)
            if len(parts) == 2:
                case, mode = parts
                if not any(arg.endswith('/' + case + '.tk') or arg == (case + '.tk') for arg in argv):
                    errors.append('0.14 G3 negative fixture mismatch: ' + r_name)
                if mode == 'check':
                    if '--check-only' not in argv:
                        errors.append('0.14 G3 check command missing --check-only: ' + r_name)
                elif mode == 'object':
                    if '-c' not in argv or '-o' not in argv:
                        errors.append('0.14 G3 object command missing -c or -o: ' + r_name)
                    if r.get('artifact_absent') is not True:
                        errors.append('0.14 G3 negative object command did not confirm artifact absence: ' + r_name)
                elif mode == 'ir':
                    if '--emit-llvm' not in argv or '-o' not in argv:
                        errors.append('0.14 G3 IR command missing --emit-llvm or -o: ' + r_name)
                    if r.get('artifact_absent') is not True:
                        errors.append('0.14 G3 negative IR command did not confirm artifact absence: ' + r_name)
                elif mode == 'compile':
                    if '-o' not in argv:
                        errors.append('0.14 G3 compile command missing -o: ' + r_name)
                    if r.get('artifact_absent') is not True:
                        errors.append('0.14 G3 negative compile command did not confirm artifact absence: ' + r_name)
            if r.get('diagnostic') != 'E04512':
                errors.append('0.14 G3 negative command diagnostic mismatch: ' + r_name)
            if 'E04512' not in err_s:
                errors.append('0.14 G3 negative command stderr missing E04512: ' + r_name)
        else:
            errors.append('Unknown 0.14 command receipt: ' + r_name)

    missing = expected_receipt_names - seen_receipts
    if missing:
        errors.append('Missing 0.14 command receipts: ' + ', '.join(sorted(missing)))

    return errors


EXPECTED_015_FIXTURES_DIGEST = '19d90ad6d157e4f12a3ddab1ddd73721df11f5f321c68a0e6ac3877792f191a4'


def expected_015_script_sha256():
    m = get_bundle_manifest()
    if m and isinstance(m.get('modules'), dict):
        return m['modules'].get('test_0_15_candidate.py')
    return None


EXPECTED_015_VERSION_RECEIPTS = {
    'version-tokac',
    'version-toka',
    'version-tokafmt',
    'version-tokalsp',
}

POS_015_CASES = [
    'recur_direct',
    'recur_mutual',
    'recur_order_permuted',
    'recur_bsearch',
    'recur_tree',
    'recur_borrow',
    'recur_cede',
    'cleanup_drop',
    'async_nonrecur_control',
]

EXPECTED_015_POS_RECEIPTS = {
    'pos-tki-provider-compile',
    'pos-tki-consumer-compile',
    'pos-tki-consumer-run',
}
for case in POS_015_CASES:
    EXPECTED_015_POS_RECEIPTS.add(f'pos-{case}-compile')
    EXPECTED_015_POS_RECEIPTS.add(f'pos-{case}-run')

NEG_015_DIAGNOSTICS = {
    'neg-async-direct-check': 'E0406',
    'neg-async-direct-object': 'E0406',
    'neg-async-direct-ir': 'E0406',
    'neg-async-direct-compile': 'E0406',
    'neg-async_mutual-check': 'E0406',
    'neg-async_cycle_mixed-check': 'E0406',
    'neg-poly_recur-check': 'E0406',
    'neg-type_mismatch-check': 'E0408',
    'neg-trait_bound-check': 'E0606',
    'neg-borrow_conflict-check': 'E0441',
    'neg-double_cede-check': 'E0438',
    'neg-mutual_error-check': 'E04576',
    'neg-tainted_reuse-check': 'E04576',
}

NEG_015_CHECK_FIXTURES = {
    'neg-async-direct-check': 'async_recur_direct.tk',
    'neg-async_mutual-check': 'async_recur_mutual.tk',
    'neg-async_cycle_mixed-check': 'async_cycle_mixed.tk',
    'neg-poly_recur-check': 'poly_recur.tk',
    'neg-type_mismatch-check': 'type_mismatch.tk',
    'neg-trait_bound-check': 'trait_bound.tk',
    'neg-borrow_conflict-check': 'borrow_conflict.tk',
    'neg-double_cede-check': 'double_cede.tk',
    'neg-mutual_error-check': 'mutual_error.tk',
    'neg-tainted_reuse-check': 'tainted_reuse.tk',
}

EXPECTED_015_NEG_RECEIPTS = set(NEG_015_DIAGNOSTICS.keys())
EXPECTED_015_RECEIPTS = (
    EXPECTED_015_VERSION_RECEIPTS | EXPECTED_015_POS_RECEIPTS | EXPECTED_015_NEG_RECEIPTS
)


def candidate_015_receipt_errors(receipt, revision, label, target=None):
    if not isinstance(receipt, dict):
        return ['0.15 candidate receipt is missing or not a dict']
    errors = []
    if receipt.get('schema') != 'toka.0.15-candidate-controls':
        errors.append('0.15 receipt schema mismatch')
    version = receipt.get('version')
    if type(version) is not int or isinstance(version, bool) or version != 1:
        errors.append('0.15 receipt version must be int 1')
    if receipt.get('result') != 'pass':
        errors.append('0.15 receipt result is not pass')
    if receipt.get('candidate_revision') != revision:
        errors.append('0.15 candidate revision mismatch')
    if receipt.get('version_label') != label:
        errors.append('0.15 version label mismatch')
    target_val = receipt.get('target')
    if not isinstance(target_val, str) or not target_val:
        errors.append('0.15 target missing or invalid')
    elif target is not None and target_val != target:
        errors.append('0.15 target mismatch: expected %s, got %s' % (target, target_val))
    if receipt.get('build_testing') is not False:
        errors.append('0.15 receipt build_testing must be False')
    if receipt.get('groups') != ['G-recursion']:
        errors.append('0.15 receipt groups mismatch')
    if receipt.get('fixtures_digest') != EXPECTED_015_FIXTURES_DIGEST:
        errors.append('0.15 fixtures digest mismatch')

    if receipt.get('control_script_name') != 'test_0_15_candidate.py':
        errors.append('0.15 control script name mismatch')
    control_sha = receipt.get('control_script_sha256')
    expected_control_sha = expected_015_script_sha256()
    if not isinstance(control_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', control_sha):
        errors.append('0.15 control script sha256 missing or invalid')
    elif expected_control_sha and control_sha != expected_control_sha:
        errors.append('0.15 control script digest mismatch')

    # Source-hidden TKI facts validation
    tki_facts = receipt.get('source_hidden_tki')
    if not isinstance(tki_facts, dict):
        errors.append('0.15 receipt missing source_hidden_tki dictionary')
    else:
        if tki_facts.get('missing_interface_rejected') is not True:
            errors.append('0.15 source_hidden_tki missing_interface_rejected must be True')
        if tki_facts.get('corrupt_interface_rejected') is not True:
            errors.append('0.15 source_hidden_tki corrupt_interface_rejected must be True')
        if tki_facts.get('consumer_result') != 'pass':
            errors.append('0.15 source_hidden_tki consumer_result must be pass')
        psp = tki_facts.get('provider_source_present')
        if psp is not False or type(psp) is not bool:
            errors.append('0.15 source_hidden_tki provider_source_present must exist and be exactly boolean False')
        p_tki = tki_facts.get('provider_tki_sha256')
        if not isinstance(p_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', p_tki) or len(set(p_tki)) <= 4:
            errors.append('0.15 source_hidden_tki provider_tki_sha256 missing, invalid or unbound dummy hash')
        p_obj = tki_facts.get('provider_object_sha256')
        if not isinstance(p_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', p_obj) or len(set(p_obj)) <= 4:
            errors.append('0.15 source_hidden_tki provider_object_sha256 missing, invalid or unbound dummy hash')

        c_tki = tki_facts.get('consumed_tki_sha256')
        if not isinstance(c_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', c_tki):
            errors.append('0.15 source_hidden_tki consumed_tki_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_tki != p_tki:
            errors.append('0.15 source_hidden_tki consumed_tki_sha256 mismatch with provider_tki_sha256')

        c_obj = tki_facts.get('consumed_object_sha256')
        if not isinstance(c_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', c_obj):
            errors.append('0.15 source_hidden_tki consumed_object_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_obj != p_obj:
            errors.append('0.15 source_hidden_tki consumed_object_sha256 mismatch with provider_object_sha256')

        if 'missing_interface_command' not in tki_facts:
            errors.append('0.15 source_hidden_tki missing_interface_command is mandatory')
        if 'corrupt_interface_command' not in tki_facts:
            errors.append('0.15 source_hidden_tki corrupt_interface_command is mandatory')

    ver = label.removeprefix('v')
    ver_regex = re.compile(r'(?<![0-9a-zA-Z._+-])' + re.escape(ver) + r'(?![0-9a-zA-Z._+-])')

    tools = receipt.get('four_tool_versions')
    if not isinstance(tools, dict) or set(tools.keys()) != {'tokac', 'toka', 'tokafmt', 'tokalsp'}:
        errors.append('0.15 four_tool_versions missing or invalid')
    else:
        for t_name, t_str in tools.items():
            if not isinstance(t_str, str) or not ver_regex.search(t_str):
                errors.append('0.15 tool version mismatch: ' + t_name)

    counts = receipt.get('counts')
    expected_groups = {'version': 4, 'G-recursion-positive': 21, 'G-recursion-negative': 13}
    if not isinstance(counts, dict):
        errors.append('0.15 counts missing or not a dict')
    else:
        for k in ('total_receipts', 'passed', 'failed'):
            val = counts.get(k)
            if type(val) is not int or isinstance(val, bool):
                errors.append(f'0.15 counts {k} must be exact int')
        if counts.get('total_receipts') != 38 or counts.get('passed') != 38 or counts.get('failed') != 0:
            errors.append('0.15 counts values mismatch: expected total 38, passed 38, failed 0')
        groups = counts.get('groups')
        if not isinstance(groups, dict) or groups != expected_groups:
            errors.append('0.15 counts groups mismatch or invalid')
        else:
            for g_name, g_cnt in groups.items():
                if type(g_cnt) is not int or isinstance(g_cnt, bool):
                    errors.append(f'0.15 counts groups {g_name} must be exact int')

    receipts = receipt.get('receipts')
    if not isinstance(receipts, list) or len(receipts) != 38:
        errors.append('0.15 command receipts list must have exactly 38 entries')
        return errors

    sdk_root = receipt.get('SDK_root')
    if not isinstance(sdk_root, str) or not sdk_root:
        errors.append('0.15 SDK_root missing or invalid')

    expected_tokac = None
    for r in receipts:
        if isinstance(r, dict) and r.get('name') == 'version-tokac':
            v_argv = r.get('argv') or r.get('cmd')
            v_cwd = r.get('cwd')
            if isinstance(v_argv, list) and v_argv and isinstance(v_cwd, str):
                v_prog = v_argv[0].replace('\\', '/')
                expected_tokac = os.path.normpath(v_prog if os.path.isabs(v_prog) else os.path.join(v_cwd, v_prog))
            break

    if expected_tokac is None:
        errors.append('0.15 candidate receipts missing version-tokac command to establish compiler identity')
    elif isinstance(sdk_root, str) and sdk_root:
        sdk_tokac_bin = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac'))
        sdk_tokac_exe = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac.exe'))
        if expected_tokac not in (sdk_tokac_bin, sdk_tokac_exe):
            errors.append(f'0.15 version-tokac program {expected_tokac} does not match SDK_root {sdk_root}')

    # Full command-receipt validation for nested TKI negative commands
    if isinstance(tki_facts, dict):
        for neg_key in ('missing_interface_command', 'corrupt_interface_command'):
            if neg_key not in tki_facts:
                continue
            n_rec = tki_facts[neg_key]
            if not isinstance(n_rec, dict):
                errors.append(f'0.15 source_hidden_tki {neg_key} must be a dictionary')
                continue
            # Required field presence and strict types
            if 'cwd' not in n_rec or not isinstance(n_rec['cwd'], str) or not n_rec['cwd']:
                errors.append(f'0.15 source_hidden_tki {neg_key} cwd missing or invalid')
            cwd_val = n_rec.get('cwd', '')

            if 'exit_code' not in n_rec or type(n_rec['exit_code']) is not int or isinstance(n_rec['exit_code'], bool) or n_rec['exit_code'] != 1:
                errors.append(f'0.15 source_hidden_tki {neg_key} exit_code must be exact integer 1')

            if 'signal' not in n_rec or n_rec['signal'] is not None:
                errors.append(f'0.15 source_hidden_tki {neg_key} signal must be explicit null')

            if 'termination' not in n_rec or n_rec['termination'] is not None:
                errors.append(f'0.15 source_hidden_tki {neg_key} termination must be explicit null')

            if 'error' not in n_rec or n_rec['error'] is not None:
                errors.append(f'0.15 source_hidden_tki {neg_key} error must be explicit null')

            if 'ok' not in n_rec or type(n_rec['ok']) is not bool or n_rec['ok'] is not True:
                errors.append(f'0.15 source_hidden_tki {neg_key} ok must be exact boolean True')

            if 'result' not in n_rec or n_rec['result'] != 'pass':
                errors.append(f'0.15 source_hidden_tki {neg_key} result must be pass')

            if 'artifact_absent' not in n_rec or type(n_rec['artifact_absent']) is not bool or n_rec['artifact_absent'] is not True:
                errors.append(f'0.15 source_hidden_tki {neg_key} artifact_absent must be exact boolean True')

            if 'execution_ms' not in n_rec:
                errors.append(f'0.15 source_hidden_tki {neg_key} execution_ms missing')
            else:
                ms = n_rec['execution_ms']
                if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
                    errors.append(f'0.15 source_hidden_tki {neg_key} execution_ms invalid')

            if 'argv' not in n_rec:
                errors.append(f'0.15 source_hidden_tki {neg_key} argv missing')
                n_argv = []
            elif not isinstance(n_rec['argv'], list) or not n_rec['argv'] or not all(isinstance(a, str) and a for a in n_rec['argv']):
                errors.append(f'0.15 source_hidden_tki {neg_key} argv invalid')
                n_argv = []
            else:
                n_argv = n_rec['argv']
                n_prog = os.path.normpath(n_argv[0].replace('\\', '/'))
                if expected_tokac and n_prog != expected_tokac:
                    errors.append(f'0.15 source_hidden_tki {neg_key} compiler mismatch: executes {n_prog}, expected {expected_tokac}')
                if len(n_argv) != 7 or n_argv.count('-I') != 1 or n_argv.count('-o') != 1 or n_argv[1] != '-I' or n_argv[5] != '-o':
                    errors.append(f'0.15 source_hidden_tki {neg_key} argv template mismatch')
                else:
                    if not (n_argv[3].endswith('/tki_consumer.tk') or n_argv[3] == 'tki_consumer.tk'):
                        errors.append(f'0.15 source_hidden_tki {neg_key} missing consumer source argument')
                    if os.path.basename(n_argv[4]) != 'provider.o':
                        errors.append(f'0.15 source_hidden_tki {neg_key} missing provider.o object argument')
                    bad_bin_base = os.path.basename(n_argv[6])
                    if bad_bin_base not in ('bad_bin', 'bad_bin.bin', 'bad_bin.exe'):
                        errors.append(f'0.15 source_hidden_tki {neg_key} output basename mismatch')
                    if isinstance(cwd_val, str) and cwd_val:
                        norm_i = os.path.normpath(n_argv[2] if os.path.isabs(n_argv[2]) else os.path.join(cwd_val, n_argv[2]))
                        norm_cwd = os.path.normpath(cwd_val)
                        if norm_i != norm_cwd:
                            errors.append(f'0.15 source_hidden_tki {neg_key} -I directory must match cwd')

            out_hash = n_rec.get('stdout_sha256')
            if not isinstance(out_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', out_hash):
                errors.append(f'0.15 source_hidden_tki {neg_key} stdout_sha256 missing or invalid')

            err_hash = n_rec.get('stderr_sha256')
            if not isinstance(err_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', err_hash):
                errors.append(f'0.15 source_hidden_tki {neg_key} stderr_sha256 missing or invalid')

            if 'stdout' not in n_rec or not isinstance(n_rec['stdout'], str):
                errors.append(f'0.15 source_hidden_tki {neg_key} stdout missing or not a string')
            else:
                out_text = n_rec['stdout']
                if isinstance(out_hash, str) and hashlib.sha256(out_text.encode('utf-8')).hexdigest() != out_hash:
                    errors.append(f'0.15 source_hidden_tki {neg_key} stdout content does not match stdout_sha256')

            if 'stderr' not in n_rec or not isinstance(n_rec['stderr'], str):
                errors.append(f'0.15 source_hidden_tki {neg_key} stderr missing or not a string')
            else:
                err_text = n_rec['stderr']
                if isinstance(err_hash, str) and hashlib.sha256(err_text.encode('utf-8')).hexdigest() != err_hash:
                    errors.append(f'0.15 source_hidden_tki {neg_key} stderr content does not match stderr_sha256')
                if 'E0901' not in err_text:
                    errors.append(f'0.15 source_hidden_tki {neg_key} stderr missing E0901')
                if neg_key == 'missing_interface_command' and 'Could not open file' not in err_text:
                    errors.append(f'0.15 source_hidden_tki {neg_key} stderr missing missing-file diagnostic fact')
                elif neg_key == 'corrupt_interface_command' and 'Incompatible or stale interface file' not in err_text:
                    errors.append(f'0.15 source_hidden_tki {neg_key} stderr missing corrupt-file diagnostic fact')

    compile_outputs = {}
    for r in receipts:
        if isinstance(r, dict):
            name = str(r.get('name', ''))
            argv = r.get('argv') or r.get('cmd')
            cwd = r.get('cwd')
            if isinstance(argv, list) and isinstance(cwd, str) and cwd and '-o' in argv:
                if argv.count('-o') != 1:
                    errors.append(f'0.15 compile command must have unique -o flag: {name}')
                idx = argv.index('-o')
                if idx + 1 < len(argv) and isinstance(argv[idx + 1], str) and argv[idx + 1]:
                    raw_out = argv[idx + 1]
                    norm_out = os.path.normpath(raw_out if os.path.isabs(raw_out) else os.path.join(cwd, raw_out))
                    compile_outputs[name] = norm_out

    run_to_compile = {
        'pos-tki-consumer-run': ('pos-tki-consumer-compile', 'tki_bin'),
    }
    for case in POS_015_CASES:
        run_to_compile[f'pos-{case}-run'] = (f'pos-{case}-compile', f'{case}_bin')

    compile_expected_bases = {
        'pos-tki-provider-compile': ('provider.o',),
        'pos-tki-consumer-compile': ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'),
        'neg-async-direct-object': ('async_direct.o',),
        'neg-async-direct-ir': ('async_direct.ll',),
        'neg-async-direct-compile': ('async_direct.bin', 'async_direct.exe', 'async_direct'),
    }
    for case in POS_015_CASES:
        compile_expected_bases[f'pos-{case}-compile'] = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')

    seen_receipts = set()
    for r in receipts:
        if not isinstance(r, dict):
            errors.append('0.15 command receipt is not a dict')
            continue
        r_name = str(r.get('name', ''))
        if not r_name:
            errors.append('0.15 command receipt missing name')
            continue
        if r_name in seen_receipts:
            errors.append('0.15 duplicate command receipt name: ' + r_name)
        seen_receipts.add(r_name)

        if r.get('ok') is not True:
            errors.append('0.15 command receipt is not marked ok: ' + r_name)
        if r.get('result') != 'pass':
            errors.append('0.15 command receipt result is not pass: ' + r_name)

        if 'signal' not in r or r['signal'] is not None:
            errors.append('0.15 command receipt missing signal key or abnormal signal: ' + r_name)
        if r.get('termination') is not None or r.get('error') is not None:
            errors.append('0.15 command receipt indicates abnormal termination or error: ' + r_name)

        argv = r.get('argv') or r.get('cmd')
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
            errors.append('0.15 command receipt argv invalid: ' + r_name)
            continue

        cwd = r.get('cwd')
        if not isinstance(cwd, str) or not cwd:
            errors.append('0.15 command receipt cwd missing or invalid: ' + r_name)
            cwd = ''

        prog = argv[0].replace('\\', '/')
        if prog in ('/bin/true', '/bin/false', 'true', 'false') or prog.endswith('/true') or prog.endswith('/false'):
            errors.append('0.15 command receipt program cannot be no-op dummy: ' + r_name)

        norm_prog = os.path.normpath(prog if os.path.isabs(prog) else os.path.join(cwd, prog))
        prog_base = os.path.basename(norm_prog)

        ms = r.get('execution_ms')
        if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
            errors.append('0.15 command receipt execution_ms missing or invalid: ' + r_name)

        exit_code = r.get('exit_code')
        if type(exit_code) is not int or isinstance(exit_code, bool):
            errors.append('0.15 command receipt exit_code missing or not int: ' + r_name)
            continue

        if 'stdout' not in r or not isinstance(r.get('stdout'), str):
            errors.append('0.15 command receipt stdout missing or not a string: ' + r_name)
            out_s = ''
        else:
            out_s = r['stdout']

        if 'stderr' not in r or not isinstance(r.get('stderr'), str):
            errors.append('0.15 command receipt stderr missing or not a string: ' + r_name)
            err_s = ''
        else:
            err_s = r['stderr']

        if 'stdout_sha256' not in r or hashlib.sha256(out_s.encode('utf-8')).hexdigest() != r['stdout_sha256']:
            errors.append('0.15 command receipt stdout_sha256 mismatch: ' + r_name)
        if 'stderr_sha256' not in r or hashlib.sha256(err_s.encode('utf-8')).hexdigest() != r['stderr_sha256']:
            errors.append('0.15 command receipt stderr_sha256 mismatch: ' + r_name)

        if 'failure instead of assertions' in out_s or 'assertion failed' in out_s.lower():
            errors.append('0.15 command receipt stdout indicates failure: ' + r_name)

        is_neg = r_name.startswith('neg-')
        is_pos_compile = r_name.startswith('pos-') and r_name.endswith('-compile')
        if (is_pos_compile or is_neg):
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.15 compile command program must execute tokac: ' + r_name)
            elif expected_tokac and norm_prog != expected_tokac:
                errors.append(f'0.15 compile command program mismatch with SDK tokac: {r_name} (executes {norm_prog}, expected {expected_tokac})')
            for f in ('--version', '-v', '-V', '--help', '-h'):
                if f in argv:
                    errors.append(f'0.15 compile/check command contains incompatible flag {f}: {r_name}')

        if not is_neg:
            if exit_code != 0:
                errors.append(f'0.15 positive command exit code must be 0: {r_name} (got {exit_code})')
            if r.get('diagnostic') is not None:
                errors.append('0.15 unexpected diagnostic on positive command: ' + r_name)
            if err_s.strip() and 'warning' not in err_s.lower():
                errors.append('0.15 unexpected stderr in positive command: ' + r_name)
        else:
            if exit_code != 1:
                errors.append(f'0.15 negative command exit code must be 1: {r_name} (got {exit_code})')

        # Exact argv template validation by command role
        if r_name.startswith('version-'):
            t_name = r_name.removeprefix('version-')
            if len(argv) != 2 or argv[1] != '--version':
                errors.append(f'0.15 tool version command argv template mismatch: {r_name}')
            if t_name == 'toka':
                if prog_base not in ('toka', 'toka.exe'):
                    errors.append('0.15 tool version command program mismatch: ' + r_name)
            else:
                if prog_base not in (t_name, t_name + '.exe'):
                    errors.append('0.15 tool version command program mismatch: ' + r_name)
            if isinstance(sdk_root, str) and sdk_root:
                sdk_tool_bin = os.path.normpath(os.path.join(sdk_root, 'bin', t_name))
                sdk_tool_exe = os.path.normpath(os.path.join(sdk_root, 'bin', t_name + '.exe'))
                if norm_prog not in (sdk_tool_bin, sdk_tool_exe):
                    errors.append(f'0.15 tool version program {norm_prog} does not match SDK_root {sdk_root}: {r_name}')
            if not ver_regex.search(out_s):
                errors.append(f'0.15 tool version {t_name} stdout missing {ver}: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-run'):
            if len(argv) != 1:
                errors.append(f'0.15 run command argv template mismatch (expected only executable): {r_name}')
            comp_name, exp_base = run_to_compile.get(r_name, ('', ''))
            expected_compiled_out = compile_outputs.get(comp_name)
            if not expected_compiled_out:
                errors.append(f'0.15 missing matching compile output for run command: {r_name} (needs {comp_name})')
            elif norm_prog != expected_compiled_out:
                errors.append(f'0.15 run program does not match compile output: {r_name} (executes {norm_prog}, compiled {expected_compiled_out})')
            valid_bases = (exp_base, exp_base + '.bin', exp_base + '.exe')
            if prog_base not in valid_bases:
                errors.append(f'0.15 run program basename mismatch: {r_name} (got {prog_base}, expected one of {valid_bases})')

        elif r_name == 'pos-tki-provider-compile':
            # Template: [tokac, <src>, -c, -o, <obj>]
            if len(argv) != 5:
                errors.append(f'0.15 pos-tki-provider-compile argv length mismatch: expected 5, got {len(argv)}')
            if argv.count('-c') != 1 or argv.count('-o') != 1:
                errors.append(f'0.15 pos-tki-provider-compile must have unique -c and -o: {r_name}')
            if len(argv) >= 4 and (argv[2] != '-c' or argv[3] != '-o'):
                errors.append(f'0.15 pos-tki-provider-compile argv ordering mismatch: {r_name}')
            if not (argv[1].endswith('/tki_provider.tk') or argv[1] == 'tki_provider.tk'):
                errors.append(f'0.15 pos-tki-provider-compile fixture mismatch: {r_name}')
            if len(argv) >= 5:
                out_path = os.path.normpath(argv[4] if os.path.isabs(argv[4]) else os.path.join(cwd, argv[4]))
                if os.path.basename(out_path) != 'provider.o':
                    errors.append(f'0.15 pos-tki-provider-compile output basename mismatch: {r_name}')

        elif r_name == 'pos-tki-consumer-compile':
            # Template: [tokac, -I, <stage_dir>, <src>, <provider.o>, -o, <bin>]
            if len(argv) != 7:
                errors.append(f'0.15 pos-tki-consumer-compile argv length mismatch: expected 7, got {len(argv)}')
            if argv.count('-I') != 1 or argv.count('-o') != 1:
                errors.append(f'0.15 pos-tki-consumer-compile must have unique -I and -o: {r_name}')
            if len(argv) >= 6 and (argv[1] != '-I' or argv[5] != '-o'):
                errors.append(f'0.15 pos-tki-consumer-compile argv ordering mismatch: {r_name}')
            if len(argv) >= 3:
                i_dir = os.path.normpath(argv[2] if os.path.isabs(argv[2]) else os.path.join(cwd, argv[2]))
                norm_cwd = os.path.normpath(cwd) if cwd else ''
                if norm_cwd and i_dir != norm_cwd:
                    errors.append('0.15 pos-tki-consumer-compile -I directory must match consumer work directory: ' + r_name)
            if not (len(argv) >= 4 and (argv[3].endswith('/tki_consumer.tk') or argv[3] == 'tki_consumer.tk')):
                errors.append(f'0.15 pos-tki-consumer-compile consumer fixture mismatch: {r_name}')
            if len(argv) >= 5:
                if os.path.basename(argv[4]) != 'provider.o':
                    errors.append('0.15 pos-tki-consumer-compile provider object basename must be provider.o: ' + r_name)
                norm_o = os.path.normpath(argv[4] if os.path.isabs(argv[4]) else os.path.join(cwd, argv[4]))
                norm_o_dir = os.path.dirname(norm_o)
                if 'i_dir' in locals() and norm_o_dir != i_dir:
                    errors.append('0.15 pos-tki-consumer-compile provider object directory must match -I import directory: ' + r_name)
            if len(argv) >= 7:
                out_path = os.path.normpath(argv[6] if os.path.isabs(argv[6]) else os.path.join(cwd, argv[6]))
                if os.path.basename(out_path) not in ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'):
                    errors.append(f'0.15 pos-tki-consumer-compile output basename mismatch: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-compile'):
            # Template: [tokac, <src>, -o, <bin>]
            case = r_name.removeprefix('pos-').removesuffix('-compile')
            if len(argv) != 4:
                errors.append(f'0.15 positive compile command argv length mismatch: expected 4, got {len(argv)}: {r_name}')
            if argv.count('-o') != 1:
                errors.append(f'0.15 positive compile command must have unique -o: {r_name}')
            if len(argv) >= 3 and argv[2] != '-o':
                errors.append(f'0.15 positive compile command argv ordering mismatch: {r_name}')
            if not (argv[1].endswith('/' + case + '.tk') or argv[1] == (case + '.tk')):
                errors.append(f'0.15 compile fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                exp_bases = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')
                if os.path.basename(out_path) not in exp_bases:
                    errors.append(f'0.15 positive compile command output basename mismatch: {r_name}')

        elif r_name == 'neg-async-direct-object':
            # Template: [tokac, -c, <src>, -o, <obj>]
            if len(argv) != 5 or argv.count('-c') != 1 or argv.count('-o') != 1 or argv[1] != '-c' or argv[3] != '-o':
                errors.append(f'0.15 neg-async-direct-object command template mismatch: {r_name}')
            if not (argv[2].endswith('/async_recur_direct.tk') or argv[2] == 'async_recur_direct.tk'):
                errors.append(f'0.15 negative fixture mismatch: {r_name}')
            if len(argv) >= 5:
                out_path = os.path.normpath(argv[4] if os.path.isabs(argv[4]) else os.path.join(cwd, argv[4]))
                if os.path.basename(out_path) != 'async_direct.o':
                    errors.append(f'0.15 neg-async-direct-object output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.15 negative command did not confirm artifact absence: ' + r_name)

        elif r_name == 'neg-async-direct-ir':
            # Template: [tokac, --emit-llvm, -c, <src>, -o, <ir>]
            if len(argv) != 6 or argv.count('--emit-llvm') != 1 or argv.count('-c') != 1 or argv.count('-o') != 1 or \
                    argv[1] != '--emit-llvm' or argv[2] != '-c' or argv[4] != '-o':
                errors.append(f'0.15 neg-async-direct-ir command template mismatch: {r_name}')
            if not (argv[3].endswith('/async_recur_direct.tk') or argv[3] == 'async_recur_direct.tk'):
                errors.append(f'0.15 negative fixture mismatch: {r_name}')
            if len(argv) >= 6:
                out_path = os.path.normpath(argv[5] if os.path.isabs(argv[5]) else os.path.join(cwd, argv[5]))
                if os.path.basename(out_path) != 'async_direct.ll':
                    errors.append(f'0.15 neg-async-direct-ir output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.15 negative command did not confirm artifact absence: ' + r_name)

        elif r_name == 'neg-async-direct-compile':
            # Template: [tokac, <src>, -o, <bin>]
            if len(argv) != 4 or argv.count('-o') != 1 or argv[2] != '-o':
                errors.append(f'0.15 neg-async-direct-compile command template mismatch: {r_name}')
            if not (argv[1].endswith('/async_recur_direct.tk') or argv[1] == 'async_recur_direct.tk'):
                errors.append(f'0.15 negative fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                if os.path.basename(out_path) not in ('async_direct.bin', 'async_direct.exe', 'async_direct'):
                    errors.append(f'0.15 neg-async-direct-compile output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.15 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-check'):
            # Template: [tokac, --check-only, <src>]
            if len(argv) != 3 or argv.count('--check-only') != 1 or argv[1] != '--check-only':
                errors.append(f'0.15 negative check command template mismatch: {r_name}')
            exp_fix = NEG_015_CHECK_FIXTURES.get(r_name)
            if exp_fix:
                if not (argv[2].endswith('/' + exp_fix) or argv[2] == exp_fix):
                    errors.append(f'0.15 negative fixture mismatch: {r_name}')
            else:
                fix_name = r_name.removeprefix('neg-').removesuffix('-check')
                if not (argv[2].endswith('/' + fix_name + '.tk') or argv[2] == (fix_name + '.tk')):
                    errors.append(f'0.15 negative fixture mismatch: {r_name}')

        if r_name in NEG_015_DIAGNOSTICS:
            exp_diag = NEG_015_DIAGNOSTICS[r_name]
            if r.get('diagnostic') != exp_diag:
                errors.append(f'0.15 negative command diagnostic mismatch: {r_name} (expected {exp_diag}, got {r.get("diagnostic")})')
            if exp_diag not in err_s:
                errors.append(f'0.15 negative command stderr missing {exp_diag}: {r_name}')

    missing = EXPECTED_015_RECEIPTS - seen_receipts
    if missing:
        errors.append('Missing 0.15 command receipts: ' + ', '.join(sorted(missing)))

    return errors


EXPECTED_016_FIXTURES_DIGEST = '7579bc4271da9eb1f1f413309b580fe8495b9a28c0b15fbe430a18b89267923e'


def expected_016_script_sha256():
    m = get_bundle_manifest()
    if m and isinstance(m.get('modules'), dict):
        return m['modules'].get('test_0_16_candidate.py')
    return None


EXPECTED_016_VERSION_RECEIPTS = {
    'version-tokac',
    'version-toka',
    'version-tokafmt',
    'version-tokalsp',
}

POS_016_CASES = [
    'scoped_normal_order',
    'scoped_early_exit',
    'scoped_cascade_cancel',
    'forwarder_wrapper',
    'pre_drain_cancel_order',
    'mid_drain_cancel_barrier',
]

EXPECTED_016_POS_RECEIPTS = {
    'pos-tki-provider-compile',
    'pos-tki-consumer-compile',
    'pos-tki-consumer-run',
}
for case in POS_016_CASES:
    EXPECTED_016_POS_RECEIPTS.add(f'pos-{case}-compile')
    EXPECTED_016_POS_RECEIPTS.add(f'pos-{case}-run')

NEG_016_MULTI_MODE_CASES = [
    ('return_escape', 'E0455'),
    ('spawn_into_escape', 'E0456'),
    ('forged_spawn_routes_return', 'E0456'),
    ('await_question_drain', 'E0456'),
    ('r2_d2a_mutable_sibling_launder', 'E0456'),
    ('r2_d2b_forged_spawn_scoped_sibling', 'E0456'),
    ('forged_join', 'E0456'),
    ('forged_cancel_join', 'E0456'),
]

NEG_016_DIAGNOSTICS = {
    'neg-detached_start-check': 'E04583',
    'neg-container_escape-check': 'E0456',
    'neg-mutation_conflict-check': 'E0440',
    'neg-unjoined_scope_exit-check': 'E0456',
    'neg-borrowed_child_result-check': 'E04658',
}
for base_name, diag in NEG_016_MULTI_MODE_CASES:
    NEG_016_DIAGNOSTICS[f'neg-{base_name}-check'] = diag
    NEG_016_DIAGNOSTICS[f'neg-{base_name}-object'] = diag
    NEG_016_DIAGNOSTICS[f'neg-{base_name}-ir'] = diag
    NEG_016_DIAGNOSTICS[f'neg-{base_name}-compile'] = diag

NEG_016_CHECK_FIXTURES = {
    'neg-detached_start-check': 'detached_start.tk',
    'neg-container_escape-check': 'container_escape.tk',
    'neg-mutation_conflict-check': 'mutation_conflict.tk',
    'neg-unjoined_scope_exit-check': 'unjoined_scope_exit.tk',
    'neg-borrowed_child_result-check': 'borrowed_child_result.tk',
}
for base_name, _ in NEG_016_MULTI_MODE_CASES:
    NEG_016_CHECK_FIXTURES[f'neg-{base_name}-check'] = f'{base_name}.tk'

EXPECTED_016_NEG_RECEIPTS = set(NEG_016_DIAGNOSTICS.keys())
EXPECTED_016_RECEIPTS = (
    EXPECTED_016_VERSION_RECEIPTS | EXPECTED_016_POS_RECEIPTS | EXPECTED_016_NEG_RECEIPTS
)


def candidate_016_receipt_errors(receipt, revision, label, target=None):
    if not isinstance(receipt, dict):
        return ['0.16 candidate receipt is missing or not a dict']
    errors = []
    if receipt.get('schema') != 'toka.0.16-candidate-controls':
        errors.append('0.16 receipt schema mismatch')
    version = receipt.get('version')
    if type(version) is not int or isinstance(version, bool) or version != 1:
        errors.append('0.16 receipt version must be int 1')
    if receipt.get('result') != 'pass':
        errors.append('0.16 receipt result is not pass')
    if receipt.get('candidate_revision') != revision:
        errors.append('0.16 candidate revision mismatch')
    if receipt.get('version_label') != label:
        errors.append('0.16 version label mismatch')
    target_val = receipt.get('target')
    if not isinstance(target_val, str) or not target_val:
        errors.append('0.16 target missing or invalid')
    elif target is not None and target_val != target:
        errors.append('0.16 target mismatch: expected %s, got %s' % (target, target_val))
    if receipt.get('build_testing') is not False:
        errors.append('0.16 receipt build_testing must be False')
    if receipt.get('groups') != ['G-scoped-tasks']:
        errors.append('0.16 receipt groups mismatch')
    if receipt.get('fixtures_digest') != EXPECTED_016_FIXTURES_DIGEST:
        errors.append('0.16 fixtures digest mismatch')

    if receipt.get('control_script_name') != 'test_0_16_candidate.py':
        errors.append('0.16 control script name mismatch')
    control_sha = receipt.get('control_script_sha256')
    expected_control_sha = expected_016_script_sha256()
    if not isinstance(control_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', control_sha):
        errors.append('0.16 control script sha256 missing or invalid')
    elif expected_control_sha and control_sha != expected_control_sha:
        errors.append('0.16 control script digest mismatch')

    # Source-hidden TKI facts validation
    tki_facts = receipt.get('source_hidden_tki')
    if not isinstance(tki_facts, dict):
        errors.append('0.16 receipt missing source_hidden_tki dictionary')
    else:
        if tki_facts.get('missing_interface_rejected') is not True:
            errors.append('0.16 source_hidden_tki missing_interface_rejected must be True')
        if tki_facts.get('corrupt_interface_rejected') is not True:
            errors.append('0.16 source_hidden_tki corrupt_interface_rejected must be True')
        if tki_facts.get('consumer_result') != 'pass':
            errors.append('0.16 source_hidden_tki consumer_result must be pass')
        psp = tki_facts.get('provider_source_present')
        if psp is not False or type(psp) is not bool:
            errors.append('0.16 source_hidden_tki provider_source_present must exist and be exactly boolean False')
        p_tki = tki_facts.get('provider_tki_sha256')
        if not isinstance(p_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', p_tki) or len(set(p_tki)) <= 4:
            errors.append('0.16 source_hidden_tki provider_tki_sha256 missing, invalid or unbound dummy hash')
        p_obj = tki_facts.get('provider_object_sha256')
        if not isinstance(p_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', p_obj) or len(set(p_obj)) <= 4:
            errors.append('0.16 source_hidden_tki provider_object_sha256 missing, invalid or unbound dummy hash')

        c_tki = tki_facts.get('consumed_tki_sha256')
        if not isinstance(c_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', c_tki):
            errors.append('0.16 source_hidden_tki consumed_tki_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_tki != p_tki:
            errors.append('0.16 source_hidden_tki consumed_tki_sha256 mismatch with provider_tki_sha256')

        c_obj = tki_facts.get('consumed_object_sha256')
        if not isinstance(c_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', c_obj):
            errors.append('0.16 source_hidden_tki consumed_object_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_obj != p_obj:
            errors.append('0.16 source_hidden_tki consumed_object_sha256 mismatch with provider_object_sha256')

        if 'missing_interface_command' not in tki_facts:
            errors.append('0.16 source_hidden_tki missing_interface_command is mandatory')
        if 'corrupt_interface_command' not in tki_facts:
            errors.append('0.16 source_hidden_tki corrupt_interface_command is mandatory')
        if 'escape_negative_commands' not in tki_facts or not isinstance(tki_facts['escape_negative_commands'], dict):
            errors.append('0.16 source_hidden_tki escape_negative_commands is mandatory and must be a dictionary')
        elif set(tki_facts['escape_negative_commands'].keys()) != {'check', 'object', 'ir', 'compile'}:
            errors.append('0.16 source_hidden_tki escape_negative_commands must contain exactly [check, object, ir, compile]')
        if tki_facts.get('escape_negative_rejected') is not True or type(tki_facts.get('escape_negative_rejected')) is not bool:
            errors.append('0.16 source_hidden_tki escape_negative_rejected must be boolean True')

    ver = label.removeprefix('v')
    ver_regex = re.compile(r'(?<![0-9a-zA-Z._+-])' + re.escape(ver) + r'(?![0-9a-zA-Z._+-])')

    tools = receipt.get('four_tool_versions')
    if not isinstance(tools, dict) or set(tools.keys()) != {'tokac', 'toka', 'tokafmt', 'tokalsp'}:
        errors.append('0.16 four_tool_versions missing or invalid')
    else:
        for t_name, t_str in tools.items():
            if not isinstance(t_str, str) or not ver_regex.search(t_str):
                errors.append('0.16 tool version mismatch: ' + t_name)

    counts = receipt.get('counts')
    expected_groups = {'version': 4, 'G-scoped-tasks-positive': 15, 'G-scoped-tasks-negative': 37}
    if not isinstance(counts, dict):
        errors.append('0.16 counts missing or not a dict')
    else:
        for k in ('total_receipts', 'passed', 'failed'):
            val = counts.get(k)
            if type(val) is not int or isinstance(val, bool):
                errors.append(f'0.16 counts {k} must be exact int')
        if counts.get('total_receipts') != 56 or counts.get('passed') != 56 or counts.get('failed') != 0:
            errors.append('0.16 counts values mismatch: expected total 56, passed 56, failed 0')
        groups = counts.get('groups')
        if not isinstance(groups, dict) or groups != expected_groups:
            errors.append('0.16 counts groups mismatch or invalid')
        else:
            for g_name, g_cnt in groups.items():
                if type(g_cnt) is not int or isinstance(g_cnt, bool):
                    errors.append(f'0.16 counts groups {g_name} must be exact int')

    receipts = receipt.get('receipts')
    if not isinstance(receipts, list) or len(receipts) != 56:
        errors.append('0.16 command receipts list must have exactly 56 entries')
        return errors

    sdk_root = receipt.get('SDK_root')
    if not isinstance(sdk_root, str) or not sdk_root:
        errors.append('0.16 SDK_root missing or invalid')

    expected_tokac = None
    for r in receipts:
        if isinstance(r, dict) and r.get('name') == 'version-tokac':
            v_argv = r.get('argv') or r.get('cmd')
            v_cwd = r.get('cwd')
            if isinstance(v_argv, list) and v_argv and isinstance(v_cwd, str):
                v_prog = v_argv[0].replace('\\', '/')
                expected_tokac = os.path.normpath(v_prog if os.path.isabs(v_prog) else os.path.join(v_cwd, v_prog))
            break

    if expected_tokac is None:
        errors.append('0.16 candidate receipts missing version-tokac command to establish compiler identity')
    elif isinstance(sdk_root, str) and sdk_root:
        sdk_tokac_bin = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac'))
        sdk_tokac_exe = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac.exe'))
        if expected_tokac not in (sdk_tokac_bin, sdk_tokac_exe):
            errors.append(f'0.16 version-tokac program {expected_tokac} does not match SDK_root {sdk_root}')

    # Full command-receipt validation for nested TKI negative commands
    if isinstance(tki_facts, dict):
        nested_items = []
        for neg_key in ('missing_interface_command', 'corrupt_interface_command'):
            if neg_key in tki_facts:
                nested_items.append((neg_key, tki_facts[neg_key], 'legacy_tki'))
        esc_dict = tki_facts.get('escape_negative_commands')
        if isinstance(esc_dict, dict):
            for mode_key in ('check', 'object', 'ir', 'compile'):
                if mode_key in esc_dict:
                    nested_items.append((f'escape_negative_commands {mode_key}', esc_dict[mode_key], mode_key))

        for desc, n_rec, kind in nested_items:
            if not isinstance(n_rec, dict):
                errors.append(f'0.16 source_hidden_tki {desc} must be a dictionary')
                continue
            # Required field presence and strict types
            if 'cwd' not in n_rec or not isinstance(n_rec['cwd'], str) or not n_rec['cwd']:
                errors.append(f'0.16 source_hidden_tki {desc} cwd missing or invalid')
            cwd_val = n_rec.get('cwd', '')

            if 'exit_code' not in n_rec or type(n_rec['exit_code']) is not int or isinstance(n_rec['exit_code'], bool) or n_rec['exit_code'] != 1:
                errors.append(f'0.16 source_hidden_tki {desc} exit_code must be exact integer 1')

            if 'signal' not in n_rec or n_rec['signal'] is not None:
                errors.append(f'0.16 source_hidden_tki {desc} signal must be explicit null')

            if 'termination' not in n_rec or n_rec['termination'] is not None:
                errors.append(f'0.16 source_hidden_tki {desc} termination must be explicit null')

            if 'error' not in n_rec or n_rec['error'] is not None:
                errors.append(f'0.16 source_hidden_tki {desc} error must be explicit null')

            if 'ok' not in n_rec or type(n_rec['ok']) is not bool or n_rec['ok'] is not True:
                errors.append(f'0.16 source_hidden_tki {desc} ok must be exact boolean True')

            if 'result' not in n_rec or n_rec['result'] != 'pass':
                errors.append(f'0.16 source_hidden_tki {desc} result must be pass')

            if 'artifact_absent' not in n_rec or type(n_rec['artifact_absent']) is not bool or n_rec['artifact_absent'] is not True:
                errors.append(f'0.16 source_hidden_tki {desc} artifact_absent must be exact boolean True')

            if 'execution_ms' not in n_rec:
                errors.append(f'0.16 source_hidden_tki {desc} execution_ms missing')
            else:
                ms = n_rec['execution_ms']
                if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
                    errors.append(f'0.16 source_hidden_tki {desc} execution_ms invalid')

            if 'argv' not in n_rec:
                errors.append(f'0.16 source_hidden_tki {desc} argv missing')
                n_argv = []
            elif not isinstance(n_rec['argv'], list) or not n_rec['argv'] or not all(isinstance(a, str) and a for a in n_rec['argv']):
                errors.append(f'0.16 source_hidden_tki {desc} argv invalid')
                n_argv = []
            else:
                n_argv = n_rec['argv']
                n_prog = os.path.normpath(n_argv[0].replace('\\', '/'))
                if expected_tokac and n_prog != expected_tokac:
                    errors.append(f'0.16 source_hidden_tki {desc} compiler mismatch: executes {n_prog}, expected {expected_tokac}')
                if os.path.basename(n_prog) not in ('tokac', 'tokac.exe'):
                    errors.append(f'0.16 source_hidden_tki {desc} compiler executable must be tokac')

            out_hash = n_rec.get('stdout_sha256')
            if not isinstance(out_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', out_hash):
                errors.append(f'0.16 source_hidden_tki {desc} stdout_sha256 missing or invalid')

            err_hash = n_rec.get('stderr_sha256')
            if not isinstance(err_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', err_hash):
                errors.append(f'0.16 source_hidden_tki {desc} stderr_sha256 missing or invalid')

            if 'stdout' not in n_rec or not isinstance(n_rec['stdout'], str):
                errors.append(f'0.16 source_hidden_tki {desc} stdout missing or not a string')
            else:
                out_text = n_rec['stdout']
                if isinstance(out_hash, str) and hashlib.sha256(out_text.encode('utf-8')).hexdigest() != out_hash:
                    errors.append(f'0.16 source_hidden_tki {desc} stdout content does not match stdout_sha256')

            if 'stderr' not in n_rec or not isinstance(n_rec['stderr'], str):
                errors.append(f'0.16 source_hidden_tki {desc} stderr missing or not a string')
                err_text = ''
            else:
                err_text = n_rec['stderr']
                if isinstance(err_hash, str) and hashlib.sha256(err_text.encode('utf-8')).hexdigest() != err_hash:
                    errors.append(f'0.16 source_hidden_tki {desc} stderr content does not match stderr_sha256')

            if kind == 'legacy_tki':
                if len(n_argv) != 7 or n_argv.count('-I') != 1 or n_argv.count('-o') != 1 or n_argv[1] != '-I' or n_argv[5] != '-o':
                    errors.append(f'0.16 source_hidden_tki {desc} argv template mismatch')
                else:
                    if not (n_argv[3].endswith('/consumer.tk') or n_argv[3] == 'consumer.tk'):
                        errors.append(f'0.16 source_hidden_tki {desc} missing consumer source argument')
                    if os.path.basename(n_argv[4]) != 'provider.o':
                        errors.append(f'0.16 source_hidden_tki {desc} missing provider.o object argument')
                    bad_bin_base = os.path.basename(n_argv[6])
                    if bad_bin_base not in ('bad_bin', 'bad_bin.bin', 'bad_bin.exe'):
                        errors.append(f'0.16 source_hidden_tki {desc} output basename mismatch')
                    if isinstance(cwd_val, str) and cwd_val:
                        norm_i = os.path.normpath(n_argv[2] if os.path.isabs(n_argv[2]) else os.path.join(cwd_val, n_argv[2]))
                        norm_cwd = os.path.normpath(cwd_val)
                        if norm_i != norm_cwd:
                            errors.append(f'0.16 source_hidden_tki {desc} -I directory must match cwd')
                if 'E0901' not in err_text:
                    errors.append(f'0.16 source_hidden_tki {desc} stderr missing E0901')
                if desc == 'missing_interface_command' and 'Could not open file' not in err_text:
                    errors.append(f'0.16 source_hidden_tki {desc} stderr missing missing-file diagnostic fact')
                elif desc == 'corrupt_interface_command' and 'Incompatible or stale interface file' not in err_text:
                    errors.append(f'0.16 source_hidden_tki {desc} stderr missing corrupt-file diagnostic fact')
            else:
                if 'E0455' not in err_text:
                    errors.append(f'0.16 source_hidden_tki {desc} stderr missing E0455')
                for crash_word in ('linker failed', 'Segmentation fault', 'segmentation fault', 'Assertion failed', 'core dumped'):
                    if crash_word in err_text:
                        errors.append(f'0.16 source_hidden_tki {desc} stderr contains non-semantic failure: {crash_word}')
                if 'diagnostic' in n_rec and n_rec['diagnostic'] != 'E0455':
                    errors.append(f'0.16 source_hidden_tki {desc} diagnostic must be E0455')

                if len(n_argv) > 2 and n_argv[1] == '-I' and isinstance(cwd_val, str) and cwd_val:
                    norm_i = os.path.normpath(n_argv[2] if os.path.isabs(n_argv[2]) else os.path.join(cwd_val, n_argv[2]))
                    norm_cwd = os.path.normpath(cwd_val)
                    if norm_i != norm_cwd:
                        errors.append(f'0.16 source_hidden_tki {desc} -I directory must match cwd')

                def _is_esc_src(s):
                    b = os.path.basename(s)
                    return b in ('level_a_cross_module_escape_negative.tk', 'escape.tk')

                if kind == 'check':
                    if len(n_argv) != 5 or n_argv[1] != '-I' or n_argv[3] != '--check-only' or not _is_esc_src(n_argv[4]):
                        errors.append(f'0.16 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'object':
                    if len(n_argv) != 7 or n_argv[1] != '-I' or n_argv[3] != '-c' or not _is_esc_src(n_argv[4]) or n_argv[5] != '-o' or not n_argv[6].endswith('.o'):
                        errors.append(f'0.16 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'ir':
                    if len(n_argv) != 8 or n_argv[1] != '-I' or n_argv[3] != '--emit-llvm' or n_argv[4] != '-c' or not _is_esc_src(n_argv[5]) or n_argv[6] != '-o' or not n_argv[7].endswith('.ll'):
                        errors.append(f'0.16 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'compile':
                    if len(n_argv) != 7 or n_argv[1] != '-I' or not _is_esc_src(n_argv[3]) or os.path.basename(n_argv[4]) != 'provider.o' or n_argv[5] != '-o' or os.path.basename(n_argv[6]) not in ('escape_bin', 'escape_bin.bin', 'escape_bin.exe'):
                        errors.append(f'0.16 source_hidden_tki {desc} argv template mismatch')

    compile_outputs = {}
    for r in receipts:
        if isinstance(r, dict):
            name = str(r.get('name', ''))
            argv = r.get('argv') or r.get('cmd')
            cwd = r.get('cwd')
            if isinstance(argv, list) and isinstance(cwd, str) and cwd and '-o' in argv:
                if argv.count('-o') != 1:
                    errors.append(f'0.16 compile command must have unique -o flag: {name}')
                idx = argv.index('-o')
                if idx + 1 < len(argv) and isinstance(argv[idx + 1], str) and argv[idx + 1]:
                    raw_out = argv[idx + 1]
                    norm_out = os.path.normpath(raw_out if os.path.isabs(raw_out) else os.path.join(cwd, raw_out))
                    compile_outputs[name] = norm_out

    run_to_compile = {
        'pos-tki-consumer-run': ('pos-tki-consumer-compile', 'tki_bin'),
    }
    for case in POS_016_CASES:
        run_to_compile[f'pos-{case}-run'] = (f'pos-{case}-compile', f'{case}_bin')

    compile_expected_bases = {
        'pos-tki-provider-compile': ('provider.o',),
        'pos-tki-consumer-compile': ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'),
    }
    for base_name, _ in NEG_016_MULTI_MODE_CASES:
        compile_expected_bases[f'neg-{base_name}-object'] = (f'{base_name}.o',)
        compile_expected_bases[f'neg-{base_name}-ir'] = (f'{base_name}.ll',)
        compile_expected_bases[f'neg-{base_name}-compile'] = (f'{base_name}.bin', f'{base_name}.exe', base_name)
    for case in POS_016_CASES:
        compile_expected_bases[f'pos-{case}-compile'] = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')

    seen_receipts = set()
    for r in receipts:
        if not isinstance(r, dict):
            errors.append('0.16 command receipt is not a dict')
            continue
        r_name = str(r.get('name', ''))
        if not r_name:
            errors.append('0.16 command receipt missing name')
            continue
        if r_name in seen_receipts:
            errors.append('0.16 duplicate command receipt name: ' + r_name)
        seen_receipts.add(r_name)

        if r.get('ok') is not True:
            errors.append('0.16 command receipt is not marked ok: ' + r_name)
        if r.get('result') != 'pass':
            errors.append('0.16 command receipt result is not pass: ' + r_name)

        if 'signal' not in r or r['signal'] is not None:
            errors.append('0.16 command receipt missing signal key or abnormal signal: ' + r_name)
        if r.get('termination') is not None or r.get('error') is not None:
            errors.append('0.16 command receipt indicates abnormal termination or error: ' + r_name)

        argv = r.get('argv') or r.get('cmd')
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
            errors.append('0.16 command receipt argv invalid: ' + r_name)
            continue

        cwd = r.get('cwd')
        if not isinstance(cwd, str) or not cwd:
            errors.append('0.16 command receipt cwd missing or invalid: ' + r_name)
            cwd = ''

        prog = argv[0].replace('\\', '/')
        if prog in ('/bin/true', '/bin/false', 'true', 'false') or prog.endswith('/true') or prog.endswith('/false'):
            errors.append('0.16 command receipt program cannot be no-op dummy: ' + r_name)

        norm_prog = os.path.normpath(prog if os.path.isabs(prog) else os.path.join(cwd, prog))
        prog_base = os.path.basename(norm_prog)

        ms = r.get('execution_ms')
        if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
            errors.append('0.16 command receipt execution_ms missing or invalid: ' + r_name)

        exit_code = r.get('exit_code')
        if type(exit_code) is not int or isinstance(exit_code, bool):
            errors.append('0.16 command receipt exit_code missing or not int: ' + r_name)
            continue

        if 'stdout' not in r or not isinstance(r.get('stdout'), str):
            errors.append('0.16 command receipt stdout missing or not a string: ' + r_name)
            out_s = ''
        else:
            out_s = r['stdout']

        if 'stderr' not in r or not isinstance(r.get('stderr'), str):
            errors.append('0.16 command receipt stderr missing or not a string: ' + r_name)
            err_s = ''
        else:
            err_s = r['stderr']

        if 'stdout_sha256' not in r or hashlib.sha256(out_s.encode('utf-8')).hexdigest() != r['stdout_sha256']:
            errors.append('0.16 command receipt stdout_sha256 mismatch: ' + r_name)
        if 'stderr_sha256' not in r or hashlib.sha256(err_s.encode('utf-8')).hexdigest() != r['stderr_sha256']:
            errors.append('0.16 command receipt stderr_sha256 mismatch: ' + r_name)

        if 'failure instead of assertions' in out_s or 'assertion failed' in out_s.lower():
            errors.append('0.16 command receipt stdout indicates failure: ' + r_name)

        is_neg = r_name.startswith('neg-')
        is_pos_compile = r_name.startswith('pos-') and r_name.endswith('-compile')
        if (is_pos_compile or is_neg):
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.16 compile command program must execute tokac: ' + r_name)
            elif expected_tokac and norm_prog != expected_tokac:
                errors.append(f'0.16 compile command program mismatch with SDK tokac: {r_name} (executes {norm_prog}, expected {expected_tokac})')
            for f in ('--version', '-v', '-V', '--help', '-h'):
                if f in argv:
                    errors.append(f'0.16 compile/check command contains incompatible flag {f}: {r_name}')

        if not is_neg:
            if exit_code != 0:
                errors.append(f'0.16 positive command exit code must be 0: {r_name} (got {exit_code})')
            if r.get('diagnostic') is not None:
                errors.append('0.16 unexpected diagnostic on positive command: ' + r_name)
            if err_s.strip() and 'warning' not in err_s.lower():
                errors.append('0.16 unexpected stderr in positive command: ' + r_name)
        else:
            if exit_code != 1:
                errors.append(f'0.16 negative command exit code must be 1: {r_name} (got {exit_code})')

        # Exact argv template validation by command role
        if r_name.startswith('version-'):
            t_name = r_name.removeprefix('version-')
            if len(argv) != 2 or argv[1] != '--version':
                errors.append(f'0.16 tool version command argv template mismatch: {r_name}')
            if t_name == 'toka':
                if prog_base not in ('toka', 'toka.exe'):
                    errors.append('0.16 tool version command program mismatch: ' + r_name)
            else:
                if prog_base not in (t_name, t_name + '.exe'):
                    errors.append('0.16 tool version command program mismatch: ' + r_name)
            if isinstance(sdk_root, str) and sdk_root:
                sdk_tool_bin = os.path.normpath(os.path.join(sdk_root, 'bin', t_name))
                sdk_tool_exe = os.path.normpath(os.path.join(sdk_root, 'bin', t_name + '.exe'))
                if norm_prog not in (sdk_tool_bin, sdk_tool_exe):
                    errors.append(f'0.16 tool version program {norm_prog} does not match SDK_root {sdk_root}: {r_name}')
            if not ver_regex.search(out_s):
                errors.append(f'0.16 tool version {t_name} stdout missing {ver}: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-run'):
            if len(argv) != 1:
                errors.append(f'0.16 run command argv template mismatch (expected only executable): {r_name}')
            comp_name, exp_base = run_to_compile.get(r_name, ('', ''))
            expected_compiled_out = compile_outputs.get(comp_name)
            if not expected_compiled_out:
                errors.append(f'0.16 missing matching compile output for run command: {r_name} (needs {comp_name})')
            elif norm_prog != expected_compiled_out:
                errors.append(f'0.16 run program does not match compile output: {r_name} (executes {norm_prog}, compiled {expected_compiled_out})')
            valid_bases = (exp_base, exp_base + '.bin', exp_base + '.exe')
            if prog_base not in valid_bases:
                errors.append(f'0.16 run program basename mismatch: {r_name} (got {prog_base}, expected one of {valid_bases})')

        elif r_name == 'pos-tki-provider-compile':
            if len(argv) != 6 or argv.count('-c') != 1 or argv.count('--emit-interface') != 1 or argv.count('-o') != 1:
                errors.append(f'0.16 pos-tki-provider-compile argv template mismatch: {r_name}')
            if not (argv[2].endswith('/scoped_provider.tk') or argv[2] == 'scoped_provider.tk'):
                errors.append(f'0.16 pos-tki-provider-compile fixture mismatch: {r_name}')
            if len(argv) >= 6:
                out_path = os.path.normpath(argv[5] if os.path.isabs(argv[5]) else os.path.join(cwd, argv[5]))
                if os.path.basename(out_path) != 'provider.o':
                    errors.append(f'0.16 pos-tki-provider-compile output basename mismatch: {r_name}')

        elif r_name == 'pos-tki-consumer-compile':
            if len(argv) != 7 or argv.count('-I') != 1 or argv.count('-o') != 1:
                errors.append(f'0.16 pos-tki-consumer-compile argv template mismatch: {r_name}')
            if len(argv) >= 7:
                out_path = os.path.normpath(argv[6] if os.path.isabs(argv[6]) else os.path.join(cwd, argv[6]))
                if os.path.basename(out_path) not in ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'):
                    errors.append(f'0.16 pos-tki-consumer-compile output basename mismatch: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-compile'):
            case = r_name.removeprefix('pos-').removesuffix('-compile')
            if len(argv) != 4 or argv.count('-o') != 1 or argv[2] != '-o':
                errors.append(f'0.16 positive compile command argv length mismatch: expected 4, got {len(argv)}: {r_name}')
            if not (argv[1].endswith('/' + case + '.tk') or argv[1] == (case + '.tk')):
                errors.append(f'0.16 compile fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                exp_bases = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')
                if os.path.basename(out_path) not in exp_bases:
                    errors.append(f'0.16 positive compile command output basename mismatch: {r_name}')

        elif is_neg and r_name.endswith('-object'):
            case = r_name.removeprefix('neg-').removesuffix('-object')
            if len(argv) != 5 or argv.count('-c') != 1 or argv.count('-o') != 1 or argv[1] != '-c' or argv[3] != '-o':
                errors.append(f'0.16 negative object command template mismatch: {r_name}')
            if not (argv[2].endswith(f'/{case}.tk') or argv[2] == f'{case}.tk'):
                errors.append(f'0.16 negative fixture mismatch: {r_name}')
            if len(argv) >= 5:
                out_path = os.path.normpath(argv[4] if os.path.isabs(argv[4]) else os.path.join(cwd, argv[4]))
                if os.path.basename(out_path) != f'{case}.o':
                    errors.append(f'0.16 negative object output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.16 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-ir'):
            case = r_name.removeprefix('neg-').removesuffix('-ir')
            if len(argv) != 6 or argv.count('--emit-llvm') != 1 or argv.count('-c') != 1 or argv.count('-o') != 1 or \
                    argv[1] != '--emit-llvm' or argv[2] != '-c' or argv[4] != '-o':
                errors.append(f'0.16 negative ir command template mismatch: {r_name}')
            if not (argv[3].endswith(f'/{case}.tk') or argv[3] == f'{case}.tk'):
                errors.append(f'0.16 negative fixture mismatch: {r_name}')
            if len(argv) >= 6:
                out_path = os.path.normpath(argv[5] if os.path.isabs(argv[5]) else os.path.join(cwd, argv[5]))
                if os.path.basename(out_path) != f'{case}.ll':
                    errors.append(f'0.16 negative ir output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.16 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-compile'):
            case = r_name.removeprefix('neg-').removesuffix('-compile')
            if len(argv) != 4 or argv.count('-o') != 1 or argv[2] != '-o':
                errors.append(f'0.16 negative compile command template mismatch: {r_name}')
            if not (argv[1].endswith(f'/{case}.tk') or argv[1] == f'{case}.tk'):
                errors.append(f'0.16 negative fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                if os.path.basename(out_path) not in (f'{case}.bin', f'{case}.exe', case):
                    errors.append(f'0.16 negative compile output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.16 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-check'):
            if len(argv) != 3 or argv.count('--check-only') != 1 or argv[1] != '--check-only':
                errors.append(f'0.16 negative check command template mismatch: {r_name}')
            exp_fix = NEG_016_CHECK_FIXTURES.get(r_name)
            if exp_fix:
                if not (argv[2].endswith('/' + exp_fix) or argv[2] == exp_fix):
                    errors.append(f'0.16 negative fixture mismatch: {r_name}')
            else:
                fix_name = r_name.removeprefix('neg-').removesuffix('-check')
                if not (argv[2].endswith('/' + fix_name + '.tk') or argv[2] == (fix_name + '.tk')):
                    errors.append(f'0.16 negative fixture mismatch: {r_name}')

        if r_name in NEG_016_DIAGNOSTICS:
            exp_diag = NEG_016_DIAGNOSTICS[r_name]
            if r.get('diagnostic') != exp_diag:
                errors.append(f'0.16 negative command diagnostic mismatch: {r_name} (expected {exp_diag}, got {r.get("diagnostic")})')
            if exp_diag not in err_s:
                errors.append(f'0.16 negative command stderr missing {exp_diag}: {r_name}')

    missing = EXPECTED_016_RECEIPTS - seen_receipts
    if missing:
        errors.append('Missing 0.16 command receipts: ' + ', '.join(sorted(missing)))

    return errors


EXPECTED_017_FIXTURES_DIGEST = '7c18eb0f463654ab8ed3d4c8d5fbf2e75bf3c0a5a18e6219dd5ef66663c3e19e'


def expected_017_script_sha256():
    m = get_bundle_manifest()
    if m and isinstance(m.get('modules'), dict):
        return m['modules'].get('test_0_17_candidate.py')
    return None


EXPECTED_017_VERSION_RECEIPTS = {
    'version-tokac',
    'version-toka',
    'version-tokafmt',
    'version-tokalsp',
}

POS_017_CASES = [
    'pos1_d1_m3_carrier_consume',
    'pos2_d2_m3_carrier_fwd',
    'pos3_d5_m2_split_wrapper',
    'pos7_owned_task_start',
    'pos8_dual_scope_drain_both',
    'pos9_owned_carrier_split_var_start',
    'r2_p1_1a_two_tasks_inline_owned_start_pos',
    'r2_p1_1b_two_tasks_split_var_owned_start_pos',
    'r3_p1_1a_two_tasks_fwd_call_owned_start_pos',
    'r5_pos1_cede_self_receiver_start',
]

EXPECTED_017_POS_RECEIPTS = {
    'pos-tki-provider-compile',
    'pos-tki-consumer-compile',
    'pos-tki-consumer-run',
}
for case in POS_017_CASES:
    EXPECTED_017_POS_RECEIPTS.add(f'pos-{case}-compile')
    EXPECTED_017_POS_RECEIPTS.add(f'pos-{case}-run')

NEG_017_MULTI_MODE_CASES = [
    ('leak1_d3_m2_split_var', 'E04584'),
    ('leak2_d3_m3_unboxed_field', 'E04584'),
    ('leak6_d4_m3_field_escape', 'E0456'),
    ('leak7_d5_m3_dual_scope', 'E0456'),
    ('leak8_cast_cede_leak3_outer_whole_reassign', 'E0456'),
    ('leak9_dual_carrier_method_spawn_s1_join_s2', 'E0456'),
    ('leak10_method_receiver_start_bypass_then_join_empty_s1_split_var', 'E0456'),
    ('r2_p0_1a_nested_carrier_split_var_start', 'E04584'),
    ('r2_p0_2a_effects_self_taskscope_start_bypass', 'E0456'),
    ('r3_p0_1a_holder_method_set_task_then_start', 'E04584'),
    ('r4_p0_1_nested_field_assign_then_start', 'E04584'),
    ('r5_neg1_borrowed_self_field_postfix_start', 'E04584'),
    ('r5_neg2_cede_self_carrier_with_borrowed_task_start', 'E04584'),
]

NEG_017_DIAGNOSTICS = {
    'neg-r2_p0_1b_nested_carrier_inline_start-check': 'E04584',
    'neg-r2_p0_1c_nested_carrier_two_step_var_start-check': 'E04584',
    'neg-r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass-check': 'E0456',
    'neg-r3_p0_1b_holder_reassign_via_fwd_call_then_start-check': 'E04584',
    'neg-r3_p0_2a_effects_self_indirect_helper_start-check': 'E0456',
    'neg-r4_p0_2a_effects_self_match_start-check': 'E0456',
}
for base_name, diag in NEG_017_MULTI_MODE_CASES:
    NEG_017_DIAGNOSTICS[f'neg-{base_name}-check'] = diag
    NEG_017_DIAGNOSTICS[f'neg-{base_name}-object'] = diag
    NEG_017_DIAGNOSTICS[f'neg-{base_name}-ir'] = diag
    NEG_017_DIAGNOSTICS[f'neg-{base_name}-compile'] = diag

NEG_017_CHECK_FIXTURES = {
    'neg-r2_p0_1b_nested_carrier_inline_start-check': 'r2_p0_1b_nested_carrier_inline_start.tk',
    'neg-r2_p0_1c_nested_carrier_two_step_var_start-check': 'r2_p0_1c_nested_carrier_two_step_var_start.tk',
    'neg-r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass-check': 'r2_p0_2b_effects_self_dual_carrier_s1_join_s2_bypass.tk',
    'neg-r3_p0_1b_holder_reassign_via_fwd_call_then_start-check': 'r3_p0_1b_holder_reassign_via_fwd_call_then_start.tk',
    'neg-r3_p0_2a_effects_self_indirect_helper_start-check': 'r3_p0_2a_effects_self_indirect_helper_start.tk',
    'neg-r4_p0_2a_effects_self_match_start-check': 'r4_p0_2a_effects_self_match_start.tk',
}
for base_name, _ in NEG_017_MULTI_MODE_CASES:
    NEG_017_CHECK_FIXTURES[f'neg-{base_name}-check'] = f'{base_name}.tk'

EXPECTED_017_NEG_RECEIPTS = set(NEG_017_DIAGNOSTICS.keys())
EXPECTED_017_RECEIPTS = (
    EXPECTED_017_VERSION_RECEIPTS | EXPECTED_017_POS_RECEIPTS | EXPECTED_017_NEG_RECEIPTS
)


def candidate_017_receipt_errors(receipt, revision, label, target=None):
    if not isinstance(receipt, dict):
        return ['0.17 candidate receipt is missing or not a dict']
    errors = []
    if receipt.get('schema') != 'toka.0.17-candidate-controls':
        errors.append('0.17 receipt schema mismatch')
    version = receipt.get('version')
    if type(version) is not int or isinstance(version, bool) or version != 1:
        errors.append('0.17 receipt version must be int 1')
    if receipt.get('result') != 'pass':
        errors.append('0.17 receipt result is not pass')
    if receipt.get('candidate_revision') != revision:
        errors.append('0.17 candidate revision mismatch')
    if receipt.get('version_label') != label:
        errors.append('0.17 version label mismatch')
    target_val = receipt.get('target')
    if not isinstance(target_val, str) or not target_val:
        errors.append('0.17 target missing or invalid')
    elif target is not None and target_val != target:
        errors.append('0.17 target mismatch: expected %s, got %s' % (target, target_val))
    if receipt.get('build_testing') is not False:
        errors.append('0.17 receipt build_testing must be False')
    if receipt.get('groups') != ['G-compositional-convergence']:
        errors.append('0.17 receipt groups mismatch')
    if receipt.get('fixtures_digest') != EXPECTED_017_FIXTURES_DIGEST:
        errors.append('0.17 fixtures digest mismatch')

    if receipt.get('control_script_name') != 'test_0_17_candidate.py':
        errors.append('0.17 control script name mismatch')
    control_sha = receipt.get('control_script_sha256')
    expected_control_sha = expected_017_script_sha256()
    if not isinstance(control_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', control_sha):
        errors.append('0.17 control script sha256 missing or invalid')
    elif expected_control_sha and control_sha != expected_control_sha:
        errors.append('0.17 control script digest mismatch')

    # Source-hidden TKI facts validation
    tki_facts = receipt.get('source_hidden_tki')
    if not isinstance(tki_facts, dict):
        errors.append('0.17 receipt missing source_hidden_tki dictionary')
    else:
        if tki_facts.get('missing_interface_rejected') is not True:
            errors.append('0.17 source_hidden_tki missing_interface_rejected must be True')
        if tki_facts.get('corrupt_interface_rejected') is not True:
            errors.append('0.17 source_hidden_tki corrupt_interface_rejected must be True')
        if tki_facts.get('consumer_result') != 'pass':
            errors.append('0.17 source_hidden_tki consumer_result must be pass')
        psp = tki_facts.get('provider_source_present')
        if psp is not False or type(psp) is not bool:
            errors.append('0.17 source_hidden_tki provider_source_present must exist and be exactly boolean False')
        p_tki = tki_facts.get('provider_tki_sha256')
        if not isinstance(p_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', p_tki) or len(set(p_tki)) <= 4:
            errors.append('0.17 source_hidden_tki provider_tki_sha256 missing, invalid or unbound dummy hash')
        p_obj = tki_facts.get('provider_object_sha256')
        if not isinstance(p_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', p_obj) or len(set(p_obj)) <= 4:
            errors.append('0.17 source_hidden_tki provider_object_sha256 missing, invalid or unbound dummy hash')

        c_tki = tki_facts.get('consumed_tki_sha256')
        if not isinstance(c_tki, str) or not re.fullmatch(r'[0-9a-f]{64}', c_tki):
            errors.append('0.17 source_hidden_tki consumed_tki_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_tki != p_tki:
            errors.append('0.17 source_hidden_tki consumed_tki_sha256 mismatch with provider_tki_sha256')

        c_obj = tki_facts.get('consumed_object_sha256')
        if not isinstance(c_obj, str) or not re.fullmatch(r'[0-9a-f]{64}', c_obj):
            errors.append('0.17 source_hidden_tki consumed_object_sha256 is mandatory and must be a 64-hex SHA-256')
        elif c_obj != p_obj:
            errors.append('0.17 source_hidden_tki consumed_object_sha256 mismatch with provider_object_sha256')

        if 'missing_interface_command' not in tki_facts:
            errors.append('0.17 source_hidden_tki missing_interface_command is mandatory')
        if 'corrupt_interface_command' not in tki_facts:
            errors.append('0.17 source_hidden_tki corrupt_interface_command is mandatory')
        if 'escape_negative_commands' not in tki_facts or not isinstance(tki_facts['escape_negative_commands'], dict):
            errors.append('0.17 source_hidden_tki escape_negative_commands is mandatory and must be a dictionary')
        elif set(tki_facts['escape_negative_commands'].keys()) != {'check', 'object', 'ir', 'compile'}:
            errors.append('0.17 source_hidden_tki escape_negative_commands must contain exactly [check, object, ir, compile]')
        if tki_facts.get('escape_negative_rejected') is not True or type(tki_facts.get('escape_negative_rejected')) is not bool:
            errors.append('0.17 source_hidden_tki escape_negative_rejected must be boolean True')

    ver = label.removeprefix('v')
    ver_regex = re.compile(r'(?<![0-9a-zA-Z._+-])' + re.escape(ver) + r'(?![0-9a-zA-Z._+-])')

    tools = receipt.get('four_tool_versions')
    if not isinstance(tools, dict) or set(tools.keys()) != {'tokac', 'toka', 'tokafmt', 'tokalsp'}:
        errors.append('0.17 four_tool_versions missing or invalid')
    else:
        for t_name, t_str in tools.items():
            if not isinstance(t_str, str) or not ver_regex.search(t_str):
                errors.append('0.17 tool version mismatch: ' + t_name)

    counts = receipt.get('counts')
    expected_groups = {'version': 4, 'G-compositional-convergence-positive': 23, 'G-compositional-convergence-negative': 58}
    if not isinstance(counts, dict):
        errors.append('0.17 counts missing or not a dict')
    else:
        for k in ('total_receipts', 'passed', 'failed'):
            val = counts.get(k)
            if type(val) is not int or isinstance(val, bool):
                errors.append(f'0.17 counts {k} must be exact int')
        if counts.get('total_receipts') != 85 or counts.get('passed') != 85 or counts.get('failed') != 0:
            errors.append('0.17 counts values mismatch: expected total 85, passed 85, failed 0')
        groups = counts.get('groups')
        if not isinstance(groups, dict) or groups != expected_groups:
            errors.append('0.17 counts groups mismatch or invalid')
        else:
            for g_name, g_cnt in groups.items():
                if type(g_cnt) is not int or isinstance(g_cnt, bool):
                    errors.append(f'0.17 counts groups {g_name} must be exact int')

    receipts = receipt.get('receipts')
    if not isinstance(receipts, list) or len(receipts) != 85:
        errors.append('0.17 command receipts list must have exactly 85 entries')
        return errors

    sdk_root = receipt.get('SDK_root')
    if not isinstance(sdk_root, str) or not sdk_root:
        errors.append('0.17 SDK_root missing or invalid')

    expected_tokac = None
    for r in receipts:
        if isinstance(r, dict) and r.get('name') == 'version-tokac':
            v_argv = r.get('argv') or r.get('cmd')
            v_cwd = r.get('cwd')
            if isinstance(v_argv, list) and v_argv and isinstance(v_cwd, str):
                v_prog = v_argv[0].replace('\\', '/')
                expected_tokac = os.path.normpath(v_prog if os.path.isabs(v_prog) else os.path.join(v_cwd, v_prog))
            break

    if expected_tokac is None:
        errors.append('0.17 candidate receipts missing version-tokac command to establish compiler identity')
    elif isinstance(sdk_root, str) and sdk_root:
        sdk_tokac_bin = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac'))
        sdk_tokac_exe = os.path.normpath(os.path.join(sdk_root, 'bin', 'tokac.exe'))
        if expected_tokac not in (sdk_tokac_bin, sdk_tokac_exe):
            errors.append(f'0.17 version-tokac program {expected_tokac} does not match SDK_root {sdk_root}')

    # Full command-receipt validation for nested TKI negative commands
    if isinstance(tki_facts, dict):
        nested_items = []
        for neg_key in ('missing_interface_command', 'corrupt_interface_command'):
            if neg_key in tki_facts:
                nested_items.append((neg_key, tki_facts[neg_key], 'legacy_tki'))
        esc_dict = tki_facts.get('escape_negative_commands')
        if isinstance(esc_dict, dict):
            for mode_key in ('check', 'object', 'ir', 'compile'):
                if mode_key in esc_dict:
                    nested_items.append((f'escape_negative_commands {mode_key}', esc_dict[mode_key], mode_key))

        for desc, n_rec, kind in nested_items:
            if not isinstance(n_rec, dict):
                errors.append(f'0.17 source_hidden_tki {desc} must be a dictionary')
                continue
            if 'cwd' not in n_rec or not isinstance(n_rec['cwd'], str) or not n_rec['cwd']:
                errors.append(f'0.17 source_hidden_tki {desc} cwd missing or invalid')
            cwd_val = n_rec.get('cwd', '')

            if 'exit_code' not in n_rec or type(n_rec['exit_code']) is not int or isinstance(n_rec['exit_code'], bool) or n_rec['exit_code'] != 1:
                errors.append(f'0.17 source_hidden_tki {desc} exit_code must be exact integer 1')

            if 'signal' not in n_rec or n_rec['signal'] is not None:
                errors.append(f'0.17 source_hidden_tki {desc} signal must be explicit null')

            if 'termination' not in n_rec or n_rec['termination'] is not None:
                errors.append(f'0.17 source_hidden_tki {desc} termination must be explicit null')

            if 'error' not in n_rec or n_rec['error'] is not None:
                errors.append(f'0.17 source_hidden_tki {desc} error must be explicit null')

            if 'ok' not in n_rec or type(n_rec['ok']) is not bool or n_rec['ok'] is not True:
                errors.append(f'0.17 source_hidden_tki {desc} ok must be exact boolean True')

            if 'result' not in n_rec or n_rec['result'] != 'pass':
                errors.append(f'0.17 source_hidden_tki {desc} result must be pass')

            if 'artifact_absent' not in n_rec or type(n_rec['artifact_absent']) is not bool or n_rec['artifact_absent'] is not True:
                errors.append(f'0.17 source_hidden_tki {desc} artifact_absent must be exact boolean True')

            if 'execution_ms' not in n_rec:
                errors.append(f'0.17 source_hidden_tki {desc} execution_ms missing')
            else:
                ms = n_rec['execution_ms']
                if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
                    errors.append(f'0.17 source_hidden_tki {desc} execution_ms invalid')

            if 'argv' not in n_rec:
                errors.append(f'0.17 source_hidden_tki {desc} argv missing')
                n_argv = []
            elif not isinstance(n_rec['argv'], list) or not n_rec['argv'] or not all(isinstance(a, str) and a for a in n_rec['argv']):
                errors.append(f'0.17 source_hidden_tki {desc} argv invalid')
                n_argv = []
            else:
                n_argv = n_rec['argv']
                n_prog = os.path.normpath(n_argv[0].replace('\\', '/'))
                if expected_tokac and n_prog != expected_tokac:
                    errors.append(f'0.17 source_hidden_tki {desc} compiler mismatch: executes {n_prog}, expected {expected_tokac}')
                if os.path.basename(n_prog) not in ('tokac', 'tokac.exe'):
                    errors.append(f'0.17 source_hidden_tki {desc} compiler executable must be tokac')

            out_hash = n_rec.get('stdout_sha256')
            if not isinstance(out_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', out_hash):
                errors.append(f'0.17 source_hidden_tki {desc} stdout_sha256 missing or invalid')

            err_hash = n_rec.get('stderr_sha256')
            if not isinstance(err_hash, str) or not re.fullmatch(r'[0-9a-f]{64}', err_hash):
                errors.append(f'0.17 source_hidden_tki {desc} stderr_sha256 missing or invalid')

            if 'stdout' not in n_rec or not isinstance(n_rec['stdout'], str):
                errors.append(f'0.17 source_hidden_tki {desc} stdout missing or not a string')
            else:
                out_text = n_rec['stdout']
                if isinstance(out_hash, str) and hashlib.sha256(out_text.encode('utf-8')).hexdigest() != out_hash:
                    errors.append(f'0.17 source_hidden_tki {desc} stdout content does not match stdout_sha256')

            if 'stderr' not in n_rec or not isinstance(n_rec['stderr'], str):
                errors.append(f'0.17 source_hidden_tki {desc} stderr missing or not a string')
                err_text = ''
            else:
                err_text = n_rec['stderr']
                if isinstance(err_hash, str) and hashlib.sha256(err_text.encode('utf-8')).hexdigest() != err_hash:
                    errors.append(f'0.17 source_hidden_tki {desc} stderr content does not match stderr_sha256')

            if kind == 'legacy_tki':
                if len(n_argv) != 7 or n_argv.count('-I') != 1 or n_argv.count('-o') != 1 or n_argv[1] != '-I' or n_argv[5] != '-o':
                    errors.append(f'0.17 source_hidden_tki {desc} argv template mismatch')
                else:
                    if not (n_argv[3].endswith('/consumer.tk') or n_argv[3] == 'consumer.tk'):
                        errors.append(f'0.17 source_hidden_tki {desc} missing consumer source argument')
                    if os.path.basename(n_argv[4]) != 'provider.o':
                        errors.append(f'0.17 source_hidden_tki {desc} missing provider.o object argument')
                    bad_bin_base = os.path.basename(n_argv[6])
                    if bad_bin_base not in ('bad_bin', 'bad_bin.bin', 'bad_bin.exe'):
                        errors.append(f'0.17 source_hidden_tki {desc} output basename mismatch')
                    if isinstance(cwd_val, str) and cwd_val:
                        norm_i = os.path.normpath(n_argv[2] if os.path.isabs(n_argv[2]) else os.path.join(cwd_val, n_argv[2]))
                        norm_cwd = os.path.normpath(cwd_val)
                        if norm_i != norm_cwd:
                            errors.append(f'0.17 source_hidden_tki {desc} -I directory must match cwd')
                if 'E0901' not in err_text:
                    errors.append(f'0.17 source_hidden_tki {desc} stderr missing E0901')
                if desc == 'missing_interface_command' and 'Could not open file' not in err_text:
                    errors.append(f'0.17 source_hidden_tki {desc} stderr missing missing-file diagnostic fact')
                elif desc == 'corrupt_interface_command' and 'Incompatible or stale interface file' not in err_text:
                    errors.append(f'0.17 source_hidden_tki {desc} stderr missing corrupt-file diagnostic fact')
            else:
                if 'E0455' not in err_text:
                    errors.append(f'0.17 source_hidden_tki {desc} stderr missing E0455')
                for crash_word in ('linker failed', 'Segmentation fault', 'segmentation fault', 'Assertion failed', 'core dumped'):
                    if crash_word in err_text:
                        errors.append(f'0.17 source_hidden_tki {desc} stderr contains non-semantic failure: {crash_word}')
                if 'diagnostic' in n_rec and n_rec['diagnostic'] != 'E0455':
                    errors.append(f'0.17 source_hidden_tki {desc} diagnostic must be E0455')

                if len(n_argv) > 2 and n_argv[1] == '-I' and isinstance(cwd_val, str) and cwd_val:
                    norm_i = os.path.normpath(n_argv[2] if os.path.isabs(n_argv[2]) else os.path.join(cwd_val, n_argv[2]))
                    norm_cwd = os.path.normpath(cwd_val)
                    if norm_i != norm_cwd:
                        errors.append(f'0.17 source_hidden_tki {desc} -I directory must match cwd')

                def _is_esc_src(s):
                    b = os.path.basename(s)
                    return b in ('level_a_cross_module_escape_negative.tk', 'escape.tk')

                if kind == 'check':
                    if len(n_argv) != 5 or n_argv[1] != '-I' or n_argv[3] != '--check-only' or not _is_esc_src(n_argv[4]):
                        errors.append(f'0.17 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'object':
                    if len(n_argv) != 7 or n_argv[1] != '-I' or n_argv[3] != '-c' or not _is_esc_src(n_argv[4]) or n_argv[5] != '-o' or not n_argv[6].endswith('.o'):
                        errors.append(f'0.17 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'ir':
                    if len(n_argv) != 8 or n_argv[1] != '-I' or n_argv[3] != '--emit-llvm' or n_argv[4] != '-c' or not _is_esc_src(n_argv[5]) or n_argv[6] != '-o' or not n_argv[7].endswith('.ll'):
                        errors.append(f'0.17 source_hidden_tki {desc} argv template mismatch')
                elif kind == 'compile':
                    if len(n_argv) != 7 or n_argv[1] != '-I' or not _is_esc_src(n_argv[3]) or os.path.basename(n_argv[4]) != 'provider.o' or n_argv[5] != '-o' or os.path.basename(n_argv[6]) not in ('escape_bin', 'escape_bin.bin', 'escape_bin.exe'):
                        errors.append(f'0.17 source_hidden_tki {desc} argv template mismatch')

    compile_outputs = {}
    for r in receipts:
        if isinstance(r, dict):
            name = str(r.get('name', ''))
            argv = r.get('argv') or r.get('cmd')
            cwd = r.get('cwd')
            if isinstance(argv, list) and isinstance(cwd, str) and cwd and '-o' in argv:
                if argv.count('-o') != 1:
                    errors.append(f'0.17 compile command must have unique -o flag: {name}')
                idx = argv.index('-o')
                if idx + 1 < len(argv) and isinstance(argv[idx + 1], str) and argv[idx + 1]:
                    raw_out = argv[idx + 1]
                    norm_out = os.path.normpath(raw_out if os.path.isabs(raw_out) else os.path.join(cwd, raw_out))
                    compile_outputs[name] = norm_out

    run_to_compile = {
        'pos-tki-consumer-run': ('pos-tki-consumer-compile', 'tki_bin'),
    }
    for case in POS_017_CASES:
        run_to_compile[f'pos-{case}-run'] = (f'pos-{case}-compile', f'{case}_bin')

    compile_expected_bases = {
        'pos-tki-provider-compile': ('provider.o',),
        'pos-tki-consumer-compile': ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'),
    }
    for base_name, _ in NEG_017_MULTI_MODE_CASES:
        compile_expected_bases[f'neg-{base_name}-object'] = (f'{base_name}.o',)
        compile_expected_bases[f'neg-{base_name}-ir'] = (f'{base_name}.ll',)
        compile_expected_bases[f'neg-{base_name}-compile'] = (f'{base_name}.bin', f'{base_name}.exe', base_name)
    for case in POS_017_CASES:
        compile_expected_bases[f'pos-{case}-compile'] = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')

    seen_receipts = set()
    for r in receipts:
        if not isinstance(r, dict):
            errors.append('0.17 command receipt is not a dict')
            continue
        r_name = str(r.get('name', ''))
        if not r_name:
            errors.append('0.17 command receipt missing name')
            continue
        if r_name in seen_receipts:
            errors.append('0.17 duplicate command receipt name: ' + r_name)
        seen_receipts.add(r_name)

        if r.get('ok') is not True:
            errors.append('0.17 command receipt is not marked ok: ' + r_name)
        if r.get('result') != 'pass':
            errors.append('0.17 command receipt result is not pass: ' + r_name)

        if 'signal' not in r or r['signal'] is not None:
            errors.append('0.17 command receipt missing signal key or abnormal signal: ' + r_name)
        if r.get('termination') is not None or r.get('error') is not None:
            errors.append('0.17 command receipt indicates abnormal termination or error: ' + r_name)

        argv = r.get('argv') or r.get('cmd')
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
            errors.append('0.17 command receipt argv invalid: ' + r_name)
            continue

        cwd = r.get('cwd')
        if not isinstance(cwd, str) or not cwd:
            errors.append('0.17 command receipt cwd missing or invalid: ' + r_name)
            cwd = ''

        prog = argv[0].replace('\\', '/')
        if prog in ('/bin/true', '/bin/false', 'true', 'false') or prog.endswith('/true') or prog.endswith('/false'):
            errors.append('0.17 command receipt program cannot be no-op dummy: ' + r_name)

        norm_prog = os.path.normpath(prog if os.path.isabs(prog) else os.path.join(cwd, prog))
        prog_base = os.path.basename(norm_prog)

        ms = r.get('execution_ms')
        if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
            errors.append('0.17 command receipt execution_ms missing or invalid: ' + r_name)

        exit_code = r.get('exit_code')
        if type(exit_code) is not int or isinstance(exit_code, bool):
            errors.append('0.17 command receipt exit_code missing or not int: ' + r_name)
            continue

        if 'stdout' not in r or not isinstance(r.get('stdout'), str):
            errors.append('0.17 command receipt stdout missing or not a string: ' + r_name)
            out_s = ''
        else:
            out_s = r['stdout']

        if 'stderr' not in r or not isinstance(r.get('stderr'), str):
            errors.append('0.17 command receipt stderr missing or not a string: ' + r_name)
            err_s = ''
        else:
            err_s = r['stderr']

        if 'stdout_sha256' not in r or hashlib.sha256(out_s.encode('utf-8')).hexdigest() != r['stdout_sha256']:
            errors.append('0.17 command receipt stdout_sha256 mismatch: ' + r_name)
        if 'stderr_sha256' not in r or hashlib.sha256(err_s.encode('utf-8')).hexdigest() != r['stderr_sha256']:
            errors.append('0.17 command receipt stderr_sha256 mismatch: ' + r_name)

        if 'failure instead of assertions' in out_s or 'assertion failed' in out_s.lower():
            errors.append('0.17 command receipt stdout indicates failure: ' + r_name)

        is_neg = r_name.startswith('neg-')
        is_pos_compile = r_name.startswith('pos-') and r_name.endswith('-compile')
        if (is_pos_compile or is_neg):
            if prog_base not in ('tokac', 'tokac.exe'):
                errors.append('0.17 compile command program must execute tokac: ' + r_name)
            elif expected_tokac and norm_prog != expected_tokac:
                errors.append(f'0.17 compile command program mismatch with SDK tokac: {r_name} (executes {norm_prog}, expected {expected_tokac})')
            for f in ('--version', '-v', '-V', '--help', '-h'):
                if f in argv:
                    errors.append(f'0.17 compile/check command contains incompatible flag {f}: {r_name}')

        if not is_neg:
            if exit_code != 0:
                errors.append(f'0.17 positive command exit code must be 0: {r_name} (got {exit_code})')
            if r.get('diagnostic') is not None:
                errors.append('0.17 unexpected diagnostic on positive command: ' + r_name)
            if err_s.strip() and 'warning' not in err_s.lower():
                errors.append('0.17 unexpected stderr in positive command: ' + r_name)
        else:
            if exit_code != 1:
                errors.append(f'0.17 negative command exit code must be 1: {r_name} (got {exit_code})')

        # Exact argv template validation by command role
        if r_name.startswith('version-'):
            t_name = r_name.removeprefix('version-')
            if len(argv) != 2 or argv[1] != '--version':
                errors.append(f'0.17 tool version command argv template mismatch: {r_name}')
            if t_name == 'toka':
                if prog_base not in ('toka', 'toka.exe'):
                    errors.append('0.17 tool version command program mismatch: ' + r_name)
            else:
                if prog_base not in (t_name, t_name + '.exe'):
                    errors.append('0.17 tool version command program mismatch: ' + r_name)
            if isinstance(sdk_root, str) and sdk_root:
                sdk_tool_bin = os.path.normpath(os.path.join(sdk_root, 'bin', t_name))
                sdk_tool_exe = os.path.normpath(os.path.join(sdk_root, 'bin', t_name + '.exe'))
                if norm_prog not in (sdk_tool_bin, sdk_tool_exe):
                    errors.append(f'0.17 tool version program {norm_prog} does not match SDK_root {sdk_root}: {r_name}')
            if not ver_regex.search(out_s):
                errors.append(f'0.17 tool version {t_name} stdout missing {ver}: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-run'):
            if len(argv) != 1:
                errors.append(f'0.17 run command argv template mismatch (expected only executable): {r_name}')
            comp_name, exp_base = run_to_compile.get(r_name, ('', ''))
            expected_compiled_out = compile_outputs.get(comp_name)
            if not expected_compiled_out:
                errors.append(f'0.17 missing matching compile output for run command: {r_name} (needs {comp_name})')
            elif norm_prog != expected_compiled_out:
                errors.append(f'0.17 run program does not match compile output: {r_name} (executes {norm_prog}, compiled {expected_compiled_out})')
            valid_bases = (exp_base, exp_base + '.bin', exp_base + '.exe')
            if prog_base not in valid_bases:
                errors.append(f'0.17 run program basename mismatch: {r_name} (got {prog_base}, expected one of {valid_bases})')

        elif r_name == 'pos-tki-provider-compile':
            if len(argv) != 6 or argv.count('-c') != 1 or argv.count('--emit-interface') != 1 or argv.count('-o') != 1:
                errors.append(f'0.17 pos-tki-provider-compile argv template mismatch: {r_name}')
            if not (argv[2].endswith('/scoped_provider.tk') or argv[2] == 'scoped_provider.tk'):
                errors.append(f'0.17 pos-tki-provider-compile fixture mismatch: {r_name}')
            if len(argv) >= 6:
                out_path = os.path.normpath(argv[5] if os.path.isabs(argv[5]) else os.path.join(cwd, argv[5]))
                if os.path.basename(out_path) != 'provider.o':
                    errors.append(f'0.17 pos-tki-provider-compile output basename mismatch: {r_name}')

        elif r_name == 'pos-tki-consumer-compile':
            if len(argv) != 7 or argv.count('-I') != 1 or argv.count('-o') != 1:
                errors.append(f'0.17 pos-tki-consumer-compile argv template mismatch: {r_name}')
            if len(argv) >= 7:
                out_path = os.path.normpath(argv[6] if os.path.isabs(argv[6]) else os.path.join(cwd, argv[6]))
                if os.path.basename(out_path) not in ('tki_bin.bin', 'tki_bin.exe', 'tki_bin'):
                    errors.append(f'0.17 pos-tki-consumer-compile output basename mismatch: {r_name}')

        elif r_name.startswith('pos-') and r_name.endswith('-compile'):
            case = r_name.removeprefix('pos-').removesuffix('-compile')
            if len(argv) != 4 or argv.count('-o') != 1 or argv[2] != '-o':
                errors.append(f'0.17 positive compile command argv length mismatch: expected 4, got {len(argv)}: {r_name}')
            if not (argv[1].endswith('/' + case + '.tk') or argv[1] == (case + '.tk')):
                errors.append(f'0.17 compile fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                exp_bases = (f'{case}_bin.bin', f'{case}_bin.exe', f'{case}_bin')
                if os.path.basename(out_path) not in exp_bases:
                    errors.append(f'0.17 positive compile command output basename mismatch: {r_name}')

        elif is_neg and r_name.endswith('-object'):
            case = r_name.removeprefix('neg-').removesuffix('-object')
            if len(argv) != 5 or argv.count('-c') != 1 or argv.count('-o') != 1 or argv[1] != '-c' or argv[3] != '-o':
                errors.append(f'0.17 negative object command template mismatch: {r_name}')
            if not (argv[2].endswith(f'/{case}.tk') or argv[2] == f'{case}.tk'):
                errors.append(f'0.17 negative fixture mismatch: {r_name}')
            if len(argv) >= 5:
                out_path = os.path.normpath(argv[4] if os.path.isabs(argv[4]) else os.path.join(cwd, argv[4]))
                if os.path.basename(out_path) != f'{case}.o':
                    errors.append(f'0.17 negative object output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.17 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-ir'):
            case = r_name.removeprefix('neg-').removesuffix('-ir')
            if len(argv) != 6 or argv.count('--emit-llvm') != 1 or argv.count('-c') != 1 or argv.count('-o') != 1 or \
                    argv[1] != '--emit-llvm' or argv[2] != '-c' or argv[4] != '-o':
                errors.append(f'0.17 negative ir command template mismatch: {r_name}')
            if not (argv[3].endswith(f'/{case}.tk') or argv[3] == f'{case}.tk'):
                errors.append(f'0.17 negative fixture mismatch: {r_name}')
            if len(argv) >= 6:
                out_path = os.path.normpath(argv[5] if os.path.isabs(argv[5]) else os.path.join(cwd, argv[5]))
                if os.path.basename(out_path) != f'{case}.ll':
                    errors.append(f'0.17 negative ir output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.17 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-compile'):
            case = r_name.removeprefix('neg-').removesuffix('-compile')
            if len(argv) != 4 or argv.count('-o') != 1 or argv[2] != '-o':
                errors.append(f'0.17 negative compile command template mismatch: {r_name}')
            if not (argv[1].endswith(f'/{case}.tk') or argv[1] == f'{case}.tk'):
                errors.append(f'0.17 negative fixture mismatch: {r_name}')
            if len(argv) >= 4:
                out_path = os.path.normpath(argv[3] if os.path.isabs(argv[3]) else os.path.join(cwd, argv[3]))
                if os.path.basename(out_path) not in (f'{case}.bin', f'{case}.exe', case):
                    errors.append(f'0.17 negative compile output basename mismatch: {r_name}')
            if r.get('artifact_absent') is not True:
                errors.append('0.17 negative command did not confirm artifact absence: ' + r_name)

        elif is_neg and r_name.endswith('-check'):
            if len(argv) != 3 or argv.count('--check-only') != 1 or argv[1] != '--check-only':
                errors.append(f'0.17 negative check command template mismatch: {r_name}')
            exp_fix = NEG_017_CHECK_FIXTURES.get(r_name)
            if exp_fix:
                if not (argv[2].endswith('/' + exp_fix) or argv[2] == exp_fix):
                    errors.append(f'0.17 negative fixture mismatch: {r_name}')
            else:
                fix_name = r_name.removeprefix('neg-').removesuffix('-check')
                if not (argv[2].endswith('/' + fix_name + '.tk') or argv[2] == (fix_name + '.tk')):
                    errors.append(f'0.17 negative fixture mismatch: {r_name}')

        if r_name in NEG_017_DIAGNOSTICS:
            exp_diag = NEG_017_DIAGNOSTICS[r_name]
            if r.get('diagnostic') != exp_diag:
                errors.append(f'0.17 negative command diagnostic mismatch: {r_name} (expected {exp_diag}, got {r.get("diagnostic")})')
            if exp_diag not in err_s:
                errors.append(f'0.17 negative command stderr missing {exp_diag}: {r_name}')

    missing = EXPECTED_017_RECEIPTS - seen_receipts
    if missing:
        errors.append('Missing 0.17 command receipts: ' + ', '.join(sorted(missing)))

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
            if type(control.get('version')) is not int or control.get('version') != 1 or control.get('schema')!='toka.0.13-candidate-controls' or control.get('result')!='pass' or control.get('candidate_revision')!=revision or control.get('version_label')!=label or control.get('build_testing') is not False or control.get('groups')!=['A1','B1','B1-boundaries','B1-relative','D1-D2']:
                errors.append('0.13 installed feature proof missing: '+str(row.get('target')))
            if label != 'v0.13.0':
                r_errors = r3_r5_receipt_errors(row.get('r3_r5'), revision, label, target=row.get('target'))
                if r_errors:
                    errors.append('0.13.1 R3/R5 report proof invalid for %s: %s' % (row.get('target'), '; '.join(r_errors)))
    if label.startswith('v0.14.'):
        for row in summary.get('reports',[]):
            r_errors = candidate_014_receipt_errors(row.get('candidate_014'), revision, label, target=row.get('target'))
            if r_errors:
                errors.append('0.14 installed candidate proof invalid for %s: %s' % (row.get('target'), '; '.join(r_errors)))
    if label.startswith('v0.15.'):
        for row in summary.get('reports',[]):
            r_errors = candidate_015_receipt_errors(row.get('candidate_015'), revision, label, target=row.get('target'))
            if r_errors:
                errors.append('0.15 installed candidate proof invalid for %s: %s' % (row.get('target'), '; '.join(r_errors)))
    if label.startswith('v0.16.'):
        for row in summary.get('reports',[]):
            r_errors = candidate_016_receipt_errors(row.get('candidate_016'), revision, label, target=row.get('target'))
            if r_errors:
                errors.append('0.16 installed candidate proof invalid for %s: %s' % (row.get('target'), '; '.join(r_errors)))
    if label.startswith('v0.17.'):
        for row in summary.get('reports',[]):
            r_errors = candidate_017_receipt_errors(row.get('candidate_017'), revision, label, target=row.get('target'))
            if r_errors:
                errors.append('0.17 installed candidate proof invalid for %s: %s' % (row.get('target'), '; '.join(r_errors)))
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
