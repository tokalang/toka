#!/usr/bin/env python3
"""Byte-owner contract: unchanged regressions plus current-instance controls."""
import test_task_result_projection as gate

PREFIX = '''import std/vec::{Vec}
import std/bytes::{Bytes}
import std/task::{block_on}
fn relay(cede value: Vec<u8>) -> async Vec<u8> { return cede value }
'''

gate.CASES = {
    name: ((gate.ROOT / 'tests/pass' / (name + '.tk')).read_text(), None)
    for name in ('g09_async_owning_payload_drop_regression', 'g13_net_buffer_abi_test')
}
gate.CASES['growth_freeze_thaw_take'] = (PREFIX + '''
fn freeze(cede value: Vec<u8>) -> async Bytes { return Bytes::from_vec(cede value) }
fn main() -> i32 {
    auto data# = Vec<u8>::new()
    auto i# = 0:usize
    loop i < 129:usize { data#.push(i as u8); i += 1:usize }
    auto bytes# = block_on(freeze(cede data))
    auto thawed# = bytes#.into_vec()
    assert(bytes.len() == 0:usize, "retired Bytes")
    auto taken = thawed#.take()
    assert(thawed.len() == 0:usize, "retired Vec")
    auto result = block_on(relay(cede taken))
    assert(result.len() == 129:usize, "growth and handoff")
    assert(result.get(128:usize) == 128:u8, "last byte")
    return 0
}
''', None)
gate.CASES['both_branches'] = (PREFIX + '''
fn make(flag: bool) -> Vec<u8> {
    auto data# = Vec<u8>::new()
    if flag { data#.push(7:u8) } else { data#.push(8:u8) }
    return cede data
}
fn main() -> i32 {
    auto first = make(true)
    auto second = make(false)
    auto a = block_on(relay(cede first))
    auto b = block_on(relay(cede second))
    assert(a.get(0) == 7:u8 && b.get(0) == 8:u8, "actual branch")
    return 0
}
''', None)
for name, mutation in {
    'raw_import': 'auto data = Vec<u8>::from_raw(null, 0:usize, 0:usize)',
    'direct_constructor': 'auto data = Vec<u8>(*buf=null, len=0:usize, cap=0:usize)',
    'overwritten_length': 'auto data# = Vec<u8>::new(); data.len = 1:usize',
    'overwritten_capacity': 'auto data# = Vec<u8>::new(); data.cap = 1:usize',
    'raw_export': 'auto data# = Vec<u8>::new(); auto nul *pointer = data.unsafe_as_raw()',
    'raw_takeaway': 'auto data# = Vec<u8>::new(); auto nul *pointer# = data#.unsafe_into_raw()',
    'unproved_length': 'auto data# = Vec<u8>::with_capacity(4); data#.unsafe_set_len(4)',
}.items():
    gate.CASES[name] = (PREFIX + '\nfn main() -> i32 {\n' + mutation + '''
    auto result = block_on(relay(cede data))
    return 0
}
''', 'E04661')
gate.CASES['unqualified_branch'] = (PREFIX + '''
fn make(flag: bool) -> Vec<u8> {
    auto data# = Vec<u8>::new()
    if flag { data.cap = 1:usize }
    return cede data
}
fn main() -> i32 { auto value = make(false); auto result = block_on(relay(cede value)); return 0 }
''', 'E04661')
gate.CASES['view_escape'] = (PREFIX + '''
fn escaped() -> bytes {
    auto data# = Vec<u8>::new()
    data#.push(7:u8)
    auto value = Bytes::from_vec(cede data)
    return value.as_slice()
}
fn main() -> i32 { auto view = escaped(); return 0 }
''', 'E0455')
gate.CASES['aggregate_forward'] = (PREFIX + '''
shape Envelope(buffer: Vec<u8>, tag: i32)
fn pack(cede value: Vec<u8>) -> Envelope {
    return Envelope(buffer = cede value, tag = 9)
}
fn forward_envelope(cede value: Envelope) -> async Envelope { return cede value }
fn main() -> i32 {
    auto data# = Vec<u8>::new()
    data#.push(13:u8)
    auto envelope = pack(cede data)
    auto result = block_on(forward_envelope(cede envelope))
    assert(result.tag == 9 && result.buffer.get(0) == 13:u8, "qualified fields")
    return 0
}
''', None)
gate.CASES['unknown_modifier'] = (PREFIX + '''
fn replace(value#: Vec<u8>) { value.cap = 1:usize }
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    replace(value#)
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04661')
gate.CASES['mutated_task_result'] = (PREFIX + '''
fn main() -> i32 {
    auto value = Vec<u8>::new()
    auto result# = block_on(relay(cede value))
    result.cap = 1:usize
    auto second = block_on(relay(cede result))
    return 0
}
''', 'E04661')
gate.CASES['same_name_not_contract'] = ('''import std/task::{block_on}
shape Bytes(nul *buf: u8, len: usize)
fn make(nul *data: u8) -> async Bytes { return Bytes(*buf=*data, len=0:usize) }
fn main() -> i32 { auto value = block_on(make(null)); return 0 }
''', 'E04661')
gate.CASES['cache_requires_current_actual'] = (PREFIX + '''
fn forward_bytes(cede value: Vec<u8>) -> Vec<u8> { return cede value }
fn main() -> i32 {
    auto first = Vec<u8>::new()
    auto good = forward_bytes(cede first)
    auto ok = block_on(relay(cede good))
    auto second# = Vec<u8>::new()
    second.cap = 7:usize
    auto bad = forward_bytes(cede second)
    auto rejected = block_on(relay(cede bad))
    return 0
}
''', 'E04661')
gate.CASES['wrapped_result_take'] = (PREFIX + '''import core/result::{Result}
import std/net::{AsyncReadResult}
fn wrap_bytes(cede value: Vec<u8>) -> async Result<AsyncReadResult, i32> {
    return Result<AsyncReadResult, i32>::Ok(AsyncReadResult(buffer=cede value, bytes_read=1:usize, eof=false))
}
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    value#.push(21:u8)
    auto completed = block_on(wrap_bytes(cede value))
    auto read = completed.unwrap()
    auto bytes = read.take_buffer()
    auto received = block_on(relay(cede bytes))
    assert(received.get(0) == 21:u8, "selected variant and buffer transfer")
    return 0
}
''', None)
gate.CASES['rejected_call_rollback'] = (PREFIX + '''
fn need_number(cede value: Vec<u8>, number: i32) {}
fn main() -> i32 {
    auto value = Vec<u8>::new()
    need_number(cede value, true)
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04571')
gate.CASES['task_result_branch_pollution'] = (PREFIX + '''
fn damaged(flag: bool) -> async Vec<u8> {
    auto first = Vec<u8>::new()
    auto value# = block_on(relay(cede first))
    if flag { value.cap = 1:usize } else { value = Vec<u8>::new() }
    return cede value
}
fn main() -> i32 { auto result = block_on(damaged(false)); return 0 }
''', 'E04661')
gate.CASES['local_use_is_not_qualification'] = (PREFIX + '''
fn inspect_value(cede value: Vec<u8>) -> usize {
    auto count = value.len()
    cede value
    return count
}
fn main() -> i32 {
    auto raw = Vec<u8>::from_raw(null, 0:usize, 0:usize)
    auto number = inspect_value(cede raw)
    assert(number == 0:usize, "local use does not need an independence witness")
    return 0
}
''', None)
gate.CASES['descriptor_alias_control'] = (PREFIX + '''
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    { auto &descriptor# = &value; descriptor.cap = 0:usize }
    assert(value.len() == 0:usize, "legal descriptor write, not an ownership proof")
    return 0
}
''', None)
gate.CASES['descriptor_alias_pollution'] = (PREFIX + '''
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    { auto &descriptor# = &value; descriptor.cap = 0:usize }
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04661')
EXTERN = '''import std/bytes::{Bytes}
import std/task::{block_on}
extern fn byte_foreign_write(value#: Bytes) -> void
fn relay_bytes(cede value: Bytes) -> async Bytes { return cede value }
fn main() -> i32 {
    auto value# = Bytes::new()
    byte_foreign_write(value#)
    auto result = block_on(relay_bytes(cede value))
    return 0
}
'''
gate.CASES['extern_mutation'] = (EXTERN, 'E04661')
gate.CASES['extern_task_mutation'] = ('''import std/bytes::{Bytes}
import std/task::{block_on}
extern fn byte_foreign_task(value#: TaskHandle<Bytes>) -> void
fn relay_bytes(cede value: Bytes) -> async Bytes { return cede value }
fn main() -> i32 {
    auto input = Bytes::new()
    auto task# = relay_bytes(cede input)
    byte_foreign_task(task#)
    auto result = block_on(task)
    return 0
}
''', 'E04661')
gate.CASES['closure_mutation'] = (PREFIX + '''
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    { auto action# = { => value.cap = 0:usize; return 0 }; auto status = action#() }
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04661')

# These descriptor-poisoning programs are compiler-only negatives. Never run
# them: an incorrect admission can otherwise dereference the original null buf.
for name, result_type, expression in (
    ('nested_push_mutation', 'u8', 'value#.push(poison(value#))'),
    ('nested_resize_mutation', 'usize', 'value#.resize(poison(value#), 0:u8)'),
    ('sequential_push_mutation', 'u8', 'auto byte = poison(value#); value#.push(byte)'),
    ('sequential_resize_mutation', 'usize', 'auto count = poison(value#); value#.resize(count, 0:u8)'),
):
    gate.CASES[name] = (PREFIX + f'''
fn poison(value#: Vec<u8>) -> {result_type} {{ value.cap = 1:usize; return {0 if result_type == 'u8' else 1}:{result_type} }}
fn main() -> i32 {{
    auto value# = Vec<u8>::new()
    {expression}
    auto result = block_on(relay(cede value))
    return 0
}}
''', 'E04661')
gate.CASES['nested_valid_control'] = (PREFIX + '''
fn number() -> u8 { return 7:u8 }
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    value#.push(number())
    auto result = block_on(relay(cede value))
    assert(result.get(0) == 7:u8, "nested scalar argument")
    return 0
}
''', None)
gate.CASES['nested_new_current_receipt'] = (PREFIX + '''
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    value#.push(9:u8)
    value#.push((value#.take()).len() as u8)
    auto result = block_on(relay(cede value))
    assert(result.len() == 1:usize && result.get(0) == 1:u8, "use the new empty receiver after take")
    return 0
}
''', None)
gate.CASES['nested_member_mutation'] = (PREFIX + '''
shape Envelope(buffer#: Vec<u8>)
fn poison(value#: Vec<u8>) -> u8 { value.cap = 1:usize; return 1:u8 }
fn main() -> i32 {
    auto data = Vec<u8>::new()
    auto owner# = Envelope(buffer=cede data)
    owner.buffer#.push(poison(owner.buffer#))
    auto value = owner.buffer#.take()
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04661')
gate.CASES['nested_alias_mutation'] = (PREFIX + '''
fn poison_alias(value#: Vec<u8>) -> u8 {
    auto &descriptor# = &value
    descriptor.cap = 1:usize
    return 0:u8
}
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    value#.push(poison_alias(value#))
    auto result = block_on(relay(cede value))
    return 0
}
''', 'E04661')
gate.CASES['saved_cede_with_later_argument'] = (PREFIX + '''
fn observe_other(value#: Vec<u8>) -> u8 { return 3:u8 }
fn saved(cede value: Vec<u8>, marker: u8) -> async Vec<u8> { return cede value }
fn main() -> i32 {
    auto value# = Vec<u8>::new()
    value#.push(19:u8)
    auto other# = Vec<u8>::new()
    auto result = block_on(saved(cede value, observe_other(other#)))
    assert(result.get(0) == 19:u8, "transferred value retains its receipt")
    return 0
}
''', None)
gate.CASES['nested_mutation_rollback'] = (PREFIX + '''
fn poison(value#: Vec<u8>) -> u8 { value.cap = 1:usize; return 1:u8 }
fn need_boolean(cede value: Vec<u8>, flag: bool) { cede value }
fn main() -> i32 {
    auto value = Vec<u8>::new()
    auto other# = Vec<u8>::new()
    need_boolean(cede value, poison(other#))
    auto first = block_on(relay(cede value))
    auto second = block_on(relay(cede other))
    return 0
}
''', 'E04571')


def schema_controls(build_dir):
    import os
    import shutil
    import subprocess
    import tempfile
    from pathlib import Path
    compiler = Path(build_dir).resolve() / 'bin/tokac'
    with tempfile.TemporaryDirectory(prefix='toka-byte-contract-') as directory:
        work = Path(directory)
        source = work / 'consumer.tk'
        source.write_text(PREFIX + '''
fn main() -> i32 {
    auto value = Vec<u8>::new()
    auto result = block_on(relay(cede value))
    return 0
}
''')
        library = work / 'lib'
        shutil.copytree(gate.ROOT / 'lib', library,
                        ignore=shutil.ignore_patterns('*.o', '*.a', '*.tki', '*.ll'))
        env = dict(os.environ, TOKA_LIB=str(library))

        def check_error(source, code, label):
            prefix = [str(compiler), '--workspace-node', 'byte-contract-test', '--workspace-root', str(work), '-I', str(work), str(source)]
            normal = subprocess.run(prefix + ['--check-only'], cwd=work, env=env,
                                    capture_output=True, text=True, timeout=90)
            shadow = subprocess.run(prefix + ['--check-only', '--non-call-transfer-shadow=json'],
                                    cwd=work, env=env, capture_output=True, text=True, timeout=90)
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, normal.stderr
            assert f'error[{code}]' in normal.stderr, normal.stderr
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (label + suffix)
                result = subprocess.run(prefix + [flag, '-o', str(output)], cwd=work,
                                        env=env, capture_output=True, text=True, timeout=90)
                assert result.returncode == 1 and f'error[{code}]' in result.stderr and not output.exists(), result.stderr

        baseline = subprocess.run([str(compiler), '--workspace-node', 'byte-contract-test', '--workspace-root', str(work), str(source), '--check-only'], cwd=work,
                                  env=env, capture_output=True, text=True, timeout=90)
        assert baseline.returncode == 0, baseline.stderr
        vec = library / 'std/vec.tk'
        original = vec.read_text()
        pattern = 'self.buf[self.len] = cede val\n            self.len += 1'
        assert original.count(pattern) == 1
        vec.write_text(original.replace(pattern, 'self.buf[self.len] = cede val\n            self.len += 2'))
        check_error(source, 'E04661', 'changed-schema')
        vec.write_text(original)
        provider = work / 'byte_provider.tk'
        provider.write_text('import std/vec::{Vec}\npub fn make() -> Vec<u8> { return Vec<u8>::new() }\n')
        emitted = subprocess.run([str(compiler), '--workspace-node', 'byte-contract-test', '--workspace-root', str(work),
                                  str(provider), '-c', '--emit-interface', '-o', str(work / 'byte_provider.o')],
                                 cwd=work, env=env, capture_output=True, text=True, timeout=90)
        assert emitted.returncode == 0 and provider.with_suffix('.tki').is_file(), emitted.stderr
        provider.rename(work / 'byte_provider.hidden')
        source.write_text(PREFIX + '''import byte_provider::{make}
fn main() -> i32 { auto value = make(); auto result = block_on(relay(cede value)); return 0 }
''')
        check_error(source, 'E04661', 'source-hidden')
        print('PASS altered implementation schema and source-hidden receipt rejection', flush=True)
        # The unknown call itself remains legal. Only subsequent use of the
        # expired independence receipt is refused by the negative above.
        source.write_text(EXTERN.replace('auto result = block_on(relay_bytes(cede value))',
                                         'assert(value.len() == 0:usize, "local use after foreign call")'))
        prefix = [str(compiler), str(source), '--check-only']
        normal = subprocess.run(prefix, cwd=work, env=env, capture_output=True, text=True, timeout=90)
        shadow = subprocess.run(prefix + ['--non-call-transfer-shadow=json'], cwd=work,
                                env=env, capture_output=True, text=True, timeout=90)
        assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr
        # Explicitly a Sema control: direct mutable aggregate extern lowering
        # currently has a ptr/aggregate signature mismatch. That independent
        # ABI path is not repaired, run or counted as a runtime pass here.
        print('PASS ordinary foreign-call Sema/parity control (no ABI/runtime claim)', flush=True)

if __name__ == '__main__':
    gate.main()
    import sys
    if '--case' not in sys.argv:
        from test_byte_buffer_cleanup import run
        build = sys.argv[sys.argv.index('--build-dir') + 1]
        run(build)
        schema_controls(build)
