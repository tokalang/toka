#!/usr/bin/env python3
"""Independent regression for an empty closure returned as a thin fn."""
import argparse,json,os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',required=True);args=parser.parse_args()
    compiler=Path(args.build_dir).resolve()/'bin/tokac';env=dict(os.environ,TOKA_LIB=str(ROOT/'lib'))
    source=ROOT/'tests/semantics/integration_net/thin_return.tk'
    with tempfile.TemporaryDirectory(prefix='toka-empty-fn-return-') as directory:
        work=Path(directory)
        def run(path,*flags):
            scope=ROOT if path.is_relative_to(ROOT) else work
            return subprocess.run([str(compiler),'--workspace-node','fn-return','--workspace-root',str(scope),str(path),*map(str,flags)],cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
        normal=run(source,'--check-only');shadow=run(source,'--check-only','--non-call-transfer-shadow=json')
        assert normal.returncode==shadow.returncode==0 and normal.stderr==shadow.stderr,(normal.stderr,shadow.stderr)
        json.loads(shadow.stdout)
        binary=work/'positive';built=run(source,'-o',binary)
        assert built.returncode==0,built.stderr
        assert subprocess.run([str(binary)],timeout=10).returncode==0
        ir=work/'positive.ll';emitted=run(source,'--emit-llvm','-o',ir)
        assert emitted.returncode==0,emitted.stderr
        body=re.search(r'^define [^\n]* @make\(\) \{.*?^\}',ir.read_text(),re.S|re.M)
        assert body and '__toka_empty_fn_environment' in body[0] and 'ret.cast.temp' not in body[0],body[0] if body else 'missing make'
        negative=work/'capture.tk'
        negative.write_text('fn escaped() -> fn() -> i32 {\nauto value = 7:i32\nreturn { [copy value] => value }:fn() -> i32\n}\nfn main() -> i32 { return 0 }\n')
        for mode,suffix in [('-c','.o'),('--emit-llvm','.ll')]:
            output=work/('capture'+suffix);failed=run(negative,mode,'-o',output)
            assert failed.returncode==1 and not output.exists(),failed.stderr
        print('PASS direct/branched/unsafe empty fn return; stable carrier; capturing escape rejected')
if __name__=='__main__':main()
