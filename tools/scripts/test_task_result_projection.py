#!/usr/bin/env python3
"""Source-visible task-result projection: values, origins and rejection gates."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = '''import std/task::{block_on}
fn second(first: str, value: str) -> async str <- value { return value }
fn wait_value<T>(task: TaskHandle<T>) -> T <- task {
    auto result = task.wait:T
    return cede result
}

'''
CASES = {
    "external_after_cleanup": (PREFIX + '''
fn relay(input: str) -> str <- input {
    auto unrelated = string::from("unrelated")
    auto first = unrelated.as_view()
    auto task = second(first, input)
    return wait_value(task)
}
fn main() -> i32 {
    auto a = string::from("first")
    auto b = string::from("second")
    auto x = relay(a.as_view())
    auto y = relay(b.as_view())
    assert(x.equals("first"), "first source after task cleanup")
    assert(y.equals("second"), "cache hit uses current source")
    return 0
}
''', None),
    "static_and_scalar": ('''import std/task::{block_on}
fn literal() -> async str { return "static" }
fn number() -> async i32 { return 42 }
fn main() -> i32 {
    auto a = block_on(literal())
    auto b = block_on(number())
    assert(a.equals("static"), "static witness")
    assert(b == 42, "closed scalar result")
    return 0
}
''', None),
    "owned_result": ('''import std/task::{block_on}
auto drops# = 0:i32
shape Token(value: i32)
impl Token@Encap {
    pub value
    fn drop(self#) { drops += 1 }
}
fn make() -> async ^Token { return new Token(value = 17) }
fn main() -> i32 {
    { auto ^token = block_on(make()); assert(token.value == 17, "owned result"); }
    assert(drops == 1, "one result cleanup")
    return 0
}
''', None),
    "local_escape": (PREFIX + '''
fn escape(input: str) -> str <- input {
    auto local = string::from("local")
    auto task = second(input, local.as_view())
    return wait_value(task)
}
fn main() -> i32 { return 0 }
''', "E0455"),
    "cache_then_escape": (PREFIX + '''
fn escape(input: str) -> str <- input {
    auto good = second("unused", input)
    auto first = wait_value(good)
    auto local = string::from("local")
    auto bad = second(input, local.as_view())
    return wait_value(bad)
}
fn main() -> i32 { return 0 }
''', "E0455"),
    "missing_ceiling": ('''import std/task
fn missing(task: TaskHandle<str>) -> str {
    auto value = task.wait
    return value
}
fn main() -> i32 { return 0 }
''', "E04661"),
    "frame_escape": ('''import std/task::{block_on}
fn bad() -> async str {
    auto owner = string::from("frame")
    return owner.as_view()
}
fn main() -> i32 { auto result = block_on(bad()); return 0 }
''', "E0455"),
    "frame_descriptor": ('''import std/task::{block_on}
fn bad() -> async &str {
    auto view = "static bytes"
    return &view
}
fn main() -> i32 { return 0 }
''', "E0455"),
    "unrelated_type": ('''shape TaskHandle<T>(nul *#coro_handle: void)
fn main() -> i32 {
    auto task = TaskHandle<str>(*coro_handle = null)
    auto result = task.wait
    return 0
}
''', "E04661"),
}


CASES.update({
    "frame_parameter_descriptor": ('''import std/task
fn bad(value: str) -> async &str <- value { return &value }
fn main() -> i32 { return 0 }
''', "E04658"),
    "reference_external": ('''import std/task
fn borrow<T>(value: T) -> async T <- value { return value }
fn receive<T>(task: TaskHandle<T>) -> T <- task
where:
    T: morphology reference_only
{
    auto result = task.wait:T
    return result
}
fn main() -> i32 {
    auto view = "outside"
    auto task = borrow(&view)
    auto &result = receive(task)
    assert(result.equals("outside"), "external descriptor remains live")
    return 0
}
''', None),
    "reference_escape": ('''import std/task
fn borrow<T>(value: T) -> async T <- value { return value }
fn receive<T>(task: TaskHandle<T>) -> T <- task
where:
    T: morphology reference_only
{
    auto result = task.wait:T
    return result
}
fn bad() -> &str {
    auto view = "static bytes"
    auto task = borrow(&view)
    return receive(task)
}
fn main() -> i32 { return 0 }
''', "E0455"),
    "branch_preserved": (PREFIX + '''
fn relay(input: str, flag: bool) -> str <- input {
    auto task = second("unused", input)
    if flag { auto ignored = 1 } else { auto ignored = 2 }
    return wait_value(task)
}
fn main() -> i32 {
    auto owner = string::from("stable")
    auto a = relay(owner.as_view(), true)
    auto b = relay(owner.as_view(), false)
    assert(a.equals(b), "same witness at join")
    return 0
}
''', None),
    "branch_conflict": (PREFIX + '''
fn bad(input: str, flag: bool) -> str <- input {
    auto initial = second("unused", input)
    auto task# = cede initial
    auto local = string::from("local")
    auto alternative = second("unused", local.as_view())
    if flag { task = cede alternative }
    return wait_value(task)
}
fn main() -> i32 { return 0 }
''', "E04661"),
    "mutated_task": (PREFIX + '''
fn bad(input: str) -> str <- input {
    auto task# = second("unused", input)
    unsafe { task.*coro_handle = null }
    return wait_value(task)
}
fn main() -> i32 { return 0 }
''', "E04661"),
    "invalid_producer": (PREFIX + '''
fn invalid<T>(value: T) -> async T { return true }
fn main() -> i32 {
    auto task = invalid(7:i32)
    auto value = wait_value(task)
    return 0
}
''', "E0408"),
    "declaration_order": ('''import std/task::{block_on}
fn main() -> i32 {
    auto owner = string::from("later")
    auto task = later(owner.as_view())
    auto value = block_on(task)
    assert(value.equals("later"), "late producer")
    return 0
}
fn later(value: str) -> async str <- value { return value }
''', None),
})
CASES["task_forward_move"] = (PREFIX + '''
fn forward(cede task: TaskHandle<str>) -> TaskHandle<str> <- task { return cede task }
fn main() -> i32 {
    auto owner = string::from("forward")
    auto original = second("unused", owner.as_view())
    auto moved = forward(cede original)
    auto value = wait_value(moved)
    assert(value.equals("forward"), "task witness moved with value")
    return 0
}
''', None)
CASES["selected_field"] = (PREFIX + '''
shape Pair(first: str, second: str)
fn pick(value: Pair) -> async str <- value { return value.second }
fn relay(input: str) -> str <- input {
    auto local = string::from("unused")
    auto pair = Pair(first = local.as_view(), second = input)
    auto task = pick(pair)
    return wait_value(task)
}
fn main() -> i32 {
    auto owner = string::from("field")
    auto result = relay(owner.as_view())
    assert(result.equals("field"), "only selected field")
    return 0
}
''', None)
CASES["source_hidden"] = ('''import provider::{produce}
import std/task::{block_on}
fn main() -> i32 {
    auto task = produce("static")
    auto value = block_on(task)
    return 0
}
''', "E04661")
CASES["rejected_call_rollback"] = (PREFIX + '''
fn consume(cede task: TaskHandle<str>, amount: i32) -> str <- task {
    auto owned = cede task
    return owned.wait
}
fn main() -> i32 {
    auto initial = second("unused", "static")
    auto checked = wait_value(initial)
    auto task = second("unused", "static")
    consume(cede task, "wrong type")
    auto after = wait_value(task)
    return 0
}
''', "E04571")
CASES["post_proof_rollback"] = ('''import provider::{produce}
import std/task
fn consume(cede owner: string, task: TaskHandle<str>) -> str <- task {
    auto result = task.wait
    cede owner
    return result
}
fn main() -> i32 {
    auto owner = string::from("kept")
    auto task = produce("hidden")
    consume(cede owner, task)
    assert(owner.len() == 4, "source restored after proof rejection")
    return 0
}
''', "E04661")
CASES["shared_result"] = ('''import std/task::{block_on}
auto drops# = 0:i32
shape Token(value: i32)
impl Token@Encap {
    pub value
    fn drop(self#) { drops += 1 }
}
fn make() -> async ~Token {
    auto ~value = new Token(value = 9)
    return cede ~value
}
fn main() -> i32 {
    {
        auto ~result = block_on(make())
        { auto ~other = ~result; assert(other.value == 9, "shared result copy"); }
        assert(drops == 0, "remaining owner alive")
        assert(result.value == 9, "result still usable")
    }
    assert(drops == 1, "shared result exact once")
    return 0
}
''', None)
CASES["repeat_owned"] = ('''import std/task::{block_on}
auto drops# = 0:i32
shape Token(value: i32)
impl Token@Encap { fn drop(self#) { drops += 1 } }
fn make() -> async Token { return Token(value = 9) }
fn main() -> i32 {
    auto task = make()
    { auto result = block_on(task) }
    assert(drops == 1, "first result cleaned once")
    { auto repeated = block_on(task) }
    return 0
}
''', None)
for name in ("g09_async_wait_syntax", "g09_async_cede_shared_move_test"):
    CASES[name] = ((ROOT / "tests/pass" / (name + ".tk")).read_text(), None)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--keep-dir', type=Path)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    selected = args.case or list(CASES)
    assert set(selected) <= CASES.keys()
    with tempfile.TemporaryDirectory(prefix='toka-task-result-') as temporary:
        work = args.keep_dir or Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        rows = []
        for name in selected:
            if name in ('source_hidden', 'post_proof_rollback'):
                provider = work / 'provider.tk'
                provider.write_text('pub fn produce(value: str) -> async str <- value { return value }\n')
                emitted = subprocess.run([str(compiler), '-c', '--emit-interface', str(provider),
                                          '-o', str(work / 'provider.o')], cwd=ROOT, env=env,
                                         capture_output=True, text=True, timeout=90)
                assert emitted.returncode == 0 and (work / 'provider.tki').is_file(), emitted.stderr
                provider.replace(work / 'provider.hidden')
            text, error = CASES[name]
            source = work / (name + '.tk')
            source.write_text(text)
            def compile(flags):
                return subprocess.run([str(compiler), '-I', str(work), str(source), *map(str, flags)],
                                      cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            normal = compile(['--check-only'])
            shadow = compile(['--check-only', '--non-call-transfer-shadow=json'])
            (work / (name + '.stderr')).write_text(normal.stderr)
            expected = 1 if error else 0
            assert normal.returncode == shadow.returncode == expected, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, name
            if name == 'rejected_call_rollback':
                assert all(f'error[{code}]' not in normal.stderr for code in ('E0438', 'E0410', 'E04661')), normal.stderr
            if name == 'post_proof_rollback':
                assert all(f'error[{code}]' not in normal.stderr for code in ('E0438', 'E0410')), normal.stderr
            for mode, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (name + suffix)
                result = compile([mode, '-o', output])
                assert result.returncode == expected, (name, result.stderr)
                if error:
                    assert f'error[{error}]' in result.stderr and str(source) in result.stderr, result.stderr
                    assert not output.exists(), output
                else:
                    assert output.is_file() and output.stat().st_size > 0, output
            if not error:
                executable = work / name
                result = compile(['-o', executable])
                assert result.returncode == 0, result.stderr
                run = subprocess.run([str(executable)], capture_output=True, timeout=30)
                if name == 'repeat_owned':
                    assert run.returncode in (-4, -5), (name, run.returncode, run.stderr)
                else:
                    assert run.returncode == 0, (name, run.returncode, run.stderr)
            rows.append(dict(case=name, expected=expected, parity=True, artifacts_checked=True))
            (work / 'results.json').write_text(json.dumps(rows, indent=2))
            print('PASS', name, flush=True)


if __name__ == '__main__':
    main()
