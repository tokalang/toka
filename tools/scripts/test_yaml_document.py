#!/usr/bin/env python3
"""Flat YAML library contract; no JSON conversion or compiler modifications."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    cases = [('', None), ('# empty\n', None), ('TRUE', True), ('False', False), ('yes', 'yes'),
             ('0123', 123), ('0x1a', 26), ('1e3', 1000), ('3.14159', 3.14159),
             ('.inf', float('inf')), ('+.inf', float('inf')), ('-.Inf', -float('inf')),
             ('.NAN', float('nan')), ('".inf"', '.inf'), ("'it''s owned'", "it's owned"),
             ('a: [1, [2, 3], {k: v}]', {'a': [1, [2, 3], {'k': 'v'}]}),
             ('- a: 1\n  b: 2\n- a: 3\n  b: 4', [{'a': 1, 'b': 2}, {'a': 3, 'b': 4}]),
             ('value: |-\n  owned\n', {'value': 'owned'}),
             ('name: "\\u00e9"\nflag: null', {'name': 'é', 'flag': None}),
             ('root: {empty, enabled: true}', {'root': {'empty': None, 'enabled': True}})]
    errors = [('app:\n\tname: bad', 'tab indent is not allowed', 2, 1),
              ('key: 1\nkey: 2', "duplicate key 'key'", 2, 1),
              ('%YAML 1.2\napp: 1', 'directives % are not supported', 1, 1),
              ('---\na: 1\n---\nb: 2', 'multi-document YAML is not supported', 3, 1),
              ('&name', 'anchors and aliases are not supported', 1, 1),
              ('*name', 'anchors and aliases are not supported', 1, 1),
              ('!tag value', 'tags are not supported', 1, 1)]
    with tempfile.TemporaryDirectory(prefix='toka-yaml-flat-') as directory:
        work = Path(directory)

        def compile(source, *flags):
            scope = ROOT if source.is_relative_to(ROOT) else work
            return subprocess.run([str(compiler), '--workspace-node', 'yaml-tests', '--workspace-root', str(scope),
                                   str(source), *map(str, flags)], cwd=ROOT, env=env,
                                  text=True, capture_output=True, timeout=120)

        def check(source, diagnostic=None):
            normal = compile(source, '--check-only')
            shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == (1 if diagnostic else 0), (source.name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            json.loads(shadow.stdout)
            output = work / source.stem
            built = compile(source, '-o', output)
            if diagnostic:
                assert built.returncode == 1 and diagnostic in built.stderr and not output.exists(), built.stderr
                for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                    failed = compile(source, flag, '-o', work / (source.stem + suffix))
                    assert failed.returncode == 1 and not (work / (source.stem + suffix)).exists(), failed.stderr
            else:
                assert built.returncode == 0, built.stderr
                ran = subprocess.run([str(output)], text=True, capture_output=True, timeout=60)
                assert ran.returncode == 0, (source.name, ran.returncode, ran.stdout, ran.stderr)
            print('PASS ' + source.name, flush=True)

        lines = ['import stdx/serde/yaml::{YamlDocument}', 'fn main() -> i32 {']
        for case, (text, expected) in enumerate(cases):
            lines += ['{', f'auto result = YamlDocument::parse({json.dumps(text, ensure_ascii=False)})',
                      f'if result.is_err() {{ return {case + 1} }}', 'auto document = result.unwrap()', 'auto root = document.root()']
            def verify(value, node, tag):
                kind = 0 if value is None else 1 if isinstance(value, bool) else 2 if isinstance(value, (int, float)) else 3 if isinstance(value, str) else 4 if isinstance(value, list) else 5
                lines.append(f'if document.kind({node}) != {kind} {{ return {case + 1} }}')
                if kind == 1:
                    lines.append(f'if document.boolean({node}).unwrap() != {str(value).lower()} {{ return {case + 1} }}')
                elif kind == 2:
                    lines.append(f'auto n_{tag} = document.number({node}).unwrap()')
                    if math.isnan(value):
                        lines.append(f'if n_{tag} == n_{tag} {{ return {case + 1} }}')
                    elif math.isinf(value):
                        lines.append(f'if n_{tag} {"<=" if value > 0 else ">="} 0.0 || n_{tag} / 2.0 != n_{tag} {{ return {case + 1} }}')
                    else:
                        lines.extend([f'auto delta_{tag} = n_{tag} - ({float(value)!r})',
                                  f'if delta_{tag} != delta_{tag} || delta_{tag} > 0.000000001 || delta_{tag} < -0.000000001 {{ return {case + 1} }}'])
                elif kind == 3:
                    lines.append(f'if !document.text({node}).equals({json.dumps(value, ensure_ascii=False)}) {{ return {case + 1} }}')
                elif kind == 4:
                    lines.append(f'if document.len({node}) != {len(value)} {{ return {case + 1} }}')
                    current = f'child_{tag}_0'
                    lines.append(f'auto {current} = document.first({node})')
                    for i, item in enumerate(value):
                        verify(item, current, tag + '_' + str(i))
                        following = f'child_{tag}_{i+1}'
                        lines.append(f'auto {following} = document.next({current})')
                        current = following
                    lines.append(f'if {current} != 0 {{ return {case + 1} }}')
                elif kind == 5:
                    lines.append(f'if document.len({node}) != {len(value)} {{ return {case + 1} }}')
                    for i, (key, item) in enumerate(value.items()):
                        child = f'member_{tag}_{i}'
                        lines.append(f'auto {child} = document.find({node}, {json.dumps(key, ensure_ascii=False)})')
                        lines.append(f'if !document.key({child}).equals({json.dumps(key, ensure_ascii=False)}) {{ return {case + 1} }}')
                        verify(item, child, tag + '_' + str(i))
            verify(expected, 'root', str(case))
            lines += ['if document.valid(0) || document.valid(999999) { return 90 }', '}']
        for text, message, line, column in errors:
            lines += ['{', f'auto result = YamlDocument::parse({json.dumps(text)})', 'if result.is_ok() { return 91 }',
                      'auto error = result.unwrap_err()', f'if error.line() != {line} || error.column() != {column} {{ return 92 }}',
                      f'if !error.message().as_str().equals({json.dumps(message)}) {{ return 93 }}', '}']
        lines += ['{', f'auto result = YamlDocument::parse({json.dumps("[" * 128 + "0" + "]" * 128)})',
                  'if result.is_err() { return 94 }', '}']
        for text in ('[' * 129 + '0' + ']' * 129,
                     '\n'.join('  ' * i + 'k:' for i in range(130)) + '\n' + '  ' * 130 + '0'):
            lines += ['{', f'auto result = YamlDocument::parse({json.dumps(text)})',
                      'if result.is_ok() { return 95 }', 'auto error = result.unwrap_err()',
                      'if !error.message().as_str().equals("maximum recursion depth of 128 exceeded") { return 96 }', '}']
        lines += ['return 0', '}']
        source = work / 'matrix.tk'
        source.write_text('\n'.join(lines) + '\n')
        check(source)
        check(ROOT / 'tests/pass/g17_stdx_yaml_test.tk')
        check(ROOT / 'tests/semantics/yaml_flat_document/cleanup.tk')
        independent = work / 'independent.tk'
        independent.write_text('''import stdx/serde/yaml::{YamlDocument}
fn make() -> YamlDocument {
    auto input = string::from("a: owned")
    auto result = YamlDocument::parse(input.as_str())
    auto document = result.unwrap()
    return cede document
}
fn main() -> i32 {
    auto document = make()
    if document.text(document.find(document.root(), "a")) != "owned" { return 1 }
    return 0
}
''')
        check(independent)
        error_owner = work / 'error_owner.tk'
        error_owner.write_text('''import stdx/serde/yaml::{YamlDocument, YamlError}
fn fail() -> YamlError {
    auto input = string::from("key: first\\nkey: second")
    auto result = YamlDocument::parse(input.as_str())
    auto error = result.unwrap_err()
    return cede error
}
fn main() -> i32 {
    auto error = fail()
    if error.line() != 2 || error.column() != 1 { return 1 }
    if !error.message().as_str().equals("duplicate key 'key'") { return 2 }
    return 0
}
''')
        check(error_owner)
        for name, result_type, returned in [('view_escape', 'str', 'view'), ('descriptor_escape', '&str', '&view')]:
            source = work / (name + '.tk')
            source.write_text(f'''import stdx/serde/yaml::{{YamlDocument}}
fn escaped() -> {result_type} {{
    auto result = YamlDocument::parse("a: owned")
    auto document = result.unwrap()
    auto view = document.text(document.find(document.root(), "a"))
    return {returned}
}}
fn main() -> i32 {{ return 0 }}
''')
            check(source, 'E0455')
    print('YAML scalar/tree/error/lifecycle matrix passed, including nonfinite numbers.')


if __name__ == '__main__':
    main()
