#!/usr/bin/env python3
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
    with tempfile.TemporaryDirectory(prefix='toka-csv-flat-') as directory:
        work = Path(directory)
        def compile(source, *flags):
            return subprocess.run([str(compiler), '--workspace-node', 'csv-flat',
                                   '--workspace-root', str(ROOT if source.is_relative_to(ROOT) else work),
                                   str(source), *map(str, flags)], cwd=ROOT, env=env,
                                  text=True, capture_output=True, timeout=60)
        for source in (ROOT / 'tests/pass/g14_stdx_csv_corpus_test.tk',
                       ROOT / 'tests/semantics/csv_flat_records/records.tk',
                       ROOT / 'tests/pass/g14_stdx_csv_test.tk',
                       ROOT / 'tests/semantics/csv_flat_records/stream_blocks.tk'):
            normal = compile(source, '--check-only')
            shadow = compile(source, '--check-only', '--non-call-transfer-shadow=json')
            assert normal.returncode == shadow.returncode == 0, (source, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr
            binary = work / source.stem
            built = compile(source, '-o', binary)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, (source.name, result.returncode, result.stdout, result.stderr)
            print('PASS ' + source.stem, flush=True)
        source = work / 'escape.tk'
        source.write_text('import stdx/data/csv::{parse_records}\n'
                          'fn escaped()->&string {auto parsed=parse_records("a,b\\r\\n")\n'
                          'auto records=parsed.unwrap()\nreturn records.field(0:usize,0:usize)}\n'
                          'fn main()->i32 {return 0}\n')
        for flag, suffix in (('-c', '.o'), ('--emit-llvm', '.ll')):
            artifact = work / ('escape' + suffix)
            result = compile(source, flag, '-o', artifact)
            assert result.returncode == 1 and 'E0455' in result.stderr, result.stderr
            assert not artifact.exists()
        print('PASS field view cannot escape local document', flush=True)

if __name__ == '__main__':
    main()
