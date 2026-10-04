#!/usr/bin/env python3
"""Freeze a complete composite Preview SDK; not a release or Q0 build."""
import argparse,gzip,hashlib,json,os,subprocess,tarfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
BASE='57b0f7dd7d52bdc24c6dde0457803240e5c62e8a'
CHANGED={'bin/toka','lib/toolchain/toka_test.py','lib/toolchain/toka_test_process.py',
         'lib/toolchain/toka_test_report.py','lib/toolchain/toka_package.py','lib/toolchain/toka_evidence.py'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--target',required=True);p.add_argument('--work',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source_sha=subprocess.check_output(['git','-c','core.fsmonitor=false','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    subprocess.run([os.sys.executable,str(ROOT/'tools/scripts/stage_test_i1_sdk.py'),'--archive',str(a.archive),
                    '--target',a.target,'--work',str(a.work)],check=True,timeout=240)
    sdk=a.work/'sdk'/('toka-v0.11.0-'+a.target)
    # Restore every baseline component except the explicitly replaced Preview components.
    # A manager build may have generated caches; they are not part of the frozen SDK.
    baseline={}
    with tarfile.open(a.archive) as original:
        for info in original.getmembers():
            assert not info.issym() and not info.islnk(), 'baseline symbolic links need an explicit manifest policy'
            if not info.isfile():continue
            name=Path(info.name).relative_to('toka-v0.11.0-'+a.target).as_posix()
            baseline[name]=hashlib.sha256(original.extractfile(info).read()).hexdigest()
            if name not in CHANGED:
                path=sdk/name;path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(original.extractfile(info).read());path.chmod(info.mode)
    for path in list(sdk.rglob('*')):
        if path.is_file() and path.relative_to(sdk).as_posix() not in baseline and path.relative_to(sdk).as_posix() not in CHANGED:path.unlink()
    for path in sorted(sdk.rglob('*'),reverse=True):
        if path.is_dir() and not any(path.iterdir()):path.rmdir()
    manifest={path.relative_to(sdk).as_posix():{'sha256':sha(path),'mode':path.stat().st_mode&0o777,
              'source_sha':source_sha if path.relative_to(sdk).as_posix() in CHANGED else BASE,
              'origin':'candidate' if path.relative_to(sdk).as_posix() in CHANGED else 'frozen_base'}
              for path in sorted(sdk.rglob('*')) if path.is_file()}
    for name,expected in baseline.items():
        if name not in CHANGED:assert manifest[name]['sha256']==expected,name
    descriptor={'schema':'toka.preview-sdk-i2c','version':1,'preview':True,'candidate_sha':source_sha,
                'target':a.target,'base_sdk_source_sha':BASE,'base_sdk_archive_sha256':sha(a.archive),
                'composition':'complete SDK with explicitly versioned baseline toolchain and candidate project tools',
                'base_tool_versions':'0.11.0','not_a_public_release':True,'components':manifest}
    (sdk/'preview-sdk.json').write_text(json.dumps(descriptor,indent=2)+'\n')
    a.output.mkdir(parents=True,exist_ok=False)
    archive=a.output/('toka-preview-i2c-'+a.target+'.tar.gz')
    # Deterministic archive headers; original SDK input remains immutable.
    with archive.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as compressed:
        with tarfile.open(fileobj=compressed,mode='w') as package:
            for path in [sdk,*sorted(sdk.rglob('*'))]:
                info=package.gettarinfo(str(path),arcname=path.relative_to(sdk.parent).as_posix())
                info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
                if info.isfile():
                    with path.open('rb') as stream:package.addfile(info,stream)
                else:package.addfile(info)
    (a.output/'sdk-identity.json').write_text(json.dumps(dict(descriptor,archive=archive.name,archive_sha256=sha(archive)),indent=2)+'\n')
    (a.output/'SHA256SUMS').write_text(sha(archive)+'  '+archive.name+'\n')
    print(archive)

if __name__=='__main__':main()
