#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ('g07_for_iterators', 'g08_for_alias_binding', 'g08_for_alias_generic_clone',
         'g08_for_alias_place_iterator_vec_ref', 'g08_handle_grammar_parser_matrix',
         'g08_handle_grammar_valid_matrix', 'g08_vec_payload_borrow_views')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / 'bin/tokac'
    env = dict(os.environ, TOKA_LIB=str(ROOT / 'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-vec-reference-gate-') as temp:
        work = Path(temp)
        def check(source, *flags):
            return subprocess.run([str(compiler), '--workspace-node', 'vec-reference',
                                   '--workspace-root', str(ROOT if source.is_relative_to(ROOT) else work),
                                   str(source), *map(str, flags)], cwd=ROOT, env=env,
                                  capture_output=True, text=True, timeout=60)
        sources = [ROOT / 'tests/pass' / (n + '.tk') for n in CASES]
        sources.append(ROOT / 'tests/semantics/reference_domains/vec_growth.tk')
        for source in sources:
            normal = check(source, '--check-only')
            shadow = check(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0, (source, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            binary = work / source.stem
            built = check(source, '-o', binary)
            assert built.returncode == 0, built.stderr
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert ran.returncode == 0, (source, ran.stdout, ran.stderr)
            print('PASS ' + source.stem, flush=True)
        negative = {
            'escape': ('fn escape()->Vec<&i32>{auto owner=7:i32\nauto v#=Vec<&i32>::new()\nv=v#.appended(&owner)\nreturn cede v}\nfn main()->i32{return 0}', 'E0455'),
            'readonly': ('fn main()->i32{auto owner=7:i32\nauto v#=Vec<&i32>::new()\nv=v#.appended(&owner)\nfor auto &x in v {x=9}\nreturn 0}', 'E04572'),
            'duplicate': ('fn main()->i32{auto owner=7:i32\nauto v#=Vec<&i32>::new()\nauto result=v#.appended(&owner)\nauto again=v#.appended(&owner)\nreturn 0}', 'E0438'),
        }
        rejects = []
        for name, (body, code) in negative.items():
            source = work / (name + '.tk')
            source.write_text('import std/vec::{Vec}\n' + body + '\n')
            rejects.append((source, code))
        rejects += [(ROOT / 'tests/fail/for_alias_removes_morphology.tk', 'E04643'),
                    (ROOT / 'tests/fail/morphology_raw_extendable_vec_borrowed.tk', 'E0621')]
        for source, code in rejects:
            output = work / (source.stem + '.o')
            normal = check(source, '-c', '-o', output)
            shadow = check(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 1, (source, normal.stderr, shadow.stderr)
            assert code in normal.stderr and normal.stderr == shadow.stderr, (source, normal.stderr)
            assert not output.exists()
            print('PASS rejection ' + source.stem, flush=True)


if __name__ == '__main__':
    main()
