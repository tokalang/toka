#!/usr/bin/env python3
"""Fail closed on incomplete/censored B0; derive budgets from recorded components."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

from measure_test_baseline import SDK_HASHES, TASKS, SDK_REVISION, PROTOCOL

# Fixed before gathering hosted samples. These are policy margins, not percentiles.
POLICY = {
    'compile_ms': {'phase': 'compile_link', 'multiplier': 4, 'additive_ms': 5000,
                   'floor_ms': 30000, 'round_up_ms': 1000},
    'run_ms': {'phase': 'run', 'multiplier': 4, 'additive_ms': 1000,
               'floor_ms': 5000, 'round_up_ms': 1000},
}


def derive(maximum, policy):
    raw = maximum * policy['multiplier'] + policy['additive_ms']
    return int(math.ceil(max(raw, policy['floor_ms']) / policy['round_up_ms']) * policy['round_up_ms'])


def summarize(documents):
    targets = [d.get('target') for d in documents]
    if len(documents) != 3 or set(targets) != set(SDK_HASHES):
        raise ValueError('need exactly the three core targets, without duplicate reports')
    scripts = {d.get('script_sha256') for d in documents}
    revisions = {d.get('controller_sha') for d in documents}
    if len(scripts) != 1 or len(revisions) != 1 or None in scripts or None in revisions:
        raise ValueError('all targets must use the same immutable measurement script and SHA')
    data, table = {'compile_link': [], 'run': []}, []
    for d in documents:
        target = d['target']
        if d.get('schema') != 'toka.b0-baseline' or d.get('version') != 1 or \
                d.get('protocol') != PROTOCOL or d.get('mode') != 'full' or \
                d.get('result') != 'complete' or d.get('controller_dirty') is not False or \
                d.get('sdk_revision') != SDK_REVISION or d.get('sdk_version') != 'v0.11.0' or \
                d.get('sdk_archive_sha256') != SDK_HASHES[target] or d.get('samples_per_mode', 0) < 5:
            raise ValueError('incomplete, dirty, pilot or wrong SDK report: ' + target)
        if set(d.get('tasks', {})) != set(TASKS):
            raise ValueError('task set differs: ' + target)
        for task, source in TASKS.items():
            if d['tasks'][task]['sha256'] != hashlib.sha256(source.encode()).hexdigest():
                raise ValueError('task bytes differ: ' + task)
        records = d['records']
        if len(records) != len(TASKS) * d['samples_per_mode'] * 2:
            raise ValueError('missing or extra samples: ' + target)
        keys = {(r['task'], r['iteration'], r['mode']) for r in records}
        expected = {(task, i, mode) for task in TASKS
                    for i in range(1, d['samples_per_mode'] + 1) for mode in ('cold', 'hot')}
        if keys != expected or len(keys) != len(records):
            raise ValueError('duplicate/missing cold/hot samples: ' + target)
        identities = {json.dumps(r['sdk_identity'], sort_keys=True) for r in records}
        if len(identities) != 1:
            raise ValueError('SDK tool/runtime identity changed within host')
        for r in records:
            if not r['complete'] or r['lock_sha256_before'] != r['lock_sha256_after'] or r['sdk_identity'] != r['sdk_identity_after']:
                raise ValueError('sample or lock mismatch')
            for name, p in r['phases'].items():
                if p['state'] == 'not_started':
                    if any(p.get(k) is not None for k in ('duration_ms', 'process', 'exit_code', 'signal', 'os_error')):
                        raise ValueError('fabricated not-started phase state')
                    if name in ('compile_link', 'dependencies', 'check_pipeline', 'build_pipeline') or \
                            (name == 'run' and r['task'] != 'compile_reject' and r['phases']['compile_link']['exit_code'] == 0):
                        raise ValueError('required phase missing')
                    continue
                value = p.get('duration_ms')
                if p['state'] != 'completed' or p.get('protection_triggered') or \
                        not isinstance(value, (int, float)) or \
                        isinstance(value, bool) or not math.isfinite(value) or value < 0:
                    raise ValueError('censored/unexpected phase cannot be a completed timing')
                if name in data and p.get('signal') is None:
                    data[name].append(value)
        for task in TASKS:
            for mode in ('cold', 'hot'):
                group = [r for r in records if r['task'] == task and r['mode'] == mode]
                row = {'target': target, 'task': task, 'mode': mode, 'count': len(group),
                       'functional_failures': sum(not r['functional_expectation_met'] for r in group)}
                for phase in ('compile_link', 'run', 'network_fetch', 'dependencies', 'check_pipeline', 'build_pipeline'):
                    values = [r['phases'][phase]['duration_ms'] for r in group
                              if r['phases'][phase]['state'] == 'completed']
                    row[phase] = {'count': len(values), 'median_ms': statistics.median(values),
                                  'maximum_ms': max(values)} if values else None
                table.append(row)
    budgets = {}
    for key, policy in POLICY.items():
        maximum = max(data[policy['phase']])
        budgets[key] = dict(policy, observed_maximum_ms=maximum, sample_count=len(data[policy['phase']]),
                            computed_before_floor_ms=maximum * policy['multiplier'] + policy['additive_ms'],
                            default_ms=derive(maximum, policy))
    return {'schema': 'toka.b0-summary', 'version': 1, 'result': 'pass', 'protocol': PROTOCOL,
            'controller_sha': next(iter(revisions)), 'script_sha256': next(iter(scripts)),
            'sdk_revision': SDK_REVISION, 'budgets': budgets, 'table': table,
            'limitations': ['Fixed small projects, not arbitrary suite bounds or full-release timing.',
                           'Cold means fresh SDK/project; original bundled interfaces retained and OS page cache not flushed.',
                           'compile_link includes the observable native compile/link call; no fabricated independent durations.',
                           'Network fetch, dependency preparation and composite CLI probes are excluded from budget derivation.',
                           'Normal functional failures remain measured; unavailable run stages are not fabricated or counted.'] + [
                           'Unicode 0.1.1 incompatibility in the 0.11.0 pilot is retained as a task, not fixed or dropped.',
                           'Floors are conservative v1 policy margins; long legitimate tests may override.']}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--report', action='append', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--table', required=True, type=Path)
    a = p.parse_args()
    try:
        result = summarize([json.loads(f.read_text()) for f in a.report])
    except (ValueError, KeyError, TypeError) as error:
        result = {'result': 'fail', 'errors': [str(error)]}
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    if result['result'] != 'pass':
        raise SystemExit(json.dumps(result))
    lines = ['| Target | Task | Mode | n | functional failures | compile+link max ms | run max ms | network max ms |',
             '| --- | --- | --- | --- | --- | --- | --- | --- |']
    for row in result['table']:
        values = ['%.3f' % row[p]['maximum_ms'] if row[p] else 'not_started' for p in ('compile_link', 'run', 'network_fetch')]
        lines.append('| %s | %s | %s | %d | %d | %s | %s | %s |' % (row['target'], row['task'], row['mode'], row['count'], row['functional_failures'], *values))
    a.table.write_text('\n'.join(lines) + '\n')
    print(json.dumps(result['budgets'], indent=2))


if __name__ == '__main__':
    main()
