#!/usr/bin/env python3
"""Concrete replay boundary regressions; preserve intended rejection sites."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = '''import core/traits::{@Encap}
shape Token(id: i32, &drops#: i32)
impl Token@Encap { pub id, drops fn drop(self#) { self.drops += 1 } }
'''
CASES = {
    'shared_borrowed': (PREFIX + '''
fn main() -> i32 {
    auto drops# = 0:i32
    {
        auto ~owner# = new Token(id = 7, &drops = &drops)
        { auto ~other = ~owner; assert(other.id == 7, "copy live") }
        assert(drops == 0, "shared owner remains")
    }
    assert(drops == 1, "one final cleanup")
    return 0
}
''', None),
    'unique_borrowed': (PREFIX + '''
fn main() -> i32 {
    auto drops# = 0:i32
    { auto ^owner = new Token(id = 7, &drops = &drops); assert(owner.id == 7, "unique live") }
    assert(drops == 1, "one cleanup")
    return 0
}
''', None),
    'local_escape': (PREFIX + '''
fn escape() -> ~Token {
    auto drops# = 0:i32
    auto ~owner = new Token(id = 7, &drops = &drops)
    return cede ~owner
}
fn main() -> i32 { return 0 }
''', 'E0455'),
    'readonly': (PREFIX + '''
fn main() -> i32 {
    auto drops = 0:i32
    auto ~owner = new Token(id = 7, &drops = &drops)
    return 0
}
''', 'E04573'),
}


CASES['local_escape_unique'] = (CASES['local_escape'][0].replace('~', '^'), 'E0455')
CASES['parameter_origin'] = (PREFIX + '''
fn make(drops#: i32) -> ~Token <- drops {
    auto ~owner = new Token(id = 7, &drops = &drops)
    return cede ~owner
}
fn main() -> i32 { return 0 }
''', None)
CASES['shadowed_origin'] = (PREFIX + '''
fn escape(drops#: i32) -> ~Token <- drops {
    {
        auto drops# = 0:i32
        auto ~owner = new Token(id = 7, &drops = &drops)
        return cede ~owner
    }
}
fn main() -> i32 { return 0 }
''', 'E0455')
CASES['readonly_parameter'] = (PREFIX + '''
fn reject(drops: i32) {
    auto ~owner = new Token(id = 7, &drops = &drops)
}
fn main() -> i32 { return 0 }
''', 'E04573')
CASES['borrow_conflict'] = (PREFIX + '''
fn main() -> i32 {
    auto drops# = 0:i32
    auto &loan# = &drops
    auto ~owner = new Token(id = 7, &drops = &drops)
    loan = 1
    return 0
}
''', 'E0441')
CASES['rejected_initializer_rollback'] = ('''import core/traits::{@Encap}
shape Owned(id: i32)
impl Owned@Encap { pub id fn drop(self#) {} }
shape Holder(owned: Owned, &count#: i32)
fn main() -> i32 {
    auto item = Owned(id = 7)
    auto count = 0:i32
    auto ~rejected = new Holder(owned = cede item, &count = &count)
    return item.id
}
''', 'E04573')


CASES['owned_iterator_cleanup'] = ('''import core/traits::{@Encap}
import std/vec::{Vec}
auto drops# = 0:i32
shape Item(id: i32)
impl Item@Encap { pub id fn drop(self#) { drops += 1 } }
fn main() -> i32 {
    {
        auto values# = Vec<Item>::new()
        values#.push(Item(id = 1))
        values#.push(Item(id = 2))
        values#.push(Item(id = 3))
        auto iterator# = values.into_iter()
        {
            auto first = iterator#.next().unwrap()
            assert(first.id == 1, "owned iterator order")
        }
        assert(drops == 1, "yield owns one item")
    }
    assert(drops == 3, "iterator cleans remaining items once")
    auto empty# = Vec<Item>::new().into_iter()
    assert(empty#.next().is_none(), "empty iterator")
    return 0
}
''', None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve()/'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT/'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-replay-boundaries-') as temporary:
        work = Path(temporary)
        for name, (text, error) in CASES.items():
            source = work/(name+'.tk')
            source.write_text(text)
            def compile(*flags):
                result = subprocess.run([str(compiler), str(source), *map(str, flags)], cwd=ROOT,
                                        env=env, capture_output=True, text=True, timeout=90)
                assert result.returncode == (1 if error else 0), (name,result.stderr)
                if error: assert f'error[{error}]' in result.stderr, (name,result.stderr)
                return result
            normal = compile('--check-only')
            shadow = compile('--check-only','--non-call-transfer-shadow=json')
            assert normal.stderr == shadow.stderr, name
            if name == 'rejected_initializer_rollback':
                assert all(f'error[{code}]' not in normal.stderr for code in ('E0438','E0410')), normal.stderr
            if name in ('shared_borrowed','unique_borrowed','parameter_origin'):
                evidence = json.loads(shadow.stdout)
                new_records = [x for x in evidence['records']
                               if x['location']['file'].endswith(name+'.tk') and
                               x['boundary']=='initialization' and
                               x['plan']['dependency_roots']]
                assert any(x['dependency']=='Borrowed' and x['plan']['outcome']=='Admitted' and
                           x['plan']['referent_path'] and
                           any('drops' in str(dep) for dep in x['plan']['dependency_roots'])
                           for x in new_records), new_records
            for mode, suffix in (('-c','.o'),('--emit-llvm','.ll')):
                output=work/(name+suffix)
                compile(mode,'-o',output)
                assert output.exists()==(error is None), output
            if not error:
                output=work/name
                compile('-o',output)
                run=subprocess.run([str(output)],cwd=work,capture_output=True,timeout=30)
                assert run.returncode==0,(name,run.returncode,run.stderr)
            print('PASS',name,flush=True)

        provider = work/'provider.tk'
        provider.write_text('pub fn __toka_replay_collision(value: i32) -> fn(i32)->i32 { return { x => x + 1 }:fn(i32)->i32 }\n')
        emitted = subprocess.run([str(compiler),'-c','--emit-interface',str(provider),'-o',str(work/'provider.o')],
                                 cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
        assert emitted.returncode == 0, emitted.stderr
        provider.rename(work/'provider.hidden')
        source = work/'collision.tk'
        source.write_text('''import ./provider as peer
fn __toka_replay_collision(value: i32) -> i32 { return value }
fn main() -> i32 {
    auto callback = peer::__toka_replay_collision(1)
    return callback(4)
}
''')
        checked = subprocess.run([str(compiler),'--check-only',str(source)],cwd=ROOT,env=env,
                                 capture_output=True,text=True,timeout=90)
        assert checked.returncode == 0, checked.stderr
        for mode, suffix in (('-c','.o'),('--emit-llvm','.ll')):
            output = work/('collision'+suffix)
            rejected = subprocess.run([str(compiler),mode,str(source),'-o',str(output)],cwd=ROOT,env=env,
                                      capture_output=True,text=True,timeout=90)
            assert rejected.returncode == 1 and 'executable interface symbol identity collision' in rejected.stderr, rejected.stderr
            assert not output.exists(), output
        print('PASS symbol collision stops before emission, without signal exit',flush=True)


if __name__=='__main__': main()
