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


OWNER = '''import core/traits::{@Encap}
auto drops# = 0:i32
shape View(&first: i32, &second: i32)
impl View@Encap { pub first, second fn drop(self#) { drops += 1 } }
'''
for route, hat, handed in (('shared_copy', '~', '~owner'),
                           ('shared_move', '~', 'cede ~owner'),
                           ('unique_move', '^', 'cede ^owner')):
    returned = 'cede ~other' if hat == '~' else '^other'
    CASES[route + '_escape'] = (OWNER + f'''
fn escape(input: i32) -> {hat}View <- input {{
    auto local = 7:i32
    auto {hat}owner = new View(&first = &input, &second = &local)
    auto {hat}other = {handed}
    return {returned}
}}
fn main() -> i32 {{
    auto input = 13:i32
    auto {hat}escaped = escape(input)
    return escaped.second
}}
''', 'E0455')
    CASES[route + '_reverse_escape'] = (
        CASES[route + '_escape'][0].replace('&first = &input, &second = &local',
                                           '&first = &local, &second = &input'), 'E0455')
    CASES[route + '_live'] = (OWNER + f'''
fn main() -> i32 {{
    auto first = 13:i32
    auto second = 21:i32
    {{
        auto {hat}owner = new View(&first = &first, &second = &second)
        {{
            auto {hat}other = {handed}
            auto {hat}last = {'~other' if route == 'shared_copy' else 'cede ' + hat + 'other'}
            assert(last.first == 13 && last.second == 21, "two actual referents")
            assert(drops == 0, "owner alive")
        }}
        {'assert(owner.first == 13 && drops == 0, "remaining shared owner");' if route == 'shared_copy' else 'assert(drops == 1, "transferred owner cleaned");'}
    }}
    assert(drops == 1, "exactly one payload drop")
    return 0
}}
''', None)
    CASES[route + '_parameter_return'] = (OWNER + f'''
fn keep(input: i32) -> {hat}View <- input {{
    auto {hat}owner = new View(&first = &input, &second = &input)
    auto {hat}other = {handed}
    return {returned}
}}
fn main() -> i32 {{
    auto input = 13:i32
    {{ auto {hat}result = keep(input); assert(result.second == 13, "live external storage") }}
    assert(drops == 1, "returned payload cleaned once")
    return 0
}}
''', None)
    CASES[route + '_assignment_escape'] = (OWNER + f'''
fn escape(input: i32) -> {hat}View <- input {{
    auto local = 7:i32
    auto {hat}#other = new View(&first = &input, &second = &input)
    auto {hat}owner = new View(&first = &local, &second = &local)
    {hat}#other = {handed}
    return {returned}
}}
fn main() -> i32 {{ return 0 }}
''', 'E0455')
    CASES[route + '_assignment_live'] = (OWNER + f'''
fn main() -> i32 {{
    auto first = 13:i32
    auto second = 21:i32
    {{
        auto {hat}#other = new View(&first = &first, &second = &first)
        {{
            auto {hat}owner = new View(&first = &second, &second = &second)
            {hat}#other = {handed}
            assert(drops == 1 && other.second == 21, "old target released after preparation")
        }}
        assert(drops == 1 && other.first == 21, "new target survives source scope")
    }}
    assert(drops == 2, "both payloads cleaned once")
    return 0
}}
''', None)
    CASES[route + '_assignment_inner_escape'] = (OWNER + f'''
fn reject(input: i32) -> {hat}View <- input {{
    auto {hat}#other = new View(&first = &input, &second = &input)
    {{
        auto local = 7:i32
        auto {hat}owner = new View(&first = &local, &second = &local)
        {hat}#other = {handed}
        assert(owner.first == 7, "rejection restores source")
    }}
    return {returned}
}}
fn main() -> i32 {{ return 0 }}
''', 'E0456')
    CASES[route + '_pal_retention'] = (OWNER + f'''
fn main() -> i32 {{
    auto first = 13:i32
    auto second# = 21:i32
    auto {hat}#other = new View(&first = &first, &second = &first)
    {{
        auto {hat}owner = new View(&first = &second, &second = &second)
        {hat}#other = {handed}
    }}
    auto &exclusive# = &second
    exclusive = 22
    return other.second
}}
''', 'E0441')
    CASES[route + '_branch_escape'] = (OWNER + f'''
fn escape(input: i32, flag: bool) -> {hat}View <- input {{
    auto local = 7:i32
    auto {hat}#other = new View(&first = &input, &second = &input)
    if flag {{
        auto {hat}owner = new View(&first = &input, &second = &local)
        {hat}#other = {handed}
    }}
    return {returned}
}}
fn main() -> i32 {{ return 0 }}
''', 'E0455')
    CASES[route + '_loop_escape'] = (
        CASES[route + '_branch_escape'][0].replace('if flag {', 'loop flag {').replace(
            f'{hat}#other = {handed}', f'{hat}#other = {handed}\n        break'), 'E0455')
    CASES[route + '_shadow_binding'] = (
        CASES[route + '_live'][0].replace(f'auto {hat}other = {handed}',
                                         f'auto {hat}owner = {handed}').replace(
            'auto ' + hat + 'last = ' + ('~other' if route == 'shared_copy' else 'cede ' + hat + 'other'),
            'auto ' + hat + 'last = ' + ('~owner' if route == 'shared_copy' else 'cede ' + hat + 'owner')),
        None)
    for wrapping, expression in (
        ('unsafe', f'unsafe ({handed})'),
        ('ascription', f'(unsafe ({handed})):{hat}View')):
        for control in ('escape', 'live', 'assignment_escape', 'assignment_live'):
            original, error = CASES[route + '_' + control]
            assert original.count(f'= {handed}\n') == 1, (route, control)
            CASES[route + '_' + wrapping + '_' + control] = (
                original.replace(f'= {handed}\n', f'= {expression}\n'), error)


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
            if name == 'rejected_initializer_rollback' or name.endswith('_assignment_inner_escape'):
                assert all(f'error[{code}]' not in normal.stderr for code in ('E0438','E0410')), normal.stderr
                if name.endswith('_assignment_inner_escape'):
                    assert 'error[E0455]' not in normal.stderr, normal.stderr
                    evidence = json.loads(shadow.stdout)
                    returned = [x for x in evidence['records'] if x['boundary'] == 'return' and
                                x['location']['file'].endswith(name + '.tk') and
                                'View' in x['plan']['actual_type']]
                    assert returned and all(
                        any('binding:input;' in dep for dep in x['plan']['dependency_roots']) and
                        all('binding:local;' not in dep for dep in x['plan']['dependency_roots'])
                        for x in returned), returned
            if name.startswith(('shared_copy_', 'shared_move_', 'unique_move_')) and \
                    name.endswith(('_escape', '_parameter_return')) and \
                    'assignment_inner' not in name:
                evidence = json.loads(shadow.stdout)
                returns = [x for x in evidence['records'] if x['boundary'] == 'return' and
                           x['location']['file'].endswith(name + '.tk') and
                           'View' in x['plan']['actual_type']]
                assert returns, name
                expected = 'local' if error else 'input'
                assert all(x['dependency'] != 'None' and
                           any('binding:' + expected + ';' in dep for dep in x['plan']['dependency_roots'])
                           for x in returns), returns
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
