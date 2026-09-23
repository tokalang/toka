#!/usr/bin/env python3
"""Rechecked interface bodies must be the bodies that actually execute."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-local-interface-') as temporary:
        work = Path(temporary)
        extra_objects = []

        def run(command, expected=0):
            result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                    text=True, capture_output=True, timeout=90)
            assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
            return result

        def provider(source):
            (work/'lib.tk').write_text(source)
            run([tokac, '-c', '--emit-interface', 'lib.tk', '-o', 'lib.o'])
            interface = (work/'lib.tki').read_text()
            assert 'local_body_policy: checked-local-v1' in interface, interface
            (work/'lib.tk').unlink()
            return interface

        def check(source, expected=0, diagnostic=None, result=0):
            (work/'main.tk').write_text(source)
            normal = run([tokac, '--check-only', 'main.tk'], expected)
            shadow = run([tokac, '--check-only', '--non-call-transfer-shadow=json', 'main.tk'], expected)
            assert normal.stderr == shadow.stderr, (normal.stderr, shadow.stderr)
            for flags, name in [(['-c'], 'main.o'), (['--emit-llvm'], 'main.ll')]:
                output = work/name
                output.unlink(missing_ok=True)
                compile_result = run([tokac, *flags, 'main.tk', '-o', name], expected)
                if diagnostic:
                    assert diagnostic in compile_result.stderr, compile_result.stderr
                assert output.exists() == (expected == 0)
            if diagnostic:
                assert diagnostic in normal.stderr, normal.stderr
            if expected == 0:
                run([tokac, 'main.tk', 'lib.o', *extra_objects, '-o', 'main'])
                run([work/'main'], result)

        factory = '''fn increment(value: i32) -> i32 { return value + 1 }
pub fn make() -> fn(i32) -> i32 {
    return { value => increment(value) }:fn(i32) -> i32
}
'''
        consumer = 'import ./lib::{make}\nfn main() -> i32 { auto callback = make(); return callback(4) }\n'
        original = provider(factory)
        check(consumer, result=5)
        assert re.search(r'define internal .*@.*make', (work/'main.ll').read_text())
        assert 'value + 1' in original
        changed = original.replace('value + 1', 'value + 10')
        (work/'lib.tki').write_text(changed)
        check(consumer, result=14)  # old provider object still implements +1
        run([tokac, 'main.tk', '-o', 'without-provider'])
        run([work/'without-provider'], 14)
        print('PASS checked helper/factory executes despite old provider', flush=True)

        (work/'lib.tki').write_text(re.sub(r'^// @meta local_body_(policy|definitions):.*\n', '', original, flags=re.M))
        check(consumer, 1, 'E04661')
        (work/'lib.tki').write_text(original.replace('checked-local-v1', 'checked-local-unsupported'))
        check(consumer, 1, 'E0901')
        (work/'lib.tki').write_text(original.replace('0.9.9-23', '0.9.9-22'))
        check(consumer, 1, 'E0901')
        (work/'lib.tki').write_text(re.sub(r'local_body_definitions: .*', 'local_body_definitions: f/999', original))
        check(consumer, 1, 'Invalid executable interface definition')
        # Do not accept a declaration in place of the helper used for checking.
        missing = re.sub(r'(fn increment\([^\n]+) \{[^}]+\}', r'\1', original)
        assert missing != original
        (work/'lib.tki').write_text(missing)
        check(consumer, 1, 'Invalid executable interface definition')
        # Valid root association cannot cover its now bodyless helper.
        (work/'lib.tki').write_text(re.sub(r'local_body_definitions: .*', 'local_body_definitions: f/1', missing))
        check(consumer, 1, 'executable interface dependency')
        (work/'lib.tki').write_text(original.replace('value + 1', 'increment(value)'))
        check(consumer, 1, 'executable interface dependency')
        (work/'lib.tki').write_text(original.replace('value + 1', 'true'))
        check(consumer, 1, 'E0408')
        (work/'lib.tki').write_text(original)
        check('import ./lib::{make}\nfn main() -> i32 { auto identity = make; return 0 }\n',
              1, 'function identity is not remapped')
        print('PASS policy/helper/identity fail-closed', flush=True)

        (work/'counter.c').write_text('static int drops; void record_drop(int n) { drops += n; } int drop_count(void) { return drops; }\n')
        run(['cc', '-c', 'counter.c', '-o', 'counter.o'])
        extra_objects.append('counter.o')
        drop_factory = '''import core/traits::{@Encap}
extern fn record_drop(value: i32) -> void
shape Token(id: i32)
impl Token@Encap { pub id fn drop(self#) { record_drop(1) } }
pub fn make() -> dyn fn() -> i32 {
    auto token = Token(id = 7)
    return { [cede token] => token.id }:dyn fn() -> i32
}
'''
        original = provider(drop_factory)
        check('''import ./lib::{make}
extern fn drop_count() -> i32
fn main() -> i32 {
    {
        auto callback = make()
        auto copied = callback
        assert(callback() == 7 && copied() == 7, "shared capture live")
    }
    return drop_count()
}
''', result=1)
        assert 'record_drop(1)' in original
        (work/'lib.tki').write_text(original.replace('record_drop(1)', 'record_drop(2)'))
        check((work/'main.tk').read_text(), result=2)
        print('PASS exact capture cleanup uses checked drop body', flush=True)

        # Current actual arguments, not the dependency ceiling or task frame,
        # select the returned view. The first owner may be cleaned up first.
        original = provider('pub fn second(first: str, value: str) -> async str <- value { return value }\n')
        prefix = '''import ./lib::{second}
import std/task::{block_on}
fn wait_value<T>(task: TaskHandle<T>) -> T <- task {
    auto result = task.wait:T
    return cede result
}
'''
        check(prefix + '''
fn relay(input: str) -> str <- input {
    auto unrelated = string::from("unrelated")
    auto task = second(unrelated.as_view(), input)
    return wait_value(task)
}
fn main() -> i32 {
    auto a = string::from("first")
    auto b = string::from("second")
    auto x = relay(a.as_view())
    auto y = relay(b.as_view())
    assert(x.equals("first") && y.equals("second"), "actual source survives task cleanup")
    return 0
}
''')
        check(prefix + '''
fn escape(input: str) -> str <- input {
    auto good = second("unused", input)
    auto first = wait_value(good)
    auto owner = string::from("local")
    auto bad = second(input, owner.as_view())
    return wait_value(bad)
}
fn main() -> i32 { return 0 }
''', 1, 'E0455')
        (work/'lib.tki').write_text(original.replace('return value', 'auto owner = string::from("frame"); return owner.as_view()'))
        check(prefix + 'fn main() -> i32 { return 0 }\n', 1, 'E0455')
        print('PASS task external-source/cache mapping and frame escape refusal', flush=True)

        provider('''auto counter# = 0:i32
pub fn make() -> fn(i32) -> i32 {
    counter += 1
    return { value => value + 1 }:fn(i32) -> i32
}
''')
        check(consumer, 1, 'E0402')
        print('PASS missing private global is refused, not copied', flush=True)


if __name__ == '__main__':
    main()
