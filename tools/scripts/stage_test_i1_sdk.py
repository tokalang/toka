#!/usr/bin/env python3
"""Build only the Preview manager into a private original-SDK copy for targeted installed-SDK checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
HASHES = {'linux-x64':'05faa6cf2128f9dc385aa832c33f722e34f5a4f4b98d0613887a3efe79a35635',
          'linux-arm64':'16e8deda50d7983bfa9cffc9f55e8587f4b9b3f4c72c7346a3e82f3c173466f1',
          'macos-arm64':'81e95d01f685f4fbb057fc42b03c376c2646b41bce08398f8f40a76d2cdd7b99'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--target',choices=HASHES,required=True);p.add_argument('--work',type=Path,required=True)
    a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=False)
    assert sha(a.archive)==HASHES[a.target], 'original SDK archive digest mismatch'
    with tarfile.open(a.archive) as archive:archive.extractall(work/'sdk',filter='data')
    sdk=work/'sdk'/('toka-v0.11.0-'+a.target)
    compiler_before=sha(sdk/'bin/tokac');runtime_before=sha(sdk/'lib/sys/toka_rt.o')
    source=work/'manager-source'
    for f in (ROOT/'tools/toka').rglob('*.tk'):
        path=source/f.relative_to(ROOT/'tools/toka');path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,path)
    generated=work/'generated/toka_build';generated.mkdir(parents=True)
    (generated/'release_version.tk').write_text('pub fn release_version() -> string { return string::from("0.11.0") }\n')
    command=[str(sdk/'bin/tokac'),'--workspace-node','toka-tools-v1','--workspace-root',str(source),
             '-I',str(sdk/'lib'),'-I',str(work/'generated'),'-I',str(source),str(source/'src/main.tk'),
             '-o',str(work/'candidate-toka'),'-O3']
    env=dict(os.environ,TOKA_LIB=str(sdk/'lib'))
    # The outer test guard is a validation harness limit, not production supervision.
    r=subprocess.run(command,cwd=work,env=env,capture_output=True,timeout=180)
    (work/'manager-build.stdout').write_bytes(r.stdout);(work/'manager-build.stderr').write_bytes(r.stderr)
    if r.returncode:raise SystemExit('Preview manager build failed; original manager was not replaced')
    shutil.copyfile(work/'candidate-toka',sdk/'bin/toka');(sdk/'bin/toka').chmod(0o755)
    for name in ['toka_test.py','toka_package.py','toka_test_process.py','toka_test_report.py']:
        shutil.copyfile(ROOT/'lib/toolchain'/name,sdk/'lib/toolchain'/name)
    assert sha(sdk/'bin/tokac')==compiler_before and sha(sdk/'lib/sys/toka_rt.o')==runtime_before
    (work/'stage-identity.json').write_text(json.dumps({'result':'pass','stage':'I2-B-preview',
        'base_sdk_revision':'57b0f7dd7d52bdc24c6dde0457803240e5c62e8a','base_sdk_archive_sha256':HASHES[a.target],
        'candidate_sha':subprocess.check_output(['git','-c','core.fsmonitor=false','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'target':a.target,'compiler_sha256':compiler_before,'runtime_sha256':runtime_before,
        'manager_sha256':sha(sdk/'bin/toka'),'runner_sha256':sha(sdk/'lib/toolchain/toka_test.py'),
        'report_sha256':sha(sdk/'lib/toolchain/toka_test_report.py'),
        'supervisor_sha256':sha(sdk/'lib/toolchain/toka_test_process.py'),
        'resolver_sha256':sha(sdk/'lib/toolchain/toka_package.py'),'manager_build_command':command,
        'published_sdk_modified':False,'private_sdk_components_modified':['bin/toka','lib/toolchain/toka_test.py','lib/toolchain/toka_package.py','lib/toolchain/toka_test_process.py','lib/toolchain/toka_test_report.py'],'stable_test_v1':False},indent=2)+'\n')
    print(sdk)


if __name__=='__main__':main()
