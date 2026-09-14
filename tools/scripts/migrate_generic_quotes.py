#!/usr/bin/env python3
"""Mechanical G spelling migration. Does not infer hats or change contracts.

Only the two legacy lexer forms ('identifier and '(expression)) are removed.
Comments, ordinary/C strings, raw strings and character literals are retained.
Subsequent semantic failures require a separate, purpose-preserving migration.
"""
import argparse
from pathlib import Path
import re


def migrate(text):
    output = []
    index = 0
    removed = 0
    while index < len(text):
        start = index
        if text.startswith('//', index):
            end = text.find('\n', index)
            index = len(text) if end < 0 else end
        elif text.startswith('/*', index):
            end = text.find('*/', index + 2)
            index = len(text) if end < 0 else end + 2
        elif text[index] == '"':
            count = 1
            while index + count < len(text) and text[index + count] == '"': count += 1
            if count >= 3:
                after = index + count
                while after < len(text) and text[after] in ' \t': after += 1
                if text[after:after + 1] in ('\n', '\r'):
                    closing = re.search(r'(?m)^[ \t]*("{' + str(count) + r'})[ \t]*(?=\r?$)', text[after:])
                    index = len(text) if not closing else after + closing.end(1)
                else:
                    index += count
                    while index < len(text):
                        if text[index] != '"':
                            index += 1
                            continue
                        end = index
                        while end < len(text) and text[end] == '"': end += 1
                        run = end - index
                        index = end
                        if run == count: break
            else:
                index += 1
                while index < len(text):
                    if text[index] == '\\': index += 2
                    elif text[index] == '"':
                        index += 1
                        break
                    else: index += 1
        elif text[index] == "'":
            following = text[index + 1:index + 2]
            end = index + 1
            if following and (following.isascii() and (following.isalpha() or following == '_')):
                while end < len(text) and text[end].isascii() and (text[end].isalnum() or text[end] == '_'): end += 1
            marker = (following == '(' and text[index + 2:index + 3] != "'") or (
                end > index + 1 and text[end:end + 1] != "'")
            if marker:
                removed += 1
                index += 1
                continue
            index += 1
            while index < len(text):
                if text[index] == '\\': index += 2
                elif text[index] == "'":
                    index += 1
                    break
                else: index += 1
        else:
            index += 1
        output.append(text[start:index])
    return ''.join(output), removed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('paths', nargs='*', type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        changed = "fn f<'T>(cede 'x:T)->'T{return cede '(x)}"
        assert migrate(changed) == ("fn f<T>(cede x:T)->T{return cede (x)}", 4)
        for protected in (
            "auto a='a'\nauto b='(' \nauto c='\\''\nauto d='\\x41'",
            '// fn f<\'T>\n/* auto \'x */',
            'auto text = "fn f<\'T>"',
            'auto text = c"auto \'x"',
            'auto text = """ \'T \'(x) """',
            'auto text = """\n  \'T \'(x)\n  """',
            'auto text = """ \'T """" \'x """',
        ):
            assert migrate(protected) == (protected, 0), protected
        print('scanner: marker migration and 7 protected-text controls passed')
    files = set()
    for path in args.paths:
        files.update(path.rglob('*.tk') if path.is_dir() else [path])
    changed = count = 0
    for path in sorted(files):
        with path.open(newline='') as source: before = source.read()
        after, markers = migrate(before)
        if markers:
            print(f'{path}: {markers}')
            changed += 1
            count += markers
            if args.apply:
                with path.open('w', newline='') as destination: destination.write(after)
    print(f'{changed} files; {count} legacy markers; apply={args.apply}')


if __name__ == '__main__':
    main()
