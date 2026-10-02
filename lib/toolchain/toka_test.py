#!/usr/bin/env python3
"""Preview project tests: supervised execution and C6 reports; I2-C is pending."""
from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
import contextlib
import hashlib
import importlib.util
import json
import os
import stat
import signal
import shlex
import subprocess
from pathlib import Path
import sys
import tempfile
import time

try:
    import toka_test_report as reports
    import toka_package as packages
    from toka_test_process import Supervisor, Interrupted, SupervisionError, streamed_run
except ImportError as error:
    print('Error: active SDK test/package helper is unavailable: ' + str(error), file=sys.stderr)
    raise SystemExit(2) from error

DEFAULT_PREPARE_MS = 180000
DEFAULT_NATIVE_MS = 120000
DEFAULT_COMPILE_MS = 30000
DEFAULT_RUN_MS = 5000
PREVIEW = ('Preview: project tests with supervised execution and C6 JSON. '
           'Installed-SDK final acceptance awaits I2-C.')


class PreviewError(RuntimeError):
    category = 'infrastructure_error'


class ConfigurationError(PreviewError):
    category = 'configuration_error'


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise PreviewError(message)


def parse_options(arguments):
    if arguments == ['--help']:
        return None
    result = argparse.Namespace(entries=[], filter=[], allow_empty=False, compile_ms=DEFAULT_COMPILE_MS, run_ms=DEFAULT_RUN_MS, compile_source='default', run_source='default', json=False)
    index = 0
    seen = set()
    while index < len(arguments):
        value = arguments[index]
        if value == '--':
            result.entries.extend(arguments[index + 1:])
            break
        if value == '--json':
            if result.json:
                raise PreviewError('option may only appear once: --json')
            result.json = True
        elif value == '--allow-empty':
            if result.allow_empty:
                raise PreviewError('option may only appear once: --allow-empty')
            result.allow_empty = True
        elif value == '--filter' or value.startswith('--filter='):
            if value == '--filter':
                index += 1
                if index == len(arguments):
                    raise PreviewError('--filter requires a literal substring')
                text = arguments[index]
            else:
                text = value.partition('=')[2]
            if not text:
                raise PreviewError('filter must be a nonempty literal substring')
            result.filter.append(text)
        elif value in ('--compile-timeout-ms', '--run-timeout-ms'):
            index += 1
            if index == len(arguments) or not arguments[index].isascii() or not arguments[index].isdecimal():
                raise PreviewError(value + ' requires an integer in 1..2147483647')
            budget = int(arguments[index])
            if not 1 <= budget <= 2147483647:
                raise PreviewError(value + ' requires an integer in 1..2147483647')
            key = 'compile_ms' if value == '--compile-timeout-ms' else 'run_ms'
            if key in seen:
                raise PreviewError('option may only appear once: ' + value)
            seen.add(key)
            setattr(result, key, budget)
            setattr(result, 'compile_source' if key=='compile_ms' else 'run_source', 'cli')
        elif value.startswith('-'):
            raise PreviewError('unsupported Preview option: ' + value + '; use --help')
        else:
            result.entries.append(value)
        index += 1
    return result


def find_project(cwd):
    for directory in (cwd, *cwd.parents):
        manifest = directory / 'package.tk'
        if manifest.is_symlink():
            raise PreviewError('project manifest cannot be a symbolic link: ' + str(manifest))
        if manifest.is_file():
            return directory
    raise PreviewError('package.tk not found; run toka test inside a project')


def validate_entry(argument, cwd, root):
    raw = Path(argument)
    current = Path(raw.anchor) if raw.is_absolute() else cwd
    parts = raw.parts[1:] if raw.is_absolute() else raw.parts
    for part in parts:
        if part == '.':
            continue
        if part == '..':
            current = current.parent
            continue
        parent = current.resolve()
        current = current / part
        if current.is_symlink() and (parent == root or root in parent.parents):
            raise PreviewError('test entry cannot traverse a symbolic link: ' + argument)
    try:
        path = current.resolve(strict=True)
        relative = path.relative_to(root)
    except (ValueError, OSError) as error:
        raise PreviewError('test entry is missing or outside the project: ' + argument) from error
    if not path.is_file() or path.suffix != '.tk':
        raise PreviewError('test entry must be a regular .tk file: ' + argument)
    for directory in path.parents:
        if directory == root:
            break
        if (directory / 'package.tk').exists():
            raise PreviewError('test entry belongs to a nested project: ' + argument)
    # Resolve actual spelling even on case-insensitive filesystems.
    actual = root
    for part in relative.parts:
        choices = [p for p in actual.iterdir() if p.name == part]
        if not choices:
            target = actual / part
            choices = [p for p in actual.iterdir() if p.name.casefold() == part.casefold() and p.samefile(target)]
        if len(choices) != 1:
            raise PreviewError('ambiguous entry path: ' + argument)
        actual = choices[0]
    with actual.open('rb') as stream:
        stream.read(1)
    identifier = actual.relative_to(root).as_posix()
    try:
        identifier.encode('utf-8')
    except UnicodeError as error:
        invalid=PreviewError('test paths must be valid UTF-8')
        invalid.raw_input_base64=base64.b64encode(os.fsencode(argument)).decode('ascii')
        raise invalid from error
    return identifier, actual


def select_entries(root, cwd, entries, filters):
    excluded = []
    candidates = {}
    if entries:
        for argument in entries:
            identifier, path = validate_entry(argument, cwd, root)
            candidates[identifier] = path
    else:
        directory = root / 'tests'
        if directory.is_symlink():
            excluded.append({'path': 'tests', 'reason': 'symlink'})
        elif directory.exists():
            def visit(parent):
                # Explicit stat/read checks surface unreadable entries instead of ignoring them.
                for path in sorted(parent.iterdir(), key=lambda p: os.fsencode(p.name)):
                    relative = path.relative_to(root).as_posix()
                    kind = path.lstat().st_mode
                    if stat.S_ISLNK(kind):
                        excluded.append({'path': relative, 'reason': 'symlink'})
                    elif stat.S_ISDIR(kind):
                        if (path / 'package.tk').exists():
                            excluded.append({'path': relative, 'reason': 'nested_project'})
                        else:
                            visit(path)
                    elif path.name.endswith('_test.tk'):
                        path.stat()
                        identifier, actual = validate_entry(str(path), cwd, root)
                        candidates[identifier] = actual
            if (directory / 'package.tk').exists():
                excluded.append({'path': 'tests', 'reason': 'nested_project'})
            else:
                visit(directory)
    selected = [(identifier, path) for identifier, path in candidates.items()
                if not filters or any(value in identifier for value in filters)]
    selected.sort(key=lambda pair: pair[0].encode('utf-8'))
    return selected, {'mode': 'explicit' if entries else 'discovery', 'filters': filters,
                      'candidate_count': len(candidates), 'selected_count': len(selected),
                      'excluded': excluded}


@contextmanager
def project_write_lock(root, compile_ms):
    import fcntl
    state = root / '.toka'
    if state.is_symlink():
        raise PreviewError('project state directory cannot be a symbolic link')
    state.mkdir(exist_ok=True)
    path = state / 'test-context.lock'
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    started = time.monotonic()
    with os.fdopen(descriptor, 'r+b') as stream:
        while True:
            if (time.monotonic() - started) * 1000 >= compile_ms:
                raise PreviewError('timed out waiting for project dependency write lock')
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() - started < 0.04:
                    print('Waiting for project dependency lock', flush=True)
                if (time.monotonic() - started) * 1000 >= compile_ms:
                    raise PreviewError('timed out waiting for project dependency write lock')
                time.sleep(0.02)
        try:
            yield
        finally:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def project_context(root, compile_ms=DEFAULT_COMPILE_MS):
    manifest, lock, state = root / 'package.tk', root / 'package.lock', root / '.toka'
    if lock.is_symlink():
        raise ConfigurationError('package.lock cannot be a symbolic link')
    with project_write_lock(root, compile_ms):
        before = lock.read_bytes() if lock.is_file() else None
        resolver = packages.Resolver(manifest, lock, state,
                                     offline=os.environ.get('TOKA_OFFLINE') == '1',
                                     refresh=False, locked=True)
        resolver.run()
        after = lock.read_bytes() if lock.is_file() else None
        if before != after:
            raise PreviewError('project lock changed during test preparation')
        flags = []
        for value in packages.compiler_mappings(lock, state):
            flags += ['--pkg', value]
        for value in packages.compiler_node_mappings(lock):
            flags += ['--pkg-node', value]
        node = packages.workspace_node(manifest, lock)
        for mapping in packages.workspace_library_mappings(root, lock):
            alias = mapping.partition('=')[0]
            flags += ['--pkg', mapping, '--pkg-node', alias + '=' + node]
        if node:
            flags += ['--workspace-node', node, '--workspace-root', str(root)]
        return flags, hashlib.sha256(after).hexdigest() if after is not None else None


def native_inputs(root, sdk_lib, run_dir):
    plan = packages.native_build_plan(root / 'package.lock', root / '.toka')
    if not plan['packages']:
        return [], {'packages': [], 'objects': []}
    helper = sdk_lib / 'toolchain/toka_build.py'
    if not helper.is_file():
        raise PreviewError('active SDK native build helper is missing: ' + str(helper))
    spec = importlib.util.spec_from_file_location('_toka_test_native', helper)
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    class NativeProcesses:
        run=staticmethod(streamed_run)
        def __getattr__(self,name):return getattr(__import__('subprocess'),name)
    build.subprocess=NativeProcesses()
    previous_cwd, previous_lib = Path.cwd(), os.environ.get('TOKA_LIB')
    out, err = run_dir / 'native-build.stdout', run_dir / 'native-build.stderr'
    try:
        os.chdir(root)
        os.environ['TOKA_LIB'] = str(sdk_lib)
        with out.open('x') as stdout, err.open('x') as stderr:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    deps, cflags, searches, libraries, frameworks, identity = build.native_package_plan([plan['target']])
                    objects = build.compile_native_sources(deps, str(run_dir), cflags)
                except RuntimeError as error:
                    print(str(error), file=stderr)
                    raise PreviewError('native input preparation failed; see ' + str(err)) from error
        flags = list(objects)
        for flag, values in [('--link-search', searches), ('--link-lib', libraries), ('--link-framework', frameworks)]:
            for value in values:
                flags += [flag, value]
        return flags, {'packages': [p['alias'] for p in deps], 'objects': objects,
                       'fingerprint': identity, 'stdout': str(out), 'stderr': str(err)}
    finally:
        os.chdir(previous_cwd)
        if previous_lib is None:
            os.environ.pop('TOKA_LIB', None)
        else:
            os.environ['TOKA_LIB'] = previous_lib


def prepare_worker(mode, root, sdk_lib, run_dir, compile_ms):
    """Worker descendants inherit the supervisor-owned group, including native tools."""
    path = run_dir / (mode + '-result.json')
    try:
        if mode == 'context':
            flags, digest = project_context(root, compile_ms)
            nodes = {key.partition('=')[0]: key.partition('=')[2] for key in packages.compiler_node_mappings(root/'package.lock')}
            graph = {'workspace_root': str(root), 'workspace_node': packages.workspace_node(root/'package.tk', root/'package.lock'),
                     'dependencies': [{'root': str(packages.package_root(entry, root/'.toka').resolve()), 'node': nodes[alias]}
                                      for alias, entry in packages.read_lock(root/'package.lock').items()]}
            data = {'flags': flags, 'lock_sha256': digest, 'provenance': graph}
        else:
            flags, identity = native_inputs(root, sdk_lib, run_dir)
            data = {'flags': flags, 'identity': identity}
        packages.atomic_write(path, json.dumps(data) + '\n')
        return 0
    except (OSError, packages.PackageError, PreviewError) as error:
        packages.atomic_write(path, json.dumps({'error': str(error), 'type': type(error).__name__, 'category': getattr(error,'category','infrastructure_error')}) + '\n')
        print(str(error), file=sys.stderr)
        return 2


def link_driver_worker(status_path, original_path, arguments):
    """The cc child inherits the compiler's supervised group; no new session."""
    status_path = Path(status_path)
    status = {'state': 'starting', 'command': ['cc', *arguments], 'pid': None,
              'exit_code': None, 'signal': None, 'os_error': None}
    packages.atomic_write(status_path, json.dumps(status) + '\n')
    try:
        child = subprocess.Popen(status['command'], env=dict(os.environ, PATH=original_path))
    except OSError as error:
        status.update(state='launch_failed', os_error=error.errno, message=str(error))
        packages.atomic_write(status_path, json.dumps(status) + '\n')
        return 127
    status.update(state='started', pid=child.pid)
    packages.atomic_write(status_path, json.dumps(status) + '\n')
    code = child.wait()
    status.update(state='completed', exit_code=code if code >= 0 else None,
                  signal=-code if code < 0 else None)
    packages.atomic_write(status_path, json.dumps(status) + '\n')
    return code if code >= 0 else 128 - code


def link_driver_bridge(directory, environment):
    """Observe Linux cc exec separately from a driver's normal link rejection.

    The immutable compiler still invokes cc. An invocation-private cc shim
    records the actual launch and delegates with the original search path.
    Its lifetime is included in the existing combined compilation budget.
    """
    if not sys.platform.startswith('linux'):
        return None
    bridge = directory / 'link-driver'
    bridge.mkdir()
    status_path = bridge / 'status.json'
    packages.atomic_write(status_path, json.dumps({'state': 'not_invoked'}) + '\n')
    original_path = environment.get('PATH', os.defpath)
    command = [sys.executable, str(Path(__file__).resolve()), '--link-driver-worker',
               str(status_path), original_path]
    shim = bridge / 'cc'
    shim.write_text('#!/bin/sh\nexec ' + shlex.join(command) + ' "$@"\n')
    shim.chmod(0o700)
    environment['PATH'] = str(bridge) + os.pathsep + original_path
    return status_path


def link_driver_result(status_path):
    if status_path is None:
        return None
    status = json.loads(status_path.read_text())
    status['record_path'] = str(status_path)
    return status


def phase_error(phase):
    if phase.get('launch_error') or phase.get('supervision_error') or phase['cleanup']['status'] == 'failed':
        raise PreviewError('process supervision failed; see ' + phase['stderr'])
    if phase['interrupt_signal'] is not None:
        raise Interrupted('user interrupt')


def run_preparation(supervisor, mode, root, sdk_lib, run_dir, receipt, budget, compile_ms):
    phase = supervisor.run([sys.executable, str(Path(__file__).resolve()), '--worker', mode,
                            str(root), str(sdk_lib), str(run_dir), str(compile_ms)], root, run_dir,
                           mode, dict(os.environ), budget)
    receipt.setdefault('preparation', {})[mode] = phase
    phase_error(phase)
    if phase['trigger'] is not None:
        raise PreviewError(mode + ' preparation failed: ' + phase['trigger'])
    result_path = run_dir / (mode + '-result.json')
    if not result_path.is_file():
        raise PreviewError(mode + ' preparation produced no result; see ' + phase['stderr'])
    data = json.loads(result_path.read_text())
    if 'error' in data:
        if data.get('category') == 'configuration_error':
            if data['type'] == 'PackageConfigurationError':raise packages.PackageConfigurationError(data['error'])
            raise ConfigurationError(data['error'])
        if data['type'] == 'PackageError':
            raise packages.PackageError(data['error'])
        raise PreviewError(data['error'])
    if phase['exit_code'] != 0 or phase['signal'] is not None:
        raise PreviewError(mode + ' preparation failed; see ' + phase['stderr'])
    return data


def finalize_receipt(receipt, supervisor, path, report=None, started=None):
    """Stage the receipt, then commit one immutable outcome with signals masked.

    Summary/staging interruptions are included. Signals after the commit snapshot
    are post-completion; they cannot change the recorded return value. Persistence
    failure still overrides the snapshot with infrastructure error 2.
    """
    receipt['finalized'] = False
    preparation_started = time.monotonic()
    persistence_error = None
    try:
        packages.atomic_write(path, json.dumps(receipt, indent=2) + '\n')
    except OSError as error:
        persistence_error = error
    numbers = {signal.SIGINT, signal.SIGTERM}
    previous = signal.pthread_sigmask(signal.SIG_BLOCK, numbers) if os.name == 'posix' else None
    try:
        if previous is not None:
            for number in sorted(signal.sigpending() & numbers):
                signal.sigwait({number})
                supervisor._interrupt(number, None)
        # Result commitment follows scheduling and all bounded cleanup attempts.
        receipt['interrupt_signal'] = supervisor.interrupt_signal
        receipt['interrupt_count'] = supervisor.interrupt_count
        if persistence_error is not None:
            receipt['exit_code'] = 2
            receipt['result'] = 'infrastructure_or_configuration_error'
            receipt['persistence_error'] = str(persistence_error)
        if receipt['exit_code'] != 2 and supervisor.interrupt_signal is not None:
            receipt['exit_code'] = 130
            receipt['result'] = 'interrupted'
            for result in receipt['tests']:
                if result['result'] == 'infrastructure_error':
                    result['result'] = 'interrupted'
        receipt['finalized'] = True
        try:
            packages.atomic_write(path, json.dumps(receipt, indent=2) + '\n')
            if report is not None:
                reports.materialize(report, receipt)
                reports.observe(report, 'report_preparation', preparation_started)
                reports.observe(report, 'total', started)
                report.pop('stage_starts',None);report.pop('active_stage',None)
                packages.atomic_write(path.parent/'report.json', json.dumps(report, ensure_ascii=True, indent=2) + '\n')
        except OSError as error:
            receipt['exit_code'] = 2
            receipt['result'] = 'infrastructure_or_configuration_error'
            receipt['persistence_error'] = str(error)
            # Best effort error receipt; failure never falls back to success.
            try:
                packages.atomic_write(path, json.dumps(receipt, indent=2) + '\n')
                if report is not None:
                    reports.materialize(report, receipt)
                    packages.atomic_write(path.parent/'report.json', json.dumps(report, ensure_ascii=True, indent=2) + '\n')
            except OSError:
                pass
            raise
    finally:
        if previous is not None:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous)
    if persistence_error is not None:
        raise persistence_error


def json_mode(arguments):
    index=0
    while index<len(arguments):
        value=arguments[index]
        if value=='--':return False
        if value=='--json':return True
        if value in ('--filter','--compile-timeout-ms','--run-timeout-ms'):index+=2
        else:index+=1
    return False


def deliver_json(report):
    """A buffered/absent stdout must not silently turn delivery failure into code 0."""
    target=sys.stdout
    try:
        if target is None:raise BrokenPipeError('stdout is unavailable')
        payload=json.dumps(report,ensure_ascii=True)+'\n'
        written=target.write(payload)
        if written!=len(payload):raise OSError('stdout did not accept the complete report')
        target.flush()
        return True
    except (OSError,UnicodeError,ValueError) as error:
        if sys.stderr is not None:
            try:print('Error: could not deliver test report: '+str(error),file=sys.stderr)
            except (OSError,ValueError):pass
        # Avoid a second failing flush during interpreter shutdown. The original
        # channel is already unavailable; the caller records incomplete delivery.
        try:sys.stdout=open(os.devnull,'w')
        except OSError:sys.stdout=None
        return False


def execute_preview(arguments, sdk_lib, tokac, cwd=None):
    supervisor = Supervisor()
    report = reports.new_report()
    report['raw_inputs_base64']=[base64.b64encode(os.fsencode(value)).decode('ascii') for value in arguments if any(0xDC80<=ord(c)<=0xDCFF for c in value)]
    started = time.monotonic()
    try:
        with supervisor.signals(), contextlib.redirect_stdout(sys.stderr):
            code = _execute_preview(arguments, sdk_lib, tokac, cwd, supervisor, report, started)
    except (OSError, ValueError, packages.PackageError, PreviewError, SupervisionError) as error:
        active=report.get('active_stage')
        if active in report.get('stage_starts',{}) and report['timings'][active]['state']=='not_started':
            reports.observe(report,active,report['stage_starts'][active]);report['timings'][active]['state']='aborted'
        if not report['finalized']:
            # Configuration can fail before normal project/artifact setup; still retain a report when possible.
            if report['artifact_root'] is None:
                fallback_started=time.monotonic()
                try:
                    root=find_project((cwd or Path.cwd()).resolve())
                    report['project_root']=str(root)
                    if report['timings']['project']['state']=='not_started':reports.observe(report,'project',fallback_started)
                    fallback_artifact=time.monotonic()
                    directory=root/'.toka/test-runs'
                    if (root/'.toka').is_symlink() or directory.is_symlink():raise PreviewError('unsafe artifact directory')
                    directory.mkdir(parents=True,exist_ok=True)
                    report['artifact_root']=tempfile.mkdtemp(prefix='i2b-error-',dir=directory)
                    if report['timings']['artifact_setup']['state']=='not_started':reports.observe(report,'artifact_setup',fallback_artifact)
                except (OSError,PreviewError):
                    if report['project_root'] is None and report['timings']['project']['state']=='not_started':
                        reports.observe(report,'project',fallback_started);report['timings']['project']['state']='aborted'
            report['result']='configuration_error' if isinstance(error, PreviewError) else 'infrastructure_error'
            report['reason']=str(error);report['exit_code']=2
            report['errors']=[{'code':None,'message':str(error),'phase':report.get('active_stage'),
                               'os_error':getattr(error,'errno',None),'source':reports.source_origin(None,None,None)}]
            report['termination'].update(reason=report['result'],phase=report.get('active_stage'))
            report['finalized']=True;reports.observe(report,'total',started)
        report.pop('stage_starts',None);report.pop('active_stage',None)
        if report['artifact_root'] is not None:
            try:packages.atomic_write(Path(report['artifact_root'])/'report.json',json.dumps(report,ensure_ascii=True,indent=2)+'\n')
            except OSError as persistence:
                report['exit_code']=2;report['result']='infrastructure_error';report['errors'].append({'code':None,'message':str(persistence),'phase':'report_preparation','os_error':getattr(persistence,'errno',None),'source':reports.source_origin(None,None,None)})
        if json_mode(arguments):
            deliver_json(report)
            return 2
        raise
    if json_mode(arguments) and not deliver_json(report):return 2
    return code


def _execute_preview(arguments, sdk_lib, tokac, cwd, supervisor, report, started):
    print(PREVIEW, file=sys.stderr)
    report['active_stage']='argument_parse'
    parsed_started=time.monotonic()
    report.setdefault('stage_starts',{})['argument_parse']=parsed_started
    options = parse_options(arguments)
    reports.observe(report,'argument_parse',parsed_started)
    if options is None:
        print('Usage: toka test [entry.tk ...] [--filter <literal>] [--allow-empty]')
        print('Explicit entries replace tests/**/*_test.tk discovery. Runs serially at the project root.')
        print('--compile-timeout-ms <ms> (30000), --run-timeout-ms <ms> (5000). --json emits one C6 report.')
        return 0
    report['active_stage']='project'
    project_started=time.monotonic()
    report.setdefault('stage_starts',{})['project']=project_started
    report['timeouts'].update(compile_ms=options.compile_ms,run_ms=options.run_ms,
                              compile_source=options.compile_source,
                              run_source=options.run_source)
    report['selection']['mode']='explicit' if options.entries else 'discovery'
    report['selection']['filters']=options.filter
    invocation = (cwd or Path.cwd()).resolve()
    root = find_project(invocation)
    report['project_root']=str(root)
    reports.observe(report,'project',project_started)
    report['active_stage']='artifact_setup'
    artifact_started=time.monotonic()
    report.setdefault('stage_starts',{})['artifact_setup']=artifact_started
    state = root / '.toka'
    artifact_parent = state / 'test-runs'
    if state.is_symlink() or artifact_parent.is_symlink():
        raise PreviewError('test artifact directory cannot be a symbolic link')
    artifact_parent.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix='i2a-', dir=artifact_parent))
    report['artifact_root']=str(run_dir)
    reports.observe(report,'artifact_setup',artifact_started)
    report['timeouts'].update(compile_ms=options.compile_ms,run_ms=options.run_ms,
                              compile_source=options.compile_source,
                              run_source=options.run_source)
    receipt = {'schema': 'toka.test-preview-i2a', 'version': 1, 'preview': True,
               'project_root': str(root), 'artifact_root': str(run_dir),
               'supervision': 'posix_process_group', 'accepted_future_defaults_ms': {'compile': DEFAULT_COMPILE_MS, 'run': DEFAULT_RUN_MS},
               'compiler_runtime_deadlines_enabled': True,
               'budgets_ms': {'compile_link': options.compile_ms, 'run': options.run_ms,
                              'context': DEFAULT_PREPARE_MS, 'native': DEFAULT_NATIVE_MS,
                              'probe': options.compile_ms, 'lock_wait': options.compile_ms}, 'tests': [], 'exit_code': 2}
    pending_error = None
    try:
        receipt['active_stage']=report['active_stage']='selection'
        selection_started=time.monotonic()
        report.setdefault('stage_starts',{})['selection']=selection_started
        selected, selection = select_entries(root, invocation, options.entries, options.filter)
        receipt['selection'] = selection
        reports.observe(report,'selection',selection_started)
        supervisor.check_interrupt()
        if not selected:
            receipt['result'] = 'empty' if options.allow_empty else 'configuration_error'
            receipt['reason'] = 'no_tests' if selection['candidate_count'] == 0 else 'no_matches'
            receipt['exit_code'] = 0 if options.allow_empty else 2
            print('No tests selected. total=0, passed=0; artifacts: ' + str(run_dir))
        else:
            for identifier, entry in selected:
                receipt['tests'].append({'id': identifier, 'entry': str(entry), 'result': 'not_run'})
            if os.name != 'posix':
                report['supervision']={'backend':'unsupported','scope':'none'}
                raise PreviewError('unsupported_supervision')
            receipt['active_stage']=report['active_stage']='identity'
            report['identity']['status']='failed'
            identity_started=time.monotonic()
            report.setdefault('stage_starts',{})['identity']=identity_started
            probe = supervisor.run([str(tokac), '--version'], root, run_dir, 'probe', dict(os.environ), options.compile_ms)
            receipt.setdefault('preparation', {})['probe'] = probe
            phase_error(probe)
            if probe['trigger'] is not None or probe['exit_code'] != 0 or probe['signal'] is not None:
                raise PreviewError('compiler identity probe failed; see ' + probe['stderr'])
            reports.compiler_identity(report,tokac,sdk_lib,probe)
            reports.observe(report,'identity',identity_started)
            receipt['active_stage']=report['active_stage']='dependencies'
            dependencies_started=time.monotonic()
            report.setdefault('stage_starts',{})['dependencies']=dependencies_started
            context = run_preparation(supervisor, 'context', root, sdk_lib, run_dir, receipt, DEFAULT_PREPARE_MS, options.compile_ms)
            flags = context['flags']
            receipt['lock_sha256'] = context['lock_sha256']
            receipt['provenance'] = context['provenance']
            report['identity']['lock_sha256']=context['lock_sha256']
            report['identity']['lock_path']=str(root/'package.lock') if context['lock_sha256'] else None
            report['identity']['status']='complete'
            receipt['compiler_flags'] = flags
            receipt['tokac'] = str(tokac)
            native = run_preparation(supervisor, 'native', root, sdk_lib, run_dir, receipt, DEFAULT_NATIVE_MS, options.compile_ms)
            native_flags, receipt['native_inputs'] = native['flags'], native['identity']
            reports.observe(report,'dependencies',dependencies_started)
            receipt['active_stage']=report['active_stage']='execution'
            execution_started=time.monotonic()
            report.setdefault('stage_starts',{})['execution']=execution_started
            failures = 0
            for index, ((identifier, entry), result) in enumerate(zip(selected, receipt['tests']), 1):
                supervisor.check_interrupt()
                directory = run_dir / ('%06d' % index)
                directory.mkdir()
                if entry.is_symlink() or not entry.is_file():
                    result['result'] = 'infrastructure_error'
                    raise PreviewError('selected entry disappeared or changed kind: ' + identifier)
                result['result'] = 'infrastructure_error'  # Replaced only after a successful child launch.
                environment = dict(os.environ, TOKA_TEST_RUN_DIR=str(run_dir), TOKA_TEST_CASE_DIR=str(directory))
                exe = directory / 'test-executable'
                compile_environment = dict(environment)
                driver_status = link_driver_bridge(directory, compile_environment)
                compile_phase = supervisor.run([str(tokac), '--diagnostics-json', '-I', str(sdk_lib), '-I', str(root / 'lib'),
                                           '-I', str(root), *flags, str(entry), *native_flags,
                                           '-o', str(exe), '-O0'], root, directory, 'compile', compile_environment, options.compile_ms)
                result['compile_link'] = compile_phase
                phase_error(compile_phase)
                driver = link_driver_result(driver_status)
                if driver is not None:
                    result['link_driver'] = driver
                    if driver['state'] == 'launch_failed':
                        raise OSError(driver['os_error'], 'external cc driver could not start: ' + driver['message'])
                    if compile_phase['trigger'] is None and (driver['state'] in ('starting', 'started') or driver.get('signal')):
                        raise PreviewError('external cc driver did not complete normally; see ' + str(driver_status))
                if compile_phase['signal'] is not None and compile_phase['trigger'] is None:
                    result['result'] = 'infrastructure_error'
                    raise PreviewError('compiler terminated by signal: ' + identifier)
                if compile_phase['exit_code'] != 0 or compile_phase['trigger'] is not None:
                    result['result'] = 'timed_out' if compile_phase['trigger'] == 'timeout' else 'compile_failed'
                    failures += 1
                    print('[FAILED (Compile)] ' + identifier)
                else:
                    run = supervisor.run([str(exe)], root, directory, 'run', environment, options.run_ms)
                    result['run'] = run
                    result['result'] = 'infrastructure_error'
                    phase_error(run)
                    result['result'] = 'passed' if run['exit_code'] == 0 and run['signal'] is None and run['trigger'] is None else ('timed_out' if run['trigger'] == 'timeout' else 'run_failed')
                    failures += result['result'] != 'passed'
                    print(('[OK] ' if result['result'] == 'passed' else '[FAILED (Runtime)] ') + identifier)
                print('  logs: ' + str(directory))
            reports.observe(report,'execution',execution_started)
            supervisor.check_interrupt()
            receipt['active_stage']=report['active_stage']='report_preparation'
            receipt['exit_code'] = 1 if failures else 0
            receipt['result'] = 'failed' if failures else 'passed'
            print('Preview results: %d selected, %d passed, %d failed; artifacts: %s' %
                  (len(selected), len(selected) - failures, failures, run_dir))
    except Interrupted:
        receipt['result'] = 'interrupted'
        receipt['exit_code'] = 130
        for result in receipt['tests']:
            if result['result'] == 'infrastructure_error':
                result['result'] = 'interrupted'
    except (OSError, packages.PackageError, PreviewError, SupervisionError) as error:
        receipt['result'] = 'configuration_error' if receipt.get('active_stage')=='selection' or getattr(error,'category',None)=='configuration_error' else 'infrastructure_or_configuration_error'
        receipt['error'] = str(error)
        receipt['error_category'] = 'configuration_error' if receipt['result']=='configuration_error' else 'infrastructure_error'
        receipt['os_error'] = getattr(error,'errno',None)
        if hasattr(error,'raw_input_base64'):report['raw_inputs_base64'].append(error.raw_input_base64)
        receipt['exit_code'] = 2
        pending_error = error
    finally:
        active=report.get('active_stage')
        if active in report.get('stage_starts',{}) and report['timings'][active]['state']=='not_started':
            reports.observe(report,active,report['stage_starts'][active]);report['timings'][active]['state']='aborted'
        finalize_receipt(receipt, supervisor, run_dir / 'preview.json', report, started)
    if pending_error is not None:
        raise pending_error
    return receipt['exit_code']


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == '--link-driver-worker':
        return link_driver_worker(sys.argv[2], sys.argv[3], sys.argv[4:])
    if len(sys.argv) == 7 and sys.argv[1] == '--worker' and sys.argv[2] in ('context', 'native'):
        return prepare_worker(sys.argv[2], *[Path(value) for value in sys.argv[3:6]], int(sys.argv[6]))
    # Manager-owned options precede --; user options cannot replace SDK/tool paths.
    try:
        separator = sys.argv.index('--')
        internal = Parser(add_help=False, allow_abbrev=False)
        internal.add_argument('--sdk-lib', type=Path, required=True)
        internal.add_argument('--tokac', type=Path, required=True)
        settings = internal.parse_args(sys.argv[1:separator])
        return execute_preview(sys.argv[separator + 1:], settings.sdk_lib.resolve(), settings.tokac.resolve())
    except (ValueError, OSError, packages.PackageError, PreviewError) as error:
        print('Error: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
