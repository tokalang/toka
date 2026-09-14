#!/usr/bin/env python3
"""Incremental G implementation regressions, not full G qualification."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MORPHOLOGY_CASES = {
    'morphology_borrow_extendable_level2': 'E0621',
    'morphology_borrow_extendable_raw': 'E0621',
    'morphology_soul_only_unique': 'E0621',
    'morphology_constraint_unknown': 'E01267',
    'morphic_member_missing_quote': 'E04658',
    'morphic_member_quote_on_plain_field': 'E01268',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-G-') as directory:
        work = Path(directory)
        def check(name, *flags):
            source_dir = ROOT / ('tests/pass' if name == 'g08_raw_layered_nullability_types' else
                                 'tests/fail' if name in MORPHOLOGY_CASES else
                                 'tests/semantics/whole_value_generics')
            return subprocess.run([
                str(args.build_dir / 'bin/tokac'), '--workspace-node', 'whole-value-G',
                '--workspace-root', str(ROOT),
                str(source_dir / (name + '.tk')),
                *map(str, flags)], env=env, cwd=ROOT, capture_output=True,
                text=True, timeout=60)

        for name in ('relay', 'local_relay', 'slot_write', 'whole_borrow', 'reference_forwarding', 'structure_views', 'index_views',
                     'enum_transfer', 'shared_observer_contract', 'literal_preservation',
                     'library_domains', 'option_fallback', 'raw_local_relay',
                     'associated_values', 'qualified_borrow', 'alias_values',
                     'qualified_slot_write', 'qualified_loan_wrappers',
                     'borrowed_record_values', 'qualified_slot_loop',
                     'static_factory_chain', 'reflection_fields',
                     'enum_struct_field_binding', 'g08_raw_layered_nullability_types'):
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
        for name in ('raw_slot_borrow',):
            normal = check(name, '--check-only')
            shadow = check(name, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, name
            for flag, extension in (('-c', '.o'), ('--emit-llvm', '.ll')):
                output = work / (name + extension)
                built = check(name, flag, '-o', output)
                assert built.returncode == 0 and output.exists(), (name, built.stderr)
            print('PASS compile/parity ' + name, flush=True)
        for name, diagnostic in (
            ('borrowed_parameter_cannot_move', 'E0473'),
            ('reference_consumption_rejected', 'E04661'),
            ('raw_slot_readonly_rejected', 'E04573'),
            ('math_nonfloat_rejected', 'E0606'),
            ('math_shape_rejected', 'E04571'),
            ('atomic_shape_rejected', 'E0619'),
            ('atomic_handle_rejected', 'E0621'),
            ('plain_resource_match_rejected', 'E0554'),
            ('associated_opaque_rejected', 'E0417'),
            ('qualified_borrow_readonly', 'E04572'),
            ('qualified_descriptor_escape', 'E0455'),
            ('borrowed_record_escape', 'E0455'),
            ('reflection_wrong_owner', 'E0406'),
            ('reflection_readonly', 'E04572'),
            ('nullable_return_rejected', 'E0408'),
            ('qualified_loan_existing', 'E0475'),
            ('qualified_loan_parent', 'E0475'),
            ('qualified_loan_child', 'E0475'),
            ('qualified_loan_duplicate', 'E0475'),
            ('qualified_loan_branch', 'E0475'),
            ('qualified_loan_branch_other', 'E0475'),
            ('qualified_loan_rebindable', 'E0475'),
            ('qualified_loan_changed_view', 'E0475'),
            ('qualified_slot_write_no_capability', 'E04571'),
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
        ) + tuple(MORPHOLOGY_CASES.items()):
            normal = check(name, '--check-only')
            shadow = check(name, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1, name
            assert normal.stderr == shadow.stderr and f'error[{diagnostic}]' in normal.stderr, normal.stderr
            if name == 'morphic_member_missing_quote':
                assert 'ProjectedHandleRequiresSubroot' in normal.stderr, normal.stderr
            if name.startswith('qualified_loan_'):
                assert 'E0438' not in normal.stderr and 'E0410' not in normal.stderr, normal.stderr
                assert 'conflicting borrow originates here' in normal.stderr, normal.stderr
            if name == 'qualified_slot_write_no_capability':
                assert 'E0438' not in normal.stderr and 'E0410' not in normal.stderr, normal.stderr
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

        provider.write_text('''import std/math::{Float, sqrt}
import core/intrinsics/atomic::{load, Ordering}
pub fn checked_root<T: @{Float, Copy}>(value:T) -> T { return sqrt(value) }
pub fn checked_load<T: @Copy>(value#:T) -> T
where:
    T: morphology soul_only
{ return load(value#, Ordering::SeqCst) }
''')
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('''import provider::{checked_root, checked_load}
fn main() -> i32 {
    auto value# = 9:i32
    if checked_load(value#) != 9 || checked_root(16.0) != 4.0 { return 1 }
    return 0
}
''')
        output = work / 'source-hidden-domains'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(output)], timeout=10).returncode == 0
        for label, body, code in (
            ('float-bound', 'auto result = checked_root(7)', 'E0606'),
            ('morphology-bound', 'auto ^#value = new Value(number=1)\n'
             'auto result = checked_load<^Value>(^#value)', 'E0621'),
            ('intrinsic-domain', 'auto value# = Value(number=1)\n'
             'auto result = checked_load(value#)', 'E0619'),
        ):
            consumer.write_text('import provider::{checked_root, checked_load}\n'
                                'shape Value(number:i32)\nfn main() -> i32 {\n' + body + '\nreturn 0\n}\n')
            diagnostics = []
            for flags in ([], ['--non-call-transfer-shadow=json']):
                rejected = subprocess.run(prefix + [str(consumer), '--check-only', *flags],
                    env=env, cwd=work, capture_output=True, text=True, timeout=60)
                assert rejected.returncode == 1 and f'error[{code}]' in rejected.stderr, rejected.stderr
                diagnostics.append(rejected.stderr)
            assert diagnostics[0] == diagnostics[1], label
            for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
                rejected_output = work / (label + suffix)
                rejected = subprocess.run(prefix + [str(consumer), flag, '-o', str(rejected_output)],
                    env=env, cwd=work, capture_output=True, text=True, timeout=60)
                assert rejected.returncode == 1 and not rejected_output.exists(), rejected.stderr
        print('PASS source-hidden library bounds and intrinsic-domain controls', flush=True)

        associated_source = (ROOT / 'tests/semantics/whole_value_generics/associated_values.tk').read_text()
        associated_provider, associated_consumer = associated_source.split('shape Resource', 1)
        associated_provider = associated_provider.replace('trait @Producer', 'pub trait @Producer').replace(
            'shape Provider', 'pub shape Provider').replace('    fn relay', '    pub fn relay')
        provider.write_text(associated_provider + '''
pub fn borrow_forward<T>(&value:T) -> &T <- value {
    auto &local = &value
    return &local
}
''')
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('import provider::{Provider, Producer, borrow_forward}\nshape Resource' +
                            associated_consumer.replace('    return 0',
                                '    auto number = 17:i32\n    auto &reference = &number\n'
                                '    auto &&view = borrow_forward<&i32>(&&reference)\n'
                                '    if view != 17 { return 2 }\n    return 0'))
        output = work / 'source-hidden-associated-borrow'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(output)], timeout=10).returncode == 0
        print('PASS source-hidden associated value and descriptor-preserving borrow', flush=True)

        alias_source = (ROOT / 'tests/semantics/whole_value_generics/alias_values.tk').read_text()
        alias_provider, alias_consumer = alias_source.split('shape Resource', 1)
        alias_provider = re.sub(r'(?m)^(alias|shape|fn) ', r'pub \1 ', alias_provider)
        provider.write_text(alias_provider)
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('import provider::{Slot, Again as Input, relay, borrow}\n'
                            'fn client<T>(cede value:Input<T>) -> Input<T> {\n'
                            'auto local = cede value\nreturn cede local\n}\nshape Resource' +
                            alias_consumer.replace('relay<^Resource>(cede ^owner)',
                                                   'client<^Resource>(cede ^owner)'))
        output = work / 'source-hidden-alias'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(output)], timeout=10).returncode == 0
        print('PASS source-hidden imported alias contract', flush=True)

        chain = (ROOT / 'tests/semantics/whole_value_generics/static_factory_chain.tk').read_text()
        chain_provider, chain_consumer = chain.split('fn roundtrip', 1)
        chain_provider = chain_provider.replace('shape Box<T>', 'pub shape Box<T>')
        chain_provider = re.sub(r'(?m)^(    )fn ', r'\1pub fn ', chain_provider)
        provider.write_text(chain_provider)
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('import provider::{Box as RemoteBox}\nshape Box<T>(unrelated:T)\n'
                            'fn roundtrip' + chain_consumer.replace('Box<T>::', 'RemoteBox<T>::'))
        output = work / 'source-hidden-static-chain'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(output)], timeout=10).returncode == 0
        print('PASS source-hidden renamed static factory chain and cleanup', flush=True)

        reflection = (ROOT / 'tests/semantics/whole_value_generics/reflection_fields.tk').read_text()
        reflection_provider, reflection_main = reflection.split('fn main()', 1)
        provider.write_text(reflection_provider.replace('fn total<', 'pub fn total<')
                                              .replace('fn fill<', 'pub fn fill<'))
        emitted = subprocess.run(prefix + ['--emit-interface', '-c', str(provider),
                                  '-o', str(work / 'provider.o')], env=env, cwd=work,
                                 capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.unlink()
        consumer.write_text('import provider::{total, fill}\n'
                            'shape Pair(left:i32, right:i32)\nfn main()' + reflection_main)
        output = work / 'source-hidden-reflection'
        built = subprocess.run(prefix + [str(consumer), str(work / 'provider.o'),
                                '-o', str(output)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(output)], timeout=10).returncode == 0
        print('PASS source-hidden reflection over caller-owned nominal type', flush=True)


if __name__ == '__main__':
    main()
