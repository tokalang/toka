#!/usr/bin/env python3
"""Deque behavior and ownership, using the public RingCore API only."""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

SHARED_OWNER = '''import std/ring::{RingCore}
auto drops# = 0:i32
shape Cell(id:i32)
impl Cell@Encap { pub id
fn drop(self#) { drops += 1 } }
fn main() -> i32 {
    {
        auto ~owner = new Cell(id=77)
        {
            auto q# = RingCore<~Cell>::new()
            auto i# = 0:i32
            loop i < 40 {
                auto ~retained = ~owner
                if i % 2 == 0 { q#.push_front(cede ~retained) }
                else { q#.push_back(cede ~retained) }
                i += 1
            }
            q#.clear()
            if drops != 0 || owner.id != 77 { return 1 }
            auto ~retained = ~owner
            q#.push_back(cede ~retained)
            q#.pop_front()
            if drops != 0 || owner.id != 77 { return 2 }
        }
        if drops != 0 || owner.id != 77 { return 3 }
    }
    if drops != 1 { return 4 }
    return 0
}
'''

PLAIN = '''import std/ring::{RingCore}
shape Plain(id:i32)
impl Plain@Encap { pub id }
fn main() -> i32 {
    auto q# = RingCore<Plain>::new()
    auto i# = 0:i32
    loop i < 33 { q#.push_back(Plain(id=i)); i += 1 }
    i = 0
    loop i < 33 {
        auto item = q#.pop_front().unwrap()
        if item.id != i { return 1 }
        i += 1
    }
    if !q#.pop_back().is_none() { return 2 }
    return 0
}
'''

STRING = '''import std/ring::{RingCore}
fn exercise() -> i32 {
    auto q# = RingCore<string>::new()
    auto i# = 0:i32
    loop i < 40 { q#.push_back(i.to_string()); i += 1 }
    i = 0
    loop i < 24 {
        auto value = q#.pop_front().unwrap()
        if value != i.to_string() { return 1 }
        i += 1
    }
    q#.clear()
    q#.push_front(string::from("left"))
    q#.push_back(string::from("right"))
    return 0
}
fn main() -> i32 {
    auto i# = 0:i32
    loop i < 50 {
        if exercise() != 0 { return 1 }
        i += 1
    }
    return 0
}
'''

DUP = '''import std/ring::{RingCore}
import core/traits::{@Dup}
auto drops# = 0:i32
auto copies# = 0:i32
shape Cell(id:i32)
impl Cell@Encap { pub id
fn drop(self#) { drops += 1 } }
impl Cell@Dup {
pub fn dup(self) -> Self { copies += 1; return Cell(id=self.id) }
}
fn main() -> i32 {
    {
        auto q# = RingCore<Cell>::new()
        q#.push_front(Cell(id=1))
        q#.push_back(Cell(id=2))
        { auto a = q.get(0).unwrap(); if a.id != 1 { return 1 } }
        { auto b = q.get(1).unwrap(); if b.id != 2 { return 2 } }
        if copies != 2 || drops != 2 || q.len() != 2 { return 3 }
        if !q.get(2).is_none() || copies != 2 { return 4 }
    }
    if drops != 4 { return 5 }
    return 0
}
'''

def scalar_case():
    rng = random.Random(914)
    model = deque()
    lines = ['import std/ring::{RingCore}', 'fn main() -> i32 {',
             'auto q# = RingCore<i32>::new()']
    for i in range(400):
        op = rng.choice(['push_front', 'push_back', 'pop_front', 'pop_back'])
        if op.startswith('push'):
            value = i - 200
            (model.appendleft if op == 'push_front' else model.append)(value)
            lines.append(f'q#.{op}({value}:i32)')
        elif model:
            value = (model.popleft if op == 'pop_front' else model.pop)()
            lines.append(f'if q#.{op}().unwrap() != {value}:i32 {{ return 11 }}')
        else:
            lines.append(f'if !q#.{op}().is_none() {{ return 12 }}')
        lines.append(f'if q.len() != {len(model)}:usize {{ return 900 }}')
        if model:
            index = rng.randrange(len(model))
            lines.append(f'if q.get({index}:usize).unwrap() != {model[index]}:i32 {{ return 901 }}')
    lines += ['q#.clear()', 'if !q.is_empty() { return 902 }', 'return 0', '}']
    return '\n'.join(lines)


def resource_case(morph=''):
    # Push enough for repeated buffer growth; repeatedly refill both ends.
    element = morph + 'Cell'
    lines = ['import std/ring::{RingCore}', 'auto drops# = 0:i32',
             'shape Cell(id:i32)', 'impl Cell@Encap { pub id',
             'fn drop(self#) { drops += 1 } }',
             'fn exercise() -> i32 {', f'auto q# = RingCore<{element}>::new()']
    for i in range(40):
        if morph:
            lines += [f'auto {morph}v{i} = new Cell(id={i})',
                      f'q#.push_back(cede {morph}v{i})']
        else:
            lines.append(f'q#.push_back(Cell(id={i}))')
    lines.append('if drops != 0 { return 1 }')
    model = deque(range(40))
    for i in range(24):
        side = 'front' if i % 2 == 0 else 'back'
        expected = (model.popleft if side == 'front' else model.pop)()
        lines += ['{', f'auto {morph}value = q#.pop_{side}().unwrap()',
                  f'if value.id != {expected} || drops != {i} {{ return 2 }}', '}',
                  f'if drops != {i+1} {{ return 3 }}']
    lines += ['q#.clear()', 'if drops != 40 || !q.is_empty() { return 4 }']
    if morph:
        lines += [f'auto {morph}last = new Cell(id=40)', f'q#.push_front(cede {morph}last)']
    else:
        lines.append('q#.push_front(Cell(id=40))')
    lines += ['return 0', '}', 'fn main() -> i32 {', 'auto code = exercise()',
              'if code != 0 { return code }', 'if drops != 41 { return 5 }', 'return 0', '}']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-ring-library-') as temporary:
        work = args.keep_dir or Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        cases = {'scalar_model': scalar_case(), 'resource': resource_case(),
                 'unique': resource_case('^'), 'shared': resource_case('~'),
                 'shared_owner': SHARED_OWNER, 'plain_noncopy': PLAIN,
                 'string_pressure': STRING, 'explicit_dup': DUP}
        for name, text in cases.items():
            source = work / (name + '.tk')
            source.write_text(text)
            prefix = [str(compiler), '--workspace-node', 'ring-library',
                      '--workspace-root', str(work), str(source)]
            def run(*flags):
                return subprocess.run(prefix + list(map(str, flags)), cwd=ROOT,
                                      env=env, text=True, capture_output=True, timeout=90)
            normal = run('--check-only')
            shadow = run('--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            json.loads(shadow.stdout)
            binary = work / name
            built = run('-o', binary)
            assert built.returncode == 0, (name, built.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            assert executed.returncode == 0, (name, executed.returncode, executed.stderr)
            print('PASS ' + name, flush=True)
        negatives = {
            'non_dup': ((ROOT / 'tests/fail/ring_get_non_dup_resource.tk').read_text(), 'E0417'),
            'moved_source': ('''import std/ring::{RingCore}
shape Cell(id:i32)
impl Cell@Encap { pub id
fn drop(self#) {} }
fn main() -> i32 {
    auto q# = RingCore<Cell>::new()
    auto source = Cell(id=1)
    q#.push_back(cede source)
    return source.id
}
''', 'E0438'),
        }
        for name, (text, code) in negatives.items():
            source = work / (name + '.tk')
            source.write_text(text)
            prefix = [str(compiler), '--workspace-node', 'ring-library',
                      '--workspace-root', str(work), str(source)]
            normal = run('--check-only')
            shadow = run('--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, name
            assert f'error[{code}]' in normal.stderr, (name, normal.stderr)
            for flag, suffix in [('-c', '.o'), ('--emit-llvm', '.ll')]:
                artifact = work / (name + suffix)
                failed = run(flag, '-o', artifact)
                assert failed.returncode == 1 and not artifact.exists(), (name, failed.stderr)
            print('PASS rejection ' + name, flush=True)


if __name__ == '__main__':
    main()
