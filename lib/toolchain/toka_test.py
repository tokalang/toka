#!/usr/bin/env python3
"""I1 project test preview. Production supervision/JSON belong to I2."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import contextlib
import hashlib
import importlib.util
import json
import os
import stat
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import toka_package as packages

DEFAULT_COMPILE_MS = 30000
DEFAULT_RUN_MS = 5000
PREVIEW = ('Preview: I1 project test runner, not the stable project test contract. '
           'Compiler/runtime deadlines, process supervision and JSON output await I2.')


class PreviewError(RuntimeError):
    pass


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise PreviewError(message)


def parse_options(arguments):
    if arguments == ['--help']:
        return None
    result = argparse.Namespace(entries=[], filter=[], allow_empty=False)
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == '--':
            result.entries.extend(arguments[index + 1:])
            break
        if value == '--allow-empty':
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
        elif value.startswith('-'):
            raise PreviewError('unsupported I1 Preview option: ' + value + '; use --help')
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
        raise PreviewError('test paths must be valid UTF-8') from error
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
def project_write_lock(root):
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
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if (time.monotonic() - started) * 1000 >= DEFAULT_COMPILE_MS:
                    raise PreviewError('timed out waiting for project dependency write lock')
                time.sleep(0.02)
        try:
            yield
        finally:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def project_context(root):
    manifest, lock, state = root / 'package.tk', root / 'package.lock', root / '.toka'
    if lock.is_symlink():
        raise PreviewError('package.lock cannot be a symbolic link')
    with project_write_lock(root):
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
    previous_cwd, previous_lib = Path.cwd(), os.environ.get('TOKA_LIB')
    out, err = run_dir / 'native.stdout', run_dir / 'native.stderr'
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


def run_phase(command, root, directory, name, environment):
    stdout_path, stderr_path = directory / (name + '.stdout'), directory / (name + '.stderr')
    started = time.perf_counter_ns()
    # This is intentionally synchronous I1 execution, not C4 supervision.
    with stdout_path.open('xb') as stdout, stderr_path.open('xb') as stderr:
        child = subprocess.run(command, cwd=root, env=environment, stdin=subprocess.DEVNULL,
                               stdout=stdout, stderr=stderr)
    return {'command': [str(x) for x in command], 'exit_code': child.returncode if child.returncode >= 0 else None,
            'signal': -child.returncode if child.returncode < 0 else None,
            'duration_ms': (time.perf_counter_ns() - started) / 1e6,
            'stdout': str(stdout_path), 'stderr': str(stderr_path)}


def execute_preview(arguments, sdk_lib, tokac, cwd=None):
    print(PREVIEW, file=sys.stderr)
    options = parse_options(arguments)
    if options is None:
        print('Usage: toka test [entry.tk ...] [--filter <literal>] [--allow-empty]')
        print('Explicit entries replace tests/**/*_test.tk discovery. Runs serially at the project root.')
        print('Preview I1: --json and compiler/runtime timeout options are not available until I2.')
        return 0
    invocation = (cwd or Path.cwd()).resolve()
    root = find_project(invocation)
    state = root / '.toka'
    artifact_parent = state / 'test-runs'
    if state.is_symlink() or artifact_parent.is_symlink():
        raise PreviewError('test artifact directory cannot be a symbolic link')
    artifact_parent.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix='i1-', dir=artifact_parent))
    receipt = {'schema': 'toka.test-preview-i1', 'version': 1, 'preview': True,
               'project_root': str(root), 'artifact_root': str(run_dir),
               'supervision': 'not_implemented_i1', 'accepted_future_defaults_ms': {'compile': DEFAULT_COMPILE_MS, 'run': DEFAULT_RUN_MS},
               'compiler_runtime_deadlines_enabled': False, 'tests': [], 'exit_code': 2}
    try:
        selected, selection = select_entries(root, invocation, options.entries, options.filter)
        receipt['selection'] = selection
        if not selected:
            receipt['result'] = 'empty' if options.allow_empty else 'configuration_error'
            receipt['reason'] = 'no_tests' if selection['candidate_count'] == 0 else 'no_matches'
            receipt['exit_code'] = 0 if options.allow_empty else 2
            print('No tests selected. total=0, passed=0; artifacts: ' + str(run_dir))
            return receipt['exit_code']
        if os.name != 'posix':
            raise PreviewError('I1 project test preview supports native POSIX hosts only')
        for identifier, entry in selected:
            receipt['tests'].append({'id': identifier, 'entry': str(entry), 'result': 'not_run'})
        flags, lock_digest = project_context(root)
        receipt['lock_sha256'] = lock_digest
        receipt['compiler_flags'] = flags
        receipt['tokac'] = str(tokac)
        native_flags, receipt['native_inputs'] = native_inputs(root, sdk_lib, run_dir)
        failures = 0
        for index, ((identifier, entry), result) in enumerate(zip(selected, receipt['tests']), 1):
            directory = run_dir / ('%06d' % index)
            directory.mkdir()
            if entry.is_symlink() or not entry.is_file():
                result['result'] = 'infrastructure_error'
                raise PreviewError('selected entry disappeared or changed kind: ' + identifier)
            result['result'] = 'infrastructure_error'  # Replaced only after a successful child launch.
            environment = dict(os.environ, TOKA_TEST_RUN_DIR=str(run_dir), TOKA_TEST_CASE_DIR=str(directory))
            exe = directory / 'test-executable'
            compile_phase = run_phase([str(tokac), '-I', str(sdk_lib), *flags, str(entry), *native_flags,
                                       '-o', str(exe), '-O0'], root, directory, 'compile', environment)
            result['compile_link'] = compile_phase
            if compile_phase['signal'] is not None:
                result['result'] = 'infrastructure_error'
                raise PreviewError('compiler terminated by signal: ' + identifier)
            if compile_phase['exit_code'] != 0:
                result['result'] = 'compile_failed'
                failures += 1
                print('[FAILED (Compile)] ' + identifier)
            else:
                run = run_phase([str(exe)], root, directory, 'run', environment)
                result['run'] = run
                result['result'] = 'passed' if run['exit_code'] == 0 and run['signal'] is None else 'run_failed'
                failures += result['result'] != 'passed'
                print(('[OK] ' if result['result'] == 'passed' else '[FAILED (Runtime)] ') + identifier)
            print('  logs: ' + str(directory))
        receipt['exit_code'] = 1 if failures else 0
        receipt['result'] = 'failed' if failures else 'passed'
        print('Preview results: %d selected, %d passed, %d failed; artifacts: %s' %
              (len(selected), len(selected) - failures, failures, run_dir))
        return receipt['exit_code']
    except (OSError, packages.PackageError, PreviewError) as error:
        receipt['result'] = 'infrastructure_or_configuration_error'
        receipt['error'] = str(error)
        raise
    finally:
        packages.atomic_write(run_dir / 'preview.json', json.dumps(receipt, indent=2) + '\n')


def main():
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
