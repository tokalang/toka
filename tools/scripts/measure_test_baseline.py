#!/usr/bin/env python3
"""B0: measure immutable released SDK components, never a new test command."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tarfile
import time

SDK_REVISION = '57b0f7dd7d52bdc24c6dde0457803240e5c62e8a'
SDK_HASHES = {
    'linux-x64': '05faa6cf2128f9dc385aa832c33f722e34f5a4f4b98d0613887a3efe79a35635',
    'linux-arm64': '16e8deda50d7983bfa9cffc9f55e8587f4b9b3f4c72c7346a3e82f3c173466f1',
    'macos-arm64': '81e95d01f685f4fbb057fc42b03c376c2646b41bce08398f8f40a76d2cdd7b99',
}
UNICODE_URL = 'https://github.com/tokalang/unicode/releases/download/v0.1.1/unicode-0.1.1.tar.gz'
UNICODE_ARCHIVE = 'c68569e6efbd9eb9bf85226eca68de3a0187d4300e320aeb13857be73b5ad28a'
UNICODE_TREE = '8c82ff393812d1ddd9a8b1f6d71d8ab49863a68b6193af1d784ea722e052fe76'
# Guards protect the measurement, not proposed defaults. A guard hit is censored.
GUARDS_MS = {'compile_link': 600000, 'build_pipeline': 600000, 'run': 120000,
             'network_fetch': 180000, 'other': 180000}
PROTOCOL = 'toka.b0.sdk-components.v1'
TASKS = {
    'bare_ok': 'fn main() -> i32 { return 0 }\n',
    'owned_vec_ok': '''import std/vec::{Vec}
fn main() -> i32 {
    auto items# = Vec<string>::new()
    auto i# = 0:i32
    loop i < 2000 {
        auto text = string::from("owned")
        items#.push(cede text)
        i += 1
    }
    if items.len() != 2000:usize { return 1 }
    return 0
}
''',
    'json_ok': '''import stdx/serde/json::{parse_document}
fn main() -> i32 {
    auto total# = 0:i32
    auto i# = 0:i32
    loop i < 2000 {
        auto document = parse_document("{\\"v\\":42}").unwrap()
        total += document.number(document.find(1, "v")).unwrap() as i32
        i += 1
    }
    if total != 84000 { return 1 }
    return 0
}
''',
    'local_dep_ok': '''import official/b0_local::{answer}
fn main() -> i32 {
    if answer() != 42 { return 1 }
    return 0
}
''',
    'registry_dep_ok': '''import official/unicode::{grapheme_count}
fn main() -> i32 {
    auto total# = 0:usize
    auto i# = 0:i32
    loop i < 2000 {
        total += grapheme_count("hello").unwrap()
        i += 1
    }
    if total != 10000:usize { return 1 }
    return 0
}
''',
    'compile_reject': 'fn main() -> i32 { auto number: i32 = true; return number }\n',
    'run_nonzero': 'fn main() -> i32 { return 7 }\n',
}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def unused():
    return dict(state='not_started', duration_ms=None, observed_wall_ms=None,
                process=None, exit_code=None, signal=None, os_error=None,
                guard_ms=None, protection_triggered=False, stdout=None, stderr=None)


def execute(command, cwd, env, logs, name, expected=0, guard_ms=None):
    """Regular-file capture avoids pipe deadlocks; timer is perf_counter_ns."""
    guard_ms = guard_ms or GUARDS_MS.get(name, GUARDS_MS['other'])
    logs.mkdir(parents=True, exist_ok=True)
    out, err = logs / (name + '.stdout'), logs / (name + '.stderr')
    phase = unused()
    phase.update(command=[str(x) for x in command], cwd=str(cwd), guard_ms=guard_ms,
                 stdout=str(out), stderr=str(err))
    started = time.perf_counter_ns()
    child = None
    print('START', name, 'cwd=' + str(cwd), flush=True)
    with out.open('wb') as stdout, err.open('wb') as stderr:
        try:
            child = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                     stdout=stdout, stderr=stderr, start_new_session=True)
            phase['process'] = {'pid': child.pid, 'pgid': child.pid}
            try:
                remaining = max(0.0, guard_ms / 1000 - (time.perf_counter_ns() - started) / 1e9)
                code = child.wait(timeout=remaining)
                phase.update(state='completed', exit_code=code if code >= 0 else None,
                             signal=-code if code < 0 else None)
            except subprocess.TimeoutExpired:
                # Kill while the owned leader has not been reaped. Never reuse a stale PID.
                phase.update(state='censored', protection_triggered=True)
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait(timeout=5)
                    phase['cleanup'] = 'leader_reaped_after_group_kill'
                except (OSError, subprocess.TimeoutExpired) as error:
                    phase['cleanup'] = 'unconfirmed'
                    phase['cleanup_error'] = str(error)
                if child.returncode is not None:
                    phase['exit_code'] = child.returncode if child.returncode >= 0 else None
                    phase['signal'] = -child.returncode if child.returncode < 0 else None
        except OSError as error:
            phase.update(state='launch_failed', os_error=error.errno, error=str(error))
    elapsed = (time.perf_counter_ns() - started) / 1e6
    if phase['state'] == 'completed' and elapsed > guard_ms:
        phase.update(state='censored', protection_triggered=True, protection_reason='observed_wall_exceeded_guard')
    phase['observed_wall_ms'] = elapsed
    phase['duration_ms'] = elapsed if phase['state'] == 'completed' else None
    phase['expected_result'] = (phase['state'] == 'completed' and phase['signal'] is None and
                                ((phase['exit_code'] != 0) if expected == 'nonzero'
                                 else phase['exit_code'] == expected))
    print('END', name, phase['state'], 'wall_ms=%.3f' % elapsed,
          'exit=' + str(phase['exit_code']), flush=True)
    return phase


def git_value(*args):
    result = subprocess.run(['git', '-c', 'core.fsmonitor=false', *args],
                            capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def host_info(env):
    facts = {'platform': platform.platform(), 'machine': platform.machine(),
             'processor': platform.processor(), 'logical_cpus': os.cpu_count(),
             'python': sys.version, 'python_executable': sys.executable, 'python_sha256': digest(sys.executable),
             'image_os': os.environ.get('ImageOS'), 'image_version': os.environ.get('ImageVersion'),
             'runner_os': os.environ.get('RUNNER_OS'), 'runner_arch': os.environ.get('RUNNER_ARCH'),
             'github_run_id': os.environ.get('GITHUB_RUN_ID'),
             'github_run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT')}
    for tool in ('clang', 'ld.lld', 'ld64.lld', 'curl'):
        path = shutil.which(tool, path=env['PATH'])
        if path:
            r = subprocess.run([path, '--version'], env=env, capture_output=True, timeout=10)
            facts[tool] = {'path': path, 'sha256': digest(path),
                           'version': (r.stdout + r.stderr).decode('utf-8', errors='replace')}
    if Path('/etc/os-release').exists():
        facts['os_release'] = Path('/etc/os-release').read_text()
    facts['affinity'] = sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None
    return facts


def prepare_project(pair, sdk, task, source, env, logs):
    project = pair / 'project'
    create = execute([str(sdk / 'bin/toka'), 'new', 'project'], pair, env, logs, 'project_create')
    if not create['expected_result']:
        raise RuntimeError('released toka new failed')
    dependencies = ''
    lock = 'toka-lock-v1\n'
    if task == 'local_dep_ok':
        dep = pair / 'local_dep'
        (dep / 'lib/official').mkdir(parents=True)
        (dep / 'package.tk').write_text('pub const PACKAGE = (name = "b0_local", version = "1.0.0", dependencies = ())\n')
        (dep / 'lib/official/b0_local.tk').write_text('pub fn answer() -> i32 { return 42 }\n')
        dependencies = 'b0_local = "../local_dep",'
        # Platform-local absolute locator is unavoidable in this SDK's lock format.
        # Resolve once outside sampling, freeze those bytes for this pair.
    elif task == 'registry_dep_ok':
        dependencies = 'unicode = "unicode:0.1.1",'
        lock += 'package\tunicode\tregistry\tunicode\t0.1.1\t%s\t%s\t-\n' % (UNICODE_ARCHIVE, UNICODE_TREE)
    manifest = 'pub const PACKAGE = (name = "b0_%s", version = "1.0.0", dependencies = (%s))\n' % (task, dependencies)
    (project / 'package.tk').write_text(manifest)
    (project / 'src/main.tk').write_text(source)
    (project / 'tests').mkdir(exist_ok=True)
    (project / 'tests/main_test.tk').write_text(source)
    (project / 'package.lock').write_text(lock)
    if task == 'local_dep_ok':
        p = execute([sys.executable, str(sdk / 'lib/toolchain/toka_package.py'), 'fetch'],
                    project, env, logs, 'local_lock_setup')
        if not p['expected_result']:
            raise RuntimeError('cannot create local dependency lock')
        # Keep lock, reset materialized project state before the measured cold sample.
        shutil.rmtree(project / '.toka', ignore_errors=True)
    return project, create


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', required=True, choices=SDK_HASHES)
    parser.add_argument('--sdk-archive', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    if args.samples < 5 and not args.pilot:
        parser.error('full B0 requires at least five cold/hot pairs')
    if args.samples < 1:
        parser.error('samples must be positive')
    archive = args.sdk_archive.resolve()
    if digest(archive) != SDK_HASHES[args.target]:
        raise SystemExit('SDK archive does not match frozen v0.11.0 bytes')
    expected_os = 'Darwin' if args.target.startswith('macos') else 'Linux'
    expected_arch = ('arm64', 'aarch64') if args.target.endswith('arm64') else ('x86_64', 'amd64')
    if platform.system() != expected_os or platform.machine().lower() not in expected_arch:
        raise SystemExit('target and native host do not match')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    raw, pairs = work / 'raw', work / 'pairs'
    raw.mkdir(); pairs.mkdir()
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('TOKA') and k not in ('GH_TOKEN', 'GITHUB_TOKEN')}
    env['TOKA_OFFLINE'] = '1'
    records = []
    result = {'schema': 'toka.b0-baseline', 'version': 1, 'protocol': PROTOCOL,
              'mode': 'pilot' if args.pilot else 'full', 'target': args.target,
              'script_sha256': digest(Path(__file__)), 'controller_sha': git_value('rev-parse', 'HEAD'),
              'controller_dirty': bool(git_value('status', '--porcelain')),
              'sdk_version': 'v0.11.0', 'sdk_revision': SDK_REVISION,
              'sdk_archive_sha256': digest(archive), 'samples_per_mode': args.samples,
              'host': host_info(env), 'guards_ms': GUARDS_MS,
              'tasks': {name: {'source': src, 'sha256': hashlib.sha256(src.encode()).hexdigest(),
                               'compile_expected': 'nonzero' if name == 'compile_reject' else 0,
                               'run_expected': None if name == 'compile_reject' else (7 if name == 'run_nonzero' else 0)}
                        for name, src in TASKS.items()},
              'cold_definition': 'Fresh SDK extraction with original bundled caches; fresh project and empty project state. OS page cache is not flushed. Local lock resolved outside sampling then .toka reset.',
              'hot_definition': 'Same SDK/project/source/lock immediately after its cold sequence; caches and build products retained.',
              'phase_order': ['network_fetch', 'dependencies', 'compiler_mappings', 'compiler_nodes',
                              'workspace_node', 'compile_link', 'run', 'check_pipeline', 'build_pipeline'],
              'timing_scope': 'Direct compile_link/run are observable; separate compile/link are not. CLI probes after direct compile are composite pipelines, not pure compile times.',
              'records': records,
              'functional_completion_is_separate': 'Complete measurements may include normal functional failures. No failed task is removed or replaced; absence of run timing after compile failure is explicit.'}
    def save():
        complete = len(records) == len(TASKS) * args.samples * 2 and all(r['complete'] for r in records)
        result['result'] = 'complete' if complete else 'incomplete'
        (work / 'baseline.json').write_text(json.dumps(result, indent=2) + '\n')
    try:
        for task, source in TASKS.items():
            for iteration in range(1, args.samples + 1):
                pair = pairs / task / ('%02d' % iteration)
                pair.mkdir(parents=True)
                setup_start = time.perf_counter_ns()
                with tarfile.open(archive) as package:
                    package.extractall(pair / 'sdk', filter='data')
                sdk = pair / 'sdk' / ('toka-v0.11.0-' + args.target)
                task_env = dict(env, PATH=str(sdk / 'bin') + os.pathsep + env['PATH'])
                task_logs = raw / task / ('%02d' % iteration)
                project, create = prepare_project(pair, sdk, task, source, task_env, task_logs / 'setup')
                sdk_identity = {'tokac_sha256': digest(sdk / 'bin/tokac'),
                                'toka_sha256': digest(sdk / 'bin/toka'),
                                'runtime_sha256': digest(sdk / 'lib/sys/toka_rt.o'),
                                'helper_sha256': digest(sdk / 'lib/toolchain/toka_package.py')}
                version = execute([str(sdk / 'bin/tokac'), '--version'], pair, task_env,
                                  task_logs / 'setup', 'version')
                if not version['expected_result'] or '0.11.0' not in Path(version['stdout']).read_text():
                    raise RuntimeError('released compiler version probe failed')
                lock_before = digest(project / 'package.lock')
                (task_logs / 'setup/package.lock').write_bytes((project / 'package.lock').read_bytes())
                (task_logs / 'setup/package.tk').write_bytes((project / 'package.tk').read_bytes())
                (task_logs / 'setup/main_test.tk').write_bytes(source.encode())
                setup_ms = (time.perf_counter_ns() - setup_start) / 1e6
                for mode in ('cold', 'hot'):
                    sample_start = time.perf_counter_ns()
                    logs = task_logs / mode
                    phases = {name: unused() for name in result['phase_order'] + ['compile', 'link']}
                    record = {'task': task, 'iteration': iteration, 'mode': mode,
                              'sdk_identity': sdk_identity, 'lock_sha256_before': lock_before,
                              'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
                              'setup_ms': setup_ms if mode == 'cold' else None,
                              'project_create': create if mode == 'cold' else unused(),
                              'phases': phases, 'complete': False}
                    records.append(record)
                    if task == 'registry_dep_ok' and mode == 'cold':
                        cache = project / '.toka/cache/archives'
                        cache.mkdir(parents=True)
                        dep_archive = cache / (UNICODE_ARCHIVE + '.tar.gz')
                        phases['network_fetch'] = execute(['curl', '--fail', '--location', '--silent',
                            '--show-error', '--connect-timeout', '15', '--max-time', '120',
                            UNICODE_URL, '--output', str(dep_archive)], project, task_env, logs, 'network_fetch')
                        if not phases['network_fetch']['expected_result'] or digest(dep_archive) != UNICODE_ARCHIVE:
                            raise RuntimeError('pinned dependency network fetch failed')
                    phases['dependencies'] = execute([str(sdk / 'bin/toka'), 'fetch'], project,
                                                      task_env, logs, 'dependencies')
                    if not phases['dependencies']['expected_result']:
                        raise RuntimeError('SDK locked dependency fetch failed')
                    helper = [sys.executable, str(sdk / 'lib/toolchain/toka_package.py')]
                    flags = []
                    for name, command, flag in [('compiler_mappings', 'compiler-mappings', '--pkg'),
                                               ('compiler_nodes', 'compiler-node-mappings', '--pkg-node'),
                                               ('workspace_node', 'workspace-node', '--workspace-node')]:
                        phases[name] = execute(helper + [command], project, task_env, logs, name)
                        if not phases[name]['expected_result']:
                            raise RuntimeError('SDK context mapping failed')
                        for line in Path(phases[name]['stdout']).read_text().splitlines():
                            if line.strip(): flags += [flag, line.strip()]
                    flags += ['--workspace-root', str(project)]
                    exe = project / 'case-executable'
                    compiler = [str(sdk / 'bin/tokac'), '-I', str(sdk / 'lib'), *flags,
                                'tests/main_test.tk', '-o', str(exe), '-O0']
                    expected_compile = 'nonzero' if task == 'compile_reject' else 0
                    phases['compile_link'] = execute(compiler, project, task_env, logs,
                                                      'compile_link', expected_compile)
                    if phases['compile_link']['state'] == 'completed' and phases['compile_link']['exit_code'] == 0 and task != 'compile_reject':
                        record['executable_sha256'] = digest(exe)
                        phases['run'] = execute([str(exe)], project, task_env, logs, 'run',
                                                 7 if task == 'run_nonzero' else 0)
                    phases['check_pipeline'] = execute([str(sdk / 'bin/toka'), 'check', '--json',
                                                        'tests/main_test.tk'], project, task_env,
                                                       logs, 'check_pipeline', expected_compile)
                    phases['build_pipeline'] = execute([str(sdk / 'bin/toka'), 'build'], project,
                                                       task_env, logs, 'build_pipeline', expected_compile)
                    record['lock_sha256_after'] = digest(project / 'package.lock')
                    record['sdk_identity_after'] = {'tokac_sha256': digest(sdk / 'bin/tokac'),
                        'toka_sha256': digest(sdk / 'bin/toka'), 'runtime_sha256': digest(sdk / 'lib/sys/toka_rt.o'),
                        'helper_sha256': digest(sdk / 'lib/toolchain/toka_package.py')}
                    record['observed_total_ms'] = (time.perf_counter_ns() - sample_start) / 1e6
                    measured = [p for p in phases.values() if p['state'] != 'not_started']
                    record['functional_expectation_met'] = all(p['expected_result'] for p in measured)
                    record['complete'] = (all(p['state'] == 'completed' and not p['protection_triggered'] for p in measured) and
                                          record['lock_sha256_after'] == lock_before and record['sdk_identity_after'] == sdk_identity and
                                          (task == 'compile_reject' or phases['compile_link']['exit_code'] != 0 or phases['run']['state'] == 'completed'))
                    record['run_not_started_reason'] = 'compile_failed' if task != 'compile_reject' and phases['run']['state'] == 'not_started' else None
                    save()
                    if any(p['state'] != 'completed' for p in measured):
                        raise RuntimeError('censored or infrastructure-failed sample; stop this host')
                # Binaries/SDK are reproducible inputs, not raw-result artifacts.
                shutil.rmtree(pair)
    except Exception as error:
        result['error'] = str(error)
        save()
        raise
    save()
    print(json.dumps({'result': result['result'], 'samples': len(records), 'target': args.target}))
    raise SystemExit(0 if result['result'] == 'complete' else 1)


if __name__ == '__main__':
    main()
