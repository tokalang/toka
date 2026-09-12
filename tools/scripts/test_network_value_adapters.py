#!/usr/bin/env python3
import argparse,json,os,platform,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',required=True);args=parser.parse_args()
    if platform.system()!='Darwin': raise SystemExit('This gate qualifies the Darwin adapter only')
    compiler=Path(args.build_dir).resolve()/'bin/tokac';fixtures=ROOT/'tests/semantics/integration_net'
    env=dict(os.environ,TOKA_LIB=str(ROOT/'lib'))
    with tempfile.TemporaryDirectory(prefix='toka-net-adapter-') as directory:
        work=Path(directory);obj=work/'abi.o'
        subprocess.run(['clang','-c',str(fixtures/'abi.c'),'-o',str(obj)],check=True)
        for name,extra in [('abi',(str(obj),)),('loopback',())]:
            source=fixtures/(name+'.tk');prefix=[str(compiler),'--workspace-node','network-adapters','--workspace-root',str(ROOT),str(source)]
            normal=subprocess.run(prefix+['--check-only'],cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
            shadow=subprocess.run(prefix+['--check-only','--non-call-transfer-shadow=json'],cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
            assert normal.returncode==shadow.returncode==0 and normal.stderr==shadow.stderr,(normal.stderr,shadow.stderr)
            json.loads(shadow.stdout)
            binary=work/name
            built=subprocess.run(prefix+list(extra)+['-o',str(binary)],cwd=ROOT,env=env,text=True,capture_output=True,timeout=60)
            assert built.returncode==0,built.stderr
            ran=subprocess.run([str(binary)],timeout=20)
            assert ran.returncode==0,(name,ran.returncode)
            print('PASS '+name,flush=True)
if __name__=='__main__': main()
