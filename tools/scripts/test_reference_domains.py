#!/usr/bin/env python3
"""Top-level domains: no implicit Copy/ownership and no hidden-source drift."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-reference-domains-') as directory:
        work = Path(directory)
        provider = work / 'slot.tk'
        provider.write_text((ROOT / 'tests/semantics/reference_domains/slot.tk').read_text())
        prefix = [str(compiler), '--workspace-node', 'reference-domains',
                  '--workspace-root', str(work), '-I', str(work)]
        def compile(source, *flags):
            return subprocess.run(prefix + [str(source), *map(str, flags)],
                                  cwd=work, env=env, capture_output=True,
                                  text=True, timeout=60)
        positive = (ROOT / 'tests/semantics/reference_domains/slot_use.tk').read_text()
        negatives = {
            'unused-domain-call': ('import slot::{Slot}\nfn main()->i32 { auto v=7:i32\nauto s=Slot<&i32>(value=&v)\ns.consume(&v)\nreturn 0 }', 'E0621'),
            'nonref-mismatch': ('import slot::{non_reference}\nfn main()->i32 { auto v=7:i32\nreturn non_reference<&i32>(&v) }', 'E0621'),
            'ref-mismatch': ('import slot::{reference}\nfn main()->i32 { return reference<i32>(7) }', 'E0621'),
            'unknown': ('import slot::{non_reference}\nfn main()->i32 { return non_reference<Missing>(0) }', None),
            'unknown-referent': ('import slot::{reference}\nfn main()->i32 {auto v=0:i32\nreturn reference<&Missing>(&v)}', None),
            'const': ('fn bad<const N:i32>()->i32\nwhere:\n N: morphology non_reference\n{ return 0 }\nfn main()->i32 {return bad<1>()}', None),
            'intersection': ('fn both<T>(v:T)->i32\nwhere:\n T: morphology reference_only\n T: morphology non_reference\n{ return 0 }\nfn main()->i32 {auto v=7:i32\nreturn both<&i32>(&v)}', 'E0621'),
            'illegal-chain': ('import slot::{non_reference}\nfn main()->i32 {return non_reference<*^i32>(0)}', None),
            'reference-not-consuming': ('import slot::{reference}\nfn take<T>(cede v:T)\nwhere:\n T: morphology reference_only\n{cede v}\nfn main()->i32 {auto v=7:i32\ntake<&i32>(&v)\nreturn 0}', 'E04661'),
            'slot-escape': ('import slot::{Slot}\nfn escaped()->Slot<&i32> {auto owner=7:i32\nreturn Slot<&i32>(value=&owner)}\nfn main()->i32{return 0}', 'E0455'),
            'readonly-slot': ('import slot::{Slot}\nfn main()->i32 {auto owner=7:i32\nauto s#=Slot<&i32>(value=&owner)\ns.value=9\nreturn 0}', None),
            'domain-not-copy': ('shape Resource(id:i32)\nimpl Resource@Encap {fn drop(self#) {}}\nfn copy<T>(value:T)->T\nwhere:\n T: morphology non_reference\n{return value}\nfn main()->i32 {auto r=Resource(id=1)\nauto other=copy<Resource>(r)\nreturn 0}', None),
        }
        emitted = compile(provider, '--emit-interface', '-c', '-o', work / 'slot.o')
        assert emitted.returncode == 0, emitted.stderr
        original_tki = (work / 'slot.tki').read_text()
        assert 'morphology reference_only' in original_tki and 'morphology non_reference' in original_tki
        diagnostics = {}
        for hidden in (False, True):
            if hidden: provider.unlink()
            user = work / 'use.tk'
            user.write_text(positive)
            output = work / ('hidden' if hidden else 'visible')
            result = compile(user, work / 'slot.o', '-o', output)
            assert result.returncode == 0, result.stderr
            assert subprocess.run([str(output)], timeout=10).returncode == 0
            for name, (code, diagnostic) in negatives.items():
                source = work / (name + '.tk')
                source.write_text(code + '\n')
                artifact = work / (name + '.o')
                normal = compile(source, '-c', '-o', artifact)
                shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
                assert normal.returncode == shadow.returncode == 1, (hidden, name, normal.stderr, shadow.stderr)
                assert normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
                assert not artifact.exists() and 'error[' in normal.stderr
                if diagnostic: assert diagnostic in normal.stderr, (name, normal.stderr)
                if hidden: assert diagnostics[name] == normal.stderr, (name, diagnostics[name], normal.stderr)
                else: diagnostics[name] = normal.stderr
            print('PASS domains ' + ('source-hidden' if hidden else 'source-visible'), flush=True)
        # The new source-level contract must not be consumed as an old cache.
        assert '0.9.9-23' in original_tki
        (work / 'slot.tki').write_text(original_tki.replace('0.9.9-23', '0.9.9-22'))
        user.write_text(positive)
        rejected = compile(user, '--check-only')
        assert rejected.returncode == 1, rejected.stderr
        print('PASS old interface rejected', flush=True)


if __name__ == '__main__':
    main()
