#!/usr/bin/env python3
"""Flat JSON functionality, lifecycle rejection and normal/shadow parity."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    valid = ['null', 'true', 'false', '-0', '0e400', '1.2e-4', '3E10', '-4.56e2', '9007199254740993',
             '"hello"', '"\\u0000\\n\\t\\b\\f\\r\\/\\\\\\\""', '"\\u1f68\\uD83D\\uDE80"',
             '"中文🚀"', '[]', '{}', '{"a":1,"a":2}', '{"\\u0061":true}',
             '{"a":[1,false,null,{"message":"hello"}]}',
             json.dumps({"entries": [str(i) + '🚀' * 8 for i in range(60)]}, ensure_ascii=False),
             '1' + '0' * 309 + 'e-309', '[' * 128 + '0' + ']' * 128]
    invalid = ['', ' ', '[', '{', '[1', '{"a":1', '[1,]', '{"a":1,}', '[1 2]', '{"a" 1}',
               '{1:2}', 'true false', 'nullx', '+1', '01', '-', '.1', '1.', '1e', '1e+', 'NaN',
               '"\\x"', '"\\uZZZZ"', '"\\u12"', '"\\uD800"', '"\\uDC00"',
               '"\\uD800\\u0041"', '"line\nbreak"', '[0]]', '[' * 129 + '0' + ']' * 129]
    with tempfile.TemporaryDirectory(prefix="toka-document-") as directory:
        work = Path(directory)

        def compile(source, *flags):
            source_root = ROOT if source.is_relative_to(ROOT) else work
            cmd = [str(compiler), '--workspace-node', 'json-document-tests',
                   '--workspace-root', str(source_root), str(source), *map(str, flags)]
            started = time.perf_counter()
            try:
                proc = subprocess.run(cmd, cwd=ROOT,
                                      env=env, text=True, capture_output=True, timeout=300)
                elapsed = time.perf_counter() - started
                return proc, elapsed
            except subprocess.TimeoutExpired:
                elapsed = time.perf_counter() - started
                print(f"[TIMEOUT after {elapsed:.2f}s]: {' '.join(cmd)}", flush=True)
                raise

        def qualify(source, diagnostic=None):
            normal, t_normal = compile(source, '--check-only')
            shadow, t_shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == (1 if diagnostic else 0), (normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            json.loads(shadow.stdout)
            output = work / source.stem
            built, t_build = compile(source, '-o', output)
            if diagnostic:
                assert built.returncode == 1 and diagnostic in built.stderr and not output.exists(), built.stderr
                for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                    path = work / (source.stem + suffix)
                    failed, _ = compile(source, flag, '-o', path)
                    assert failed.returncode == 1 and not path.exists(), failed.stderr
            else:
                assert built.returncode == 0, built.stderr
                t_run_start = time.perf_counter()
                ran = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
                t_run = time.perf_counter() - t_run_start
                assert ran.returncode == 0, (source.name, ran.returncode, ran.stderr)
                print(f'PASS {source.name} (check: {t_normal:.2f}s, shadow: {t_shadow:.2f}s, build: {t_build:.2f}s, run: {t_run:.2f}s)', flush=True)
                return
            print('PASS ' + source.name, flush=True)

        lines = ['import stdx/serde/json::{parse_document, to_json}', 'fn main() -> i32 {']
        for index, source in enumerate(valid):
            lines += ['{', f'auto result = parse_document({json.dumps(source, ensure_ascii=False)})',
                      f'if result.is_err() {{ return {index + 1} }}', 'auto document = result.unwrap()']
            value = json.loads(source)
            # Deep depth case verifies acceptance separately, without generating a huge reader body.
            def verify(value, node, tag):
                kind = 0 if value is None else 1 if isinstance(value, bool) else 2 if isinstance(value, (int, float)) else 3 if isinstance(value, str) else 4 if isinstance(value, list) else 5
                lines.append(f'if document.kind({node}) != {kind} {{ return {index + 1} }}')
                if kind == 1:
                    lines.append(f'if document.boolean({node}).unwrap() != {str(value).lower()} {{ return {index + 1} }}')
                elif kind == 2:
                    expected = float(value)
                    tolerance = max(1e-10, abs(expected) * 1e-12)
                    lines.extend([f'auto delta_{tag} = document.number({node}).unwrap() - ({expected!r})',
                                  f'if delta_{tag} != delta_{tag} {{ return {index + 1} }}',
                                  f'if delta_{tag} > {tolerance!r} || delta_{tag} < -{tolerance!r} {{ return {index + 1} }}'])
                elif kind == 3:
                    # Compare decoded UTF-8 bytes, including embedded NUL/control bytes.
                    data = value.encode('utf-8')
                    lines.append(f'if document.text({node}).len() != {len(data)} {{ return {index + 1} }}')
                    for pos, byte in enumerate(data):
                        lines.append(f'if (document.text({node}).byte_at({pos}) as u8 != {byte}:u8) {{ return {index + 1} }}')
                elif kind == 4:
                    cursor = f'cursor_{tag}_0'
                    lines.append(f'auto {cursor} = document.first({node})')
                    for child_index, child in enumerate(value):
                        verify(child, cursor, tag + '_' + str(child_index))
                        following = f'cursor_{tag}_{child_index + 1}'
                        lines.append(f'auto {following} = document.next({cursor})')
                        cursor = following
                    lines.append(f'if {cursor} != 0 {{ return {index + 1} }}')
                elif kind == 5:
                    for child_index, (key, child) in enumerate(value.items()):
                        child_var = f'node_{tag}_{child_index}'
                        lines.append(f'auto {child_var} = document.find({node}, {json.dumps(key, ensure_ascii=False)})')
                        verify(child, child_var, tag + '_' + str(child_index))
            if index != len(valid) - 1:
                verify(value, '1', str(index))
            lines += ['auto serialized = to_json(document)', 'auto roundtrip = parse_document(serialized.as_str())',
                      f'if roundtrip.is_err() {{ return {index + 1} }}', '{', 'auto document = roundtrip.unwrap()']
            if index != len(valid) - 1:
                verify(value, '1', str(index) + '_roundtrip')
            lines += ['}', '}']
        for source in invalid:
            lines += ['{', f'auto result = parse_document({json.dumps(source)})', 'if result.is_ok() { return 90 }', '}']
        for data in ([192, 175], [237, 160, 128], [244, 144, 128, 128], [240, 159]):
            lines += ['{', 'auto input# = string::from("\\\"")']
            lines += [f'input#.push_byte_raw({byte}:u8)' for byte in data]
            lines += ['input#.push_str("\\\"")', 'auto result = parse_document(input.as_str())', 'if result.is_ok() { return 91 }', '}']
        lines += ['return 0', '}']
        matrix = work / 'parser_matrix.tk'
        matrix.write_text('\n'.join(lines) + '\n')
        qualify(matrix)
        qualify(ROOT / 'tests/semantics/json_flat_document/parser.tk')
        qualify(ROOT / 'tests/semantics/json_flat_document/cleanup.tk')
        independent = work / 'independent_input.tk'
        independent.write_text('''import stdx/serde/json_document::{Document, parse_document}
fn make() -> Document {
    auto input = string::from("[\\\"owned\\\"]")
    auto result = parse_document(input.as_str())
    auto document = result.unwrap()
    return cede document
}
fn main() -> i32 {
    auto document = make()
    if document.text(document.first(1)) != "owned" { return 1 }
    return 0
}
''')
        qualify(independent)
        traversal = work / 'reader_loop.tk'
        traversal.write_text('''import stdx/serde/json_document::{parse_document}
fn main() -> i32 {
    auto result = parse_document("[1,2,3]")
    auto document = result.unwrap()
    auto current# = document.first(1)
    auto total# = 0.0:f64
    loop current != 0 {
        total += document.number(current).unwrap()
        current = document.next(current)
    }
    if total != 6.0 { return 1 }
    return 0
}
''')
        qualify(traversal)
        # This container remains outside the proved borrowed-element domain;
        # do not mistake its rejection for a completed generic lifetime proof.
        qualify(ROOT / 'tests/semantics/json_flat_document/container_borrow_escape.tk', 'E04662')
        for name, body in {
            'view_escape': 'auto result = parse_document("\\\"owned\\\"")\nauto document = result.unwrap()\nreturn document.text(1)',
            'descriptor_escape': 'auto result = parse_document("\\\"owned\\\"")\nauto document = result.unwrap()\nauto view = document.text(1)\nreturn &view',
        }.items():
            result_type = '&str' if name == 'descriptor_escape' else 'str'
            source = work / (name + '.tk')
            source.write_text(f'import stdx/serde/json_document::{{parse_document}}\nfn escaped() -> {result_type} {{\n{body}\n}}\nfn main() -> i32 {{ return 0 }}\n')
            qualify(source, 'E0455')
    print(f'Document parser: {len(valid)} valid, {len(invalid) + 4} invalid inputs; lifecycle and escape gates passed.')


if __name__ == '__main__':
    main()
