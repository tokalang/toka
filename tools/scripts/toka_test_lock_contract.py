"""Independent C6/P05 consumer rules; never import producer schema factories."""

REQUIRED_FIELDS = frozenset(('schema', 'version', 'run_id', 'preview', 'result',
    'reason', 'exit_code', 'project_root', 'artifact_root', 'identity', 'selection',
    'supervision', 'timeouts', 'summary', 'tests', 'errors', 'diagnostics',
    'termination', 'timings'))
LOCK_CODES = {'missing': 'test.lock_missing', 'malformed': 'test.lock_invalid',
              'stale': 'test.lock_mismatch'}
EXIT_CODES = {'passed': 0, 'empty': 0, 'failed': 1, 'configuration_error': 2,
              'infrastructure_error': 2, 'interrupted': 130}


class ReportContractError(AssertionError):
    """Consumer rejection; the original CLI result/report must remain intact."""


def require(condition, message):
    if not condition:
        raise ReportContractError(message)


def attribution(code):
    if code is None:
        return 'specific attribution unavailable'
    if code in LOCK_CODES.values():
        return code
    return 'unknown code: ' + code


def validate(report, actual_exit=None):
    try:
        return _validate(report, actual_exit)
    except (KeyError, TypeError, AttributeError) as error:
        raise ReportContractError("C6 malformed or missing required field: " + str(error)) from error


def _validate(report, actual_exit=None):
    require(isinstance(report, dict) and REQUIRED_FIELDS <= report.keys(),
            'C6 required report field missing')
    require(report['schema'] == 'toka.test-report' and
            type(report['version']) is int and report['version'] == 1,
            'unsupported C6 schema/version; expected toka.test-report/integer 1; actual ' + repr((report['schema'],report['version'])))
    require(report.get('finalized') is True, 'C6 report is incomplete')
    require(type(report['exit_code']) is int and
            report['result'] in EXIT_CODES and
            EXIT_CODES[report['result']] == report['exit_code'],
            'C6 result and exit code contradict: ' + repr((report['result'],report['exit_code'])))
    if actual_exit is not None:
        require(actual_exit == report['exit_code'], 'CLI exit contradicts C6 report: expected ' + repr(report['exit_code']) + '; actual ' + repr(actual_exit))
    require(isinstance(report['errors'], list), 'C6 errors must be an array')
    if report['errors']:
        require(report['result'] in ('configuration_error', 'infrastructure_error'),
                'call-level errors cannot masquerade as success or semantic rejection')
    for error in report['errors']:
        require(isinstance(error, dict) and
                {'code', 'message', 'phase', 'os_error', 'source'} <= error.keys(),
                'C6 required error field missing')
        require(error['code'] is None or isinstance(error['code'], str),
                'C6 error code must be string or null')
        if error['code'] in LOCK_CODES.values():
            _lock_facts(report, error)
    return report


def _lock_facts(report, error):
    require(report['result'] == 'configuration_error' and report['exit_code'] == 2
            and error.get('category') == 'configuration_error' and
            error['phase'] == 'context', 'lock code requires configuration_error/2 in context; actual ' + repr((report['result'],report['exit_code'],error.get('category'),error['phase'])))
    termination = report['termination']
    require(termination['reason'] == 'configuration_error' and
            termination['phase'] == 'context' and termination['trigger'] == 'none',
            'lock code has inconsistent termination')
    context = report.get('preparation', {}).get('context', {}).get('phase', {})
    require(context.get('state') == 'completed' and context.get('exit_code') == 2
            and context.get('signal') is None, 'lock code lacks completed context failure')
    summary = report['summary']
    total = summary['total']
    require(type(total) is int and total > 0 and total == len(report['tests'])
            and summary['not_run'] == total and
            all(summary[key] == 0 for key in
                ('passed', 'failed', 'infrastructure_error', 'interrupted')),
            'lock code requires all selected tests not_run')
    for test in report['tests']:
        require(test['result'] == 'not_run' and
                set(test['phases']) == {'compile_link', 'compile', 'link', 'run'},
                'lock error test result/phases inconsistent')
        for phase in test['phases'].values():
            require(phase['state'] == 'not_started' and
                    all(key in phase and phase[key] is None for key in
                        ('duration_ms', 'process', 'exit_code', 'signal', 'os_error')),
                    'lock error must not launch test compile or run')


def require_lock_failure(report, expected_code, actual_exit):
    validate(report, actual_exit)
    require(expected_code in LOCK_CODES.values(), 'unknown controlled P05 expectation')
    require(len(report['errors']) == 1 and
            report['errors'][0]['code'] == expected_code,
            'P05 expected ' + expected_code + '; actual codes ' +
            repr([e['code'] for e in report['errors']]))
    _lock_facts(report, report['errors'][0])
    return report


def require_p05_failure(report, case, actual_exit, selected_contract):
    require(selected_contract in ('legacy','a1'), 'P05 contract must be explicitly legacy or a1')
    require(case in LOCK_CODES, 'unknown controlled P05 case')
    if selected_contract=='a1':
        return require_lock_failure(report,LOCK_CODES[case],actual_exit)
    validate(report,actual_exit)
    require(len(report['errors'])==1, 'legacy P05 requires one preparation error')
    try:
        _lock_facts(report,report['errors'][0])
    except (KeyError,TypeError,AttributeError) as error:
        raise ReportContractError('legacy P05 malformed required fact: '+str(error)) from error
    return report
