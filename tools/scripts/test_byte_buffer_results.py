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
gate.CASES['discarded_intermediate_requirement'] = (PREFIX + '''
fn inspect_result(cede value: Vec<u8>) -> i32 {
    auto result = block_on(relay(cede value))
    return 7
}
fn main() -> i32 {
    auto raw = Vec<u8>::from_raw(null, 0:usize, 0:usize)
    auto number = inspect_result(cede raw)
    return 0
}
''', 'E04661')


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

if __name__ == '__main__':
    gate.main()
    import sys
    if '--case' not in sys.argv:
        from test_byte_buffer_cleanup import run
        build = sys.argv[sys.argv.index('--build-dir') + 1]
        run(build)
        schema_controls(build)
