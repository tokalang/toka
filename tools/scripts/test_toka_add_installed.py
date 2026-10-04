#!/usr/bin/env python3
"""F01-F05 using the installed manager/helper, without compiler source overrides."""
import argparse,base64,contextlib,io,json,os,subprocess,sys
from pathlib import Path
from unittest.mock import patch

def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sys.path.insert(0,str(sdk/'lib/toolchain'));import toka_package as packages
 from test_toka_add import Add,manifest,library
 assert Path(packages.__file__).resolve().is_relative_to(sdk)
 env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')};env.update(PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'],PYTHONDONTWRITEBYTECODE='1');records=[]
 def run(name,args,expected,registry=None,prepare=None):
  root=out/name;manifest(root);original=(root/'package.tk').read_bytes()
  if prepare:prepare(root)
  process=subprocess.run(['toka','add',*args],cwd=root,env=dict(env,**({'TOKA_REGISTRY_URL':registry} if registry else {})),capture_output=True,timeout=60)
  (root/'stdout').write_bytes(process.stdout);(root/'stderr').write_bytes(process.stderr);assert process.returncode==expected,(name,process.stderr)
  data=json.loads(process.stdout) if '--json' in args else None
  if data and expected==0:
   entry=packages.read_lock(root/'package.lock')[data['alias']];assert data['package_node_id']==packages.package_node_id(entry) and data['content_sha256']==entry.content_sha256
   assert data['archive_sha256']==(None if entry.archive_sha256=='-' else entry.archive_sha256)
  if expected:assert (root/'package.tk').read_bytes()==original and not (root/'package.lock').exists() and (not data or data['result']=='failed')
  r={'name':name,'argv':['toka','add',*args],'exit_code':process.returncode,'report':data};(root/'result.json').write_text(json.dumps(r,indent=2)+'\n');records.append(r);return root,data
 fixture=Add();fixture.setUp();server,thread,url,digest=fixture.registry()
 try:
  root,r=run('F01-human',['dep'],0,url);assert b'0.1.2' in (root/'stdout').read_bytes() and digest[:12].encode() in (root/'stdout').read_bytes()
  root,r=run('F03-json',['dep','--json'],0,url);assert r['requested_version']=='latest' and r['resolved_version']=='0.1.2' and r['archive_sha256']==digest
  root,r=run('F02-missing',['dep:9.9.9','--json'],1,url);assert b'no verified release' in (root/'stderr').read_bytes()
 finally:server.shutdown();server.server_close();thread.join();fixture.tearDown()
 fixture=Add();fixture.setUp();server,thread,url,digest=fixture.registry(True)
 try:
  root,r=run('F02-digest',['dep:0.1.2','--json'],1,url);assert b'does not match' in (root/'stderr').read_bytes()
 finally:server.shutdown();server.server_close();thread.join();fixture.tearDown()
 dep=out/'local-dep';library(dep);root,r=run('F04-local',[str(dep),'--alias','dep','--json'],0);assert r['kind']=='path' and r['resolved_path']==str(dep) and r['resolved_version'] is None
 git=out/'git-dep';library(git)
 for args in [['init','-q'],['add','.'],['-c','user.name=fixture','-c','user.email=fixture@invalid','commit','-qm','fixed package']]:subprocess.run(['git',*args],cwd=git,check=True,capture_output=True)
 rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=git,text=True).strip();root,r=run('F04-git',['Git('+json.dumps(str(git))+', commit='+json.dumps(rev)+')','--alias','dep','--json'],0);assert r['resolved_revision']==rev and r['resolved_version'] is None
 for fault in ('empty','mismatch'):
  root=out/('F05-'+fault);manifest(root);original=(root/'package.tk').read_bytes();real=packages.subprocess.run
  def faulty(*args,**kwargs):
   child=real(*args,**kwargs)
   if fault=='empty':data=b''
   else:
    payload=json.loads(child.stdout);payload['nodes'][0]['package_node_id']='mismatched';data=json.dumps(payload).encode()
   return subprocess.CompletedProcess(child.args,0,data,child.stderr)
  with patch.object(packages.subprocess,'run',faulty):
   try:packages.add_dependency(root/'package.tk',root/'package.lock',root/'.toka',str(dep),'dep')
   except packages.PackageError as e:
    assert 'structured resolution' in str(e) and e.details['worker']['exit_code']==0;record={'name':'F05-'+fault,'evidence_layer':'installed helper consumer fault injection: actual successful resolver worker followed by corrupted output','error':str(e),'details':e.details,'exit_code':1};(root/'result.json').write_text(json.dumps(record,indent=2)+'\n');records.append(record)
   else:raise AssertionError('corrupt successful helper result accepted')
  assert (root/'package.tk').read_bytes()==original and not (root/'package.lock').exists()
 (out/'result.json').write_text(json.dumps({'result':'pass','scenarios':len(records),'records':records,'installed_helper':str(packages.__file__),'source_checkout_required':False},indent=2)+'\n')
if __name__=='__main__':main()
