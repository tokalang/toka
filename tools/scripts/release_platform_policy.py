"""Version-bound platform contracts. Never relax a historical qualification."""
from pathlib import Path
import re
import json
import hashlib
import math

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
    if label.startswith(('v0.12.', 'v0.13.', 'v0.14.')) and not re.fullmatch(r'v0\.(?:12|13|14)\.(0|[1-9][0-9]*)', label):
        raise ValueError('noncanonical three-core release label')
    return bool(re.fullmatch(r'v0\.(?:12|13|14)\.(0|[1-9][0-9]*)',label))


def policy_id(label):
    if label.startswith('v0.14.'):
        return 'toka.release-platforms.0.14.v1'
    return 'toka.release-platforms.0.13.v1' if label.startswith('v0.13.') else POLICY


def core_targets(label):
    if not re.fullmatch(r'v0\.(?:11|12|13|14)\.(0|[1-9][0-9]*)',label):
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

        prog = argv[0].replace('\\', '/')
        if prog in ('/bin/true', '/bin/false', 'true', 'false') or prog.endswith('/true') or prog.endswith('/false'):
            errors.append('0.14 command receipt program cannot be no-op dummy: ' + r_name)

        cwd = r.get('cwd')
        if not isinstance(cwd, str) or not cwd:
            errors.append('0.14 command receipt cwd missing or invalid: ' + r_name)

        ms = r.get('execution_ms')
        if type(ms) not in (int, float) or isinstance(ms, bool) or not math.isfinite(ms) or ms <= 0:
            errors.append('0.14 command receipt execution_ms missing or invalid: ' + r_name)

        exit_code = r.get('exit_code')
        if type(exit_code) is not int or isinstance(exit_code, bool):
            errors.append('0.14 command receipt exit_code missing or not int: ' + r_name)
            continue

        if 'stdout' in r:
            out_s = str(r['stdout'])
            if 'failure instead of assertions' in out_s or 'assertion failed' in out_s.lower():
                errors.append('0.14 command receipt stdout indicates failure: ' + r_name)

        is_neg = (r_name in ('g1-reject-dyn-trait', 'g1-reject-plain-self') or r_name.startswith('g3-neg-'))

        if not is_neg:
            if r.get('diagnostic') is not None:
                errors.append('0.14 unexpected diagnostic on positive command: ' + r_name)
            if 'stderr' in r:
                err_s = str(r['stderr']).strip()
                if err_s and 'warning' not in err_s.lower():
                    errors.append('0.14 unexpected stderr in positive command: ' + r_name)

        if r_name.startswith('version-'):
            if exit_code != 0:
                errors.append('0.14 tool version command exit code must be 0: ' + r_name)
            t_name = r_name.removeprefix('version-')
            if t_name == 'toka':
                if not ((prog.endswith('/toka') or prog == 'toka' or prog.endswith('/toka.exe') or prog == 'toka.exe') and not (prog.endswith('tokac') or prog.endswith('tokac.exe'))):
                    errors.append('0.14 tool version command program mismatch: ' + r_name)
            else:
                if not (prog.endswith('/' + t_name) or prog == t_name or prog.endswith('/' + t_name + '.exe') or prog == t_name + '.exe'):
                    errors.append('0.14 tool version command program mismatch: ' + r_name)

        elif r_name == 'g1-lifecycle-compile':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                errors.append('0.14 G1 compile command program must execute tokac: ' + r_name)
            if not any(arg.endswith('/g14_static_async_trait_lifecycle.tk') or arg == 'g14_static_async_trait_lifecycle.tk' for arg in argv):
                errors.append('0.14 G1 lifecycle compile fixture mismatch: ' + r_name)
            if '-o' not in argv:
                errors.append('0.14 G1 lifecycle compile command missing -o: ' + r_name)

        elif r_name == 'g1-lifecycle-run':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if not (prog.endswith('/g1_lifecycle.bin') or prog.endswith('/g1_lifecycle.exe') or prog.endswith('/g1_lifecycle') or prog == 'g1_lifecycle.bin' or prog == 'g1_lifecycle.exe' or 'g1_lifecycle' in prog):
                errors.append('0.14 G1 lifecycle run program mismatch: ' + r_name)
            if 'stdout' in r and 'All 4 lifecycle branches passed successfully!' not in str(r['stdout']):
                errors.append('0.14 G1 lifecycle run stdout assertion missing: ' + r_name)

        elif r_name.startswith('g1-sourceless-compile-'):
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
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
            if '-o' not in argv:
                errors.append('0.14 G1 sourceless compile command missing -o: ' + r_name)

        elif r_name == 'g1-sourceless-run-main':
            if exit_code != 0:
                errors.append('0.14 G1 positive command exit code must be 0: ' + r_name)
            if not (prog.endswith('/sourceless_main.bin') or prog.endswith('/sourceless_main.exe') or prog.endswith('/sourceless_main') or prog == 'sourceless_main.bin' or prog == 'sourceless_main.exe' or 'sourceless' in prog):
                errors.append('0.14 G1 sourceless run program mismatch: ' + r_name)

        elif r_name == 'g1-reject-dyn-trait':
            if exit_code != 1:
                errors.append('0.14 G1 negative command exit code must be 1: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                errors.append('0.14 G1 negative command must execute tokac: ' + r_name)
            if not any(arg.endswith('/dyn_trait_async_param.tk') or arg == 'dyn_trait_async_param.tk' for arg in argv):
                errors.append('0.14 G1 reject dyn trait fixture mismatch: ' + r_name)
            if '--check-only' not in argv:
                errors.append('0.14 G1 negative command missing --check-only: ' + r_name)
            if r.get('diagnostic') != 'E0617':
                errors.append('0.14 G1 reject dyn trait diagnostic mismatch: ' + r_name)
            if 'stderr' in r and 'E0617' not in str(r['stderr']):
                errors.append('0.14 G1 reject dyn trait stderr missing E0617: ' + r_name)

        elif r_name == 'g1-reject-plain-self':
            if exit_code != 1:
                errors.append('0.14 G1 negative command exit code must be 1: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                errors.append('0.14 G1 negative command must execute tokac: ' + r_name)
            if not any(arg.endswith('/trait_async_plain_self.tk') or arg == 'trait_async_plain_self.tk' for arg in argv):
                errors.append('0.14 G1 reject plain self fixture mismatch: ' + r_name)
            if '--check-only' not in argv:
                errors.append('0.14 G1 negative command missing --check-only: ' + r_name)
            if r.get('diagnostic') != 'E0618':
                errors.append('0.14 G1 reject plain self diagnostic mismatch: ' + r_name)
            if 'stderr' in r and 'E0618' not in str(r['stderr']):
                errors.append('0.14 G1 reject plain self stderr missing E0618: ' + r_name)

        elif r_name.startswith('g2-compile-'):
            if exit_code != 0:
                errors.append('0.14 G2 compile command exit code must be 0: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                errors.append('0.14 G2 compile command program must execute tokac: ' + r_name)
            case = r_name.removeprefix('g2-compile-')
            if not any(arg.endswith('/' + case + '.tk') or arg == (case + '.tk') for arg in argv):
                errors.append('0.14 G2 compile fixture mismatch: ' + r_name)
            if '-o' not in argv:
                errors.append('0.14 G2 compile command missing -o: ' + r_name)

        elif r_name.startswith('g2-run-'):
            if exit_code != 0:
                errors.append('0.14 G2 run command exit code must be 0: ' + r_name)
            case = r_name.removeprefix('g2-run-')
            expected = case + '.bin'
            expected_exe = case + '.exe'
            if not (prog.endswith('/' + expected) or prog.endswith('/' + expected_exe) or prog == expected or prog == expected_exe or (case in prog and not prog.endswith('.tk'))):
                errors.append('0.14 G2 run program mismatch: ' + r_name)
            if 'stdout' in r:
                out_str = str(r['stdout'])
                if case == 'lifecycle_pass_suite' and 'All 10 lifecycle and cancellation controls passed' not in out_str:
                    errors.append('0.14 G2 lifecycle pass suite stdout assertion missing: ' + r_name)
                elif case == 'given_11_branch' and 'All 11 G2 framed transport verification branches passed successfully!' not in out_str:
                    errors.append('0.14 G2 given 11 branch stdout assertion missing: ' + r_name)
                elif case == 'closed_empty' and 'closed-empty write ok=true' not in out_str:
                    errors.append('0.14 G2 closed empty stdout assertion missing: ' + r_name)

        elif r_name.startswith('g3-compile-'):
            if exit_code != 0:
                errors.append('0.14 G3 positive compile command exit code must be 0: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
                errors.append('0.14 G3 compile command program must execute tokac: ' + r_name)
            tag = r_name.removeprefix('g3-compile-')
            fix_name = expected_g3_pos_fixtures.get(tag)
            if fix_name and not any(arg.endswith('/' + fix_name) or arg == fix_name for arg in argv):
                errors.append('0.14 G3 positive compile fixture mismatch: ' + r_name)
            if '-o' not in argv:
                errors.append('0.14 G3 positive compile command missing -o: ' + r_name)

        elif r_name.startswith('g3-run-'):
            if exit_code != 0:
                errors.append('0.14 G3 positive run command exit code must be 0: ' + r_name)
            tag = r_name.removeprefix('g3-run-')
            expected = f'g3_{tag}.bin'
            expected_exe = f'g3_{tag}.exe'
            if not (prog.endswith('/' + expected) or prog.endswith('/' + expected_exe) or prog == expected or prog == expected_exe or ('g3_' + tag in prog and not prog.endswith('.tk')) or (tag in prog and not prog.endswith('.tk'))):
                errors.append('0.14 G3 run program mismatch: ' + r_name)
            if 'stdout' in r:
                out_str = str(r['stdout'])
                if tag == 'lifecycle' and 'All 16 unwrap propagation lifecycle controls passed' not in out_str:
                    errors.append('0.14 G3 lifecycle run stdout assertion missing: ' + r_name)
                elif tag == 'aggregate' and 'PASS' not in out_str:
                    errors.append('0.14 G3 aggregate run stdout assertion missing: ' + r_name)

        elif r_name.startswith('g3-neg-'):
            if exit_code != 1:
                errors.append('0.14 G3 negative command exit code must be 1: ' + r_name)
            if not (prog.endswith('/tokac') or prog == 'tokac' or prog.endswith('/tokac.exe') or prog == 'tokac.exe'):
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
            if 'stderr' in r and 'E04512' not in str(r['stderr']):
                errors.append('0.14 G3 negative command stderr missing E04512: ' + r_name)
        else:
            errors.append('Unknown 0.14 command receipt: ' + r_name)

    missing = expected_receipt_names - seen_receipts
    if missing:
        errors.append('Missing 0.14 command receipts: ' + ', '.join(sorted(missing)))

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
