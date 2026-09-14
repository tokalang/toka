#!/usr/bin/env python3
"""Incremental G implementation regressions, not full G qualification."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-G-') as directory:
        work = Path(directory)
        def check(name, *flags):
            return subprocess.run([
                str(args.build_dir / 'bin/tokac'), '--workspace-node', 'whole-value-G',
                '--workspace-root', str(ROOT),
                str(ROOT / 'tests/semantics/whole_value_generics' / (name + '.tk')),
                *map(str, flags)], env=env, cwd=ROOT, capture_output=True,
                text=True, timeout=60)

        for name in ('relay', 'local_relay', 'slot_write', 'whole_borrow', 'reference_forwarding', 'structure_views', 'index_views',
                     'enum_transfer', 'shared_observer_contract', 'literal_preservation'):
            normal = check(name, '--check-only')
            shadow = check(name, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, name
            output = work / name
            built = check(name, '-o', output)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
            assert result.returncode == 0, (name, result.returncode, result.stderr)
            if name == 'whole_borrow':
                ir = work / 'whole_borrow.ll'
                emitted = check(name, '--emit-llvm', '-o', ir)
                assert emitted.returncode == 0, emitted.stderr
                bodies = []
                for function, body in re.findall(
                        r'^define linkonce_odr ptr @(__toka_gfn_[^(]+)\(ptr %value\) \{(.*?)^}',
                        ir.read_text(), re.M | re.S):
                    identity = function.split('_M_')[0].removeprefix('__toka_gfn_')
                    if ';6:borrow;' in bytes.fromhex(identity).decode():
                        bodies.append(body)
                assert len(bodies) == 3
                for body in bodies:
                    # All three results are the caller slot, not a callee
                    # descriptor or a second load through that slot's value.
                    loads = re.findall(r'(%[^ ]+) = load ptr, ptr %value.addr,', body)
                    assert len(loads) == 1 and body.count('= load ptr') == 1, body
                    assert f'ret ptr {loads[0]}' in body, body
            print('PASS runtime/parity ' + name, flush=True)
        for name, diagnostic in (
            ('borrowed_parameter_cannot_move', 'E0473'),
            ('reference_consumption_rejected', 'E04661'),
            ('concrete_payload_not_owner', 'E04571'),
            ('payload_write_is_not_slot_write', 'E04571'),
            ('abstract_does_not_reveal_fields', 'E0417'),
            ('abstract_root_not_assumed', 'E0406'),
            ('local_descriptor_escape', 'E0455'),
            ('named_copy_requires_cede', 'E04570'),
            ('copy_source_invalidated', 'E0438'),
            ('abstract_field_root_not_assumed', 'E0406'),
            ('legacy_quote_type', 'E01268'),
            ('legacy_quote_binding', 'E01268'),
            ('legacy_quote_expression', 'E01268'),
        ):
            normal = check(name, '--check-only')
            shadow = check(name, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1, name
            assert normal.stderr == shadow.stderr and f'error[{diagnostic}]' in normal.stderr, normal.stderr
            for flag, extension in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (name + extension)
                rejected = check(name, flag, '-o', output)
                assert rejected.returncode == 1 and not output.exists(), (name, rejected.stderr)
            print('PASS rejection/parity/no-artifact ' + name, flush=True)

        provider = work / 'provider.tk'
        declaration = 'fn relay<T>(cede value:T) -> T { return cede value }'
        provider.write_text('pub ' + declaration + '\n')
        prefix = [str(args.build_dir / 'bin/tokac'), '--workspace-node', 'G-hidden',
                  '--workspace-root', str(work), '-I', str(work)]
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        assert (work / 'provider.tki').exists()
        provider.unlink()
        consumer = work / 'consumer.tk'
        consumer.write_text((ROOT / 'tests/semantics/whole_value_generics/relay.tk')
                            .read_text().replace(declaration, 'import provider::{relay}'))
        normal = subprocess.run(prefix + [str(consumer), '--check-only'], env=env,
                                cwd=work, capture_output=True, text=True, timeout=60)
        shadow = subprocess.run(prefix + [str(consumer), '--check-only',
                                '--non-call-transfer-shadow=json'], env=env,
                                cwd=work, capture_output=True, text=True, timeout=60)
        assert normal.returncode == shadow.returncode == 0, (normal.stderr, shadow.stderr)
        assert normal.stderr == shadow.stderr
        output = work / 'source-hidden'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (result.returncode, result.stderr)
        print('PASS source-hidden runtime/parity relay', flush=True)

        # A provider's symbolic Slot<T> must not be rebound to a caller's
        # same-named declaration when a generic factory result is inferred.
        provider.write_text('''pub shape Slot<T>(value:T)
pub fn pack<T>(cede value:T) -> Slot<T> { return Slot<T>(value=cede value) }
''')
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('''import provider::{Slot as RemoteSlot, pack}
shape Slot<T>(different:T)
shape Resource(value:i32)
fn wrap<T>(cede value:T) -> RemoteSlot<T> {
    auto result = pack<T>(cede value)
    { auto &view = &(result.value) }
    return cede result
}
fn main() -> i32 {
    auto ^owner = new Resource(value=31)
    auto result = wrap<^Resource>(cede ^owner)
    if result.value.value != 31 { return 1 }
    return 0
}
''')
        output = work / 'source-hidden-structure'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (result.returncode, result.stderr)
        print('PASS source-hidden structure/nominal shadowing', flush=True)


if __name__ == '__main__':
    main()
