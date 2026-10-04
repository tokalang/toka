"""Scope public evidence after a complete compiler check; never prune analysis."""
import argparse
import base64
import contextlib
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys

import toka_package as packages
from toka_test import project_context, ConfigurationError as ProjectConfigurationError
from toka_test_report import source_origin


class ConfigurationError(ValueError):
    pass


class StderrChannelError(OSError):
    pass


def forward_stderr(data):
    """Keep stdout reserved for the report even when stderr is unusable."""
    try:
        stream = sys.stderr
        if stream is None:
            raise OSError(errno.EBADF, 'stderr is not available')
        binary = getattr(stream, 'buffer', None)
        if binary is not None:
            binary.write(data)
        else:
            stream.write(data.decode('utf-8', errors='replace'))
        stream.flush()
    except (OSError, ValueError) as error:
        # CPython otherwise retries the failed buffered flush at shutdown and
        # replaces our infrastructure exit code with 120.
        sys.stderr = sys.__stderr__ = io.StringIO()
        raise StderrChannelError(getattr(error, 'errno', None), 'cannot write stderr: ' + str(error)) from error


class PreparationProgress:
    def write(self, text):
        forward_stderr(text.encode('utf-8'))
        return len(text)

    def flush(self):
        pass  # Each write was already flushed by forward_stderr.


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ConfigurationError(message)


def identity(prefix, value):
    return prefix + hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                             separators=(',', ':')).encode()).hexdigest()


def view(document, kind, target, decision, source, graph, sdk_root, raw):
    source = str(Path(source).resolve())
    if kind == 'file':target = str(Path(target).resolve())
    if not isinstance(document, dict) or document.get('schema') != 'toka.semantic-evidence' or document.get('version') != 1 or not isinstance(document.get('records'), list):
        raise RuntimeError('compiler did not produce public evidence v1')
    records = document['records']
    known_paths = {source}
    decisions = set()
    annotated = []
    for record in records:
        for key in ('primary_location', 'origin_location'):
            location = record.get(key)
            if not isinstance(location, dict) or not isinstance(location.get('file'), str):
                raise RuntimeError('compiler evidence has an invalid location')
            if location['file']:
                known_paths.add(str(Path(location['file']).resolve()))
        if record.get('decision') not in ('Allow', 'Reject', 'ConservativeReject'):
            raise RuntimeError('compiler evidence has an invalid decision')
        decision_id = identity('decision-v1-', record)
        reason = {key: record[key] for key in ('reason', 'subject', 'origin', 'primary_location', 'origin_location')}
        reason_id = identity('reason-v1-', reason)
        decisions.add(decision_id)
        annotated.append(dict(record, decision_id=decision_id, reason_id=reason_id,
                              source=source_origin(record['primary_location']['file'], graph, sdk_root),
                              origin_source=source_origin(record['origin_location']['file'], graph, sdk_root)))
    if kind == 'file' and target not in known_paths:
        raise ConfigurationError('target is not represented in the checked source/evidence graph: ' + target)
    if kind == 'decision' and decision not in decisions:
        raise ConfigurationError('unknown decision identifier: ' + str(decision))
    selected = []
    retained_rejections = []
    for record in annotated:
        matches = kind == 'all' or (kind == 'decision' and record['decision_id'] == decision)
        if kind == 'file':
            matches = any(location['file'] and str(Path(location['file']).resolve()) == target
                          for location in (record['primary_location'], record['origin_location']))
        if matches or record['decision'] != 'Allow':
            selected.append(record)
            if not matches:retained_rejections.append(record['decision_id'])
    reasons = {}
    for record in selected:
        reasons[record['reason_id']] = {key: record[key] for key in
            ('reason', 'subject', 'origin', 'primary_location', 'origin_location', 'source', 'origin_source')}
    return {'schema':'toka.semantic-evidence-view', 'version':1,
            'scope':{'kind':kind, 'target':decision if kind == 'decision' else target,
                     'output_filtered':len(selected) < len(records)},
            'analysis':{'scope':'full', 'exit_code':raw.returncode,
                        'result':'passed' if raw.returncode == 0 else 'failed',
                        'records_total':len(records), 'records_emitted':len(selected),
                        'retained_rejections':retained_rejections},
            'records':selected, 'reasons':reasons}


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    report = {'schema':'toka.semantic-evidence-view', 'version':1, 'result':'configuration_error',
              'exit_code':2, 'scope':{'kind':None,'target':None,'output_filtered':False,'input':argv},
              'analysis':{'scope':'full','result':'not_started','exit_code':None,'records_total':None,
                          'records_emitted':0,'retained_rejections':[]},
              'records':[], 'reasons':{}, 'compiler':None, 'errors':[]}
    raw = None
    try:
        try:
            for value in argv:value.encode('utf-8')
        except UnicodeError as error:
            encoded = [os.fsencode(value) for value in argv]
            report['scope']['input_base64'] = [base64.b64encode(value).decode('ascii') for value in encoded]
            report['scope']['input'] = [value.decode('utf-8', errors='replace') for value in encoded]
            raise ConfigurationError('evidence arguments must be valid UTF-8') from error
        parser = Parser(description=__doc__)
        parser.add_argument('--compiler', type=Path, required=True)
        parser.add_argument('--sdk-lib', type=Path, required=True)
        parser.add_argument('source', type=Path)
        parser.add_argument('--json', action='store_true')
        parser.add_argument('--scope', default='file')
        parser.add_argument('--target', type=Path)
        parser.add_argument('--decision')
        parser.add_argument('--raw-project', action='store_true')
        parser.add_argument('-I', dest='includes', action='append', default=[])
        parser.add_argument('-o')
        args = parser.parse_args(argv)
        report['scope'].update(kind=args.scope, target=args.decision if args.scope == 'decision' else str(args.target or args.source))
        if args.scope not in ('file','decision','all'):
            raise ConfigurationError('invalid evidence scope: ' + args.scope)
        if (args.scope == 'decision') != bool(args.decision) or (args.target is not None and args.scope != 'file'):
            raise ConfigurationError('--decision requires scope decision; --target requires scope file')
        try:
            source = args.source.resolve(strict=True)
            target = str((args.target or args.source).resolve(strict=True)) if args.scope == 'file' else None
        except (OSError,ValueError) as error:
            raise ConfigurationError('invalid source/target: ' + str(error)) from error
        if not source.is_file():raise ConfigurationError('source must be a file')
        if target is not None and not Path(target).is_file():raise ConfigurationError('target must be a file')
        report['scope']['target'] = args.decision if args.scope == 'decision' else target
        flags = []
        root = Path.cwd().resolve()
        graph = None
        if not args.raw_project and (root/'package.tk').is_file():
            with contextlib.redirect_stdout(PreparationProgress()):
                flags, _ = project_context(root)
            graph = {'workspace_root':str(root),'workspace_node':packages.workspace_node(root/'package.tk',root/'package.lock'),
                     'dependencies':[{'root':str(packages.package_root(entry, root/'.toka').resolve()),
                                      'node':packages.package_node_id(entry)}
                                     for entry in packages.read_lock(root/'package.lock').values()]}
        compiler = args.compiler.resolve()
        command = [str(compiler),'--semantic-evidence=json','--check-only','-I',str(args.sdk_lib),*flags]
        for include in args.includes:command += ['-I',include]
        if args.o:command += ['-o',args.o]
        command.append(str(source))
        raw = subprocess.run(command, capture_output=True,
                             env=dict(os.environ, TOKA_LIB=str(args.sdk_lib.resolve())))
        report['analysis'].update(result='passed' if raw.returncode == 0 else 'failed',exit_code=raw.returncode)
        report['compiler'] = {'argv':command,'exit_code':raw.returncode,
                              'stdout_sha256':hashlib.sha256(raw.stdout).hexdigest(),
                              'stderr_base64':base64.b64encode(raw.stderr).decode('ascii')}
        document = json.loads(raw.stdout)
        if isinstance(document, dict) and isinstance(document.get('records'), list):
            report['analysis']['records_total'] = len(document['records'])
        scoped = view(document, args.scope, target, args.decision, str(source), graph,
                      str(args.sdk_lib.resolve().parent), raw)
        report.update(scoped)
        report['scope']['input'] = argv
        report['result'] = report['analysis']['result']
        report['exit_code'] = raw.returncode if raw.returncode >= 0 else 128 - raw.returncode
    except (ConfigurationError, ProjectConfigurationError, packages.PackageConfigurationError) as error:
        report.update(result='configuration_error',exit_code=2)
        report['errors'] = [{'category':'configuration_error','message':str(error)}]
    except (OSError, ValueError, RuntimeError, KeyError, TypeError) as error:
        report.update(result='infrastructure_error',exit_code=2)
        report['errors'] = [{'category':'infrastructure_error','message':str(error)}]
        if raw is not None and isinstance(report['compiler'], dict):
            report['compiler']['stdout_base64'] = base64.b64encode(raw.stdout).decode('ascii')
    if raw is not None and raw.stderr:
        try:
            forward_stderr(raw.stderr)
        except StderrChannelError as error:
            report.update(result='infrastructure_error',exit_code=2)
            report['errors'].append({'category':'infrastructure_error','message':str(error),'channel':'stderr','errno':error.errno})
            if isinstance(report['compiler'], dict):
                report['compiler']['stdout_base64'] = base64.b64encode(raw.stdout).decode('ascii')
    print(json.dumps(report, sort_keys=True))
    return report['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
