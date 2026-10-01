#!/usr/bin/env python3
"""Measurement guard and inference controls; no SDK qualification substitute."""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

from measure_test_baseline import execute, unused, SDK_HASHES, SDK_REVISION, TASKS, PROTOCOL
from summarize_test_baseline import summarize, derive, POLICY


def fixture():
    docs = []
    for target, digest in SDK_HASHES.items():
        d = {'schema': 'toka.b0-baseline', 'version': 1, 'protocol': PROTOCOL,
             'mode': 'full', 'result': 'complete', 'target': target,
             'controller_sha': 'a' * 40, 'script_sha256': 'b' * 64, 'controller_dirty': False,
             'sdk_revision': SDK_REVISION, 'sdk_version': 'v0.11.0', 'sdk_archive_sha256': digest,
             'samples_per_mode': 5, 'tasks': {k: {'sha256': hashlib.sha256(v.encode()).hexdigest()} for k,v in TASKS.items()}, 'records': []}
        for task in TASKS:
            for i in range(1,6):
                for mode in ('cold','hot'):
                    phases = {p: unused() for p in ['compile_link','compile','link','run','dependencies',
                                                   'network_fetch','check_pipeline','build_pipeline']}
                    for name in ['compile_link','dependencies','check_pipeline','build_pipeline'] + ([] if task=='compile_reject' else ['run']):
                        phases[name].update(state='completed', duration_ms=123.5, observed_wall_ms=123.5,
                                            process={'pid': 1, 'pgid': 1}, exit_code=0, signal=None, expected_result=True)
                    d['records'].append({'task':task,'iteration':i,'mode':mode,'complete':True,
                                         'sdk_identity': {'compiler':'c' * 64}, 'sdk_identity_after': {'compiler':'c' * 64},
                                         'lock_sha256_before':'d' * 64,'lock_sha256_after':'d' * 64,
                                         'functional_expectation_met': True, 'phases': phases})
        docs.append(d)
    return docs


def main():
    if os.name != 'posix':
        raise SystemExit('B0 scope is native POSIX core hosts')
    with tempfile.TemporaryDirectory(prefix='b0-guard-test-') as tmp:
        root=Path(tmp)
        ok=execute([sys.executable,'-c','print("guard fixture")'],root,os.environ.copy(),root/'ok','run',guard_ms=5000)
        assert ok['state']=='completed' and ok['duration_ms'] is not None
        cutoff=execute([sys.executable,'-c','import time; time.sleep(20)'],root,os.environ.copy(),root/'cutoff','run',guard_ms=40)
        assert cutoff['state']=='censored' and cutoff['protection_triggered'] and cutoff['duration_ms'] is None
        assert cutoff['observed_wall_ms'] >= 40 and cutoff['cleanup'] != 'unconfirmed'
        missing=execute([str(root/'missing-tool')],root,os.environ.copy(),root/'missing','run',guard_ms=1000)
        assert missing['state']=='launch_failed' and missing['duration_ms'] is None and missing['os_error'] is not None
    docs=fixture(); assert summarize(docs)['result']=='pass'
    # Functional rejection is preserved, not confused with a censored observation.
    for d in docs:
        for r in d['records']:
            if r['task']=='registry_dep_ok':
                r['functional_expectation_met']=False
                r['phases']['compile_link']['exit_code']=1
                r['phases']['run']=unused()
    summary=summarize(docs); assert sum(r['functional_failures'] for r in summary['table'])==30
    controls=[]
    def reject(name, change):
        modified=copy.deepcopy(docs);change(modified)
        try:summarize(modified)
        except (ValueError,KeyError,TypeError):controls.append(name)
        else:raise AssertionError('accepted invalid input: '+name)
    reject('pilot',lambda d:d[0].update(mode='pilot'))
    reject('dirty',lambda d:d[0].update(controller_dirty=True))
    reject('SDK',lambda d:d[0].update(sdk_archive_sha256='0'*64))
    reject('controller',lambda d:d[0].update(controller_sha='c'*40))
    reject('script',lambda d:d[0].update(script_sha256='e'*64))
    reject('missing sample',lambda d:d[0]['records'].pop())
    reject('duplicate sample',lambda d:d[0]['records'].append(d[0]['records'][0]))
    reject('lock changed',lambda d:d[0]['records'][0].update(lock_sha256_after='0'*64))
    reject('SDK changed',lambda d:d[0]['records'][0].update(sdk_identity_after={}))
    reject('censored',lambda d:d[0]['records'][0]['phases']['compile_link'].update(state='censored',duration_ms=None,protection_triggered=True))
    reject('hidden guard',lambda d:d[0]['records'][0]['phases']['compile_link'].update(protection_triggered=True))
    reject('missing run',lambda d:d[0]['records'][0]['phases'].update(run=unused()))
    reject('fabricated split',lambda d:d[0]['records'][0]['phases']['compile'].update(duration_ms=0))
    assert derive(123.5,POLICY['compile_ms'])==30000 and derive(123.5,POLICY['run_ms'])==5000
    assert derive(10001,POLICY['compile_ms'])==46000
    print(json.dumps({'result':'pass','guard_controls':['completed','censored','launch_failed'],
                      'rejected':controls,'rounding_control':'passed','functional_failures_retained':30}))


if __name__=='__main__':main()
