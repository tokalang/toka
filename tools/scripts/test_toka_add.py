#!/usr/bin/env python3
"""I3 add resolution controls with verified package locks and fail-closed reports."""
import contextlib,http.server,io,json,os,subprocess,sys,tarfile,tempfile,threading,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'lib/toolchain'))
import toka_package as packages

def manifest(root,deps=''):
 root.mkdir(parents=True,exist_ok=True);(root/'package.tk').write_text('pub const PACKAGE=(name="add",version="1.0.0",dependencies=('+deps+'))\n')
def library(root,alias='dep'):
 manifest(root);(root/'lib/official').mkdir(parents=True);(root/'lib/official'/ (alias+'.tk')).write_text('pub fn answer()->i32 { return 42 }\n')

class Add(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory(prefix='toka-add-');self.base=Path(self.tmp.name);self.root=self.base/'project';manifest(self.root)
 def tearDown(self):self.tmp.cleanup()
 def add(self,spec,alias=None):return packages.add_dependency(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',spec,alias)
 def test_local_lock_identity_has_no_fake_version(self):
  dep=self.base/'dep';library(dep);r=self.add('../dep');entry=packages.read_lock(self.root/'package.lock')['dep']
  self.assertEqual(r['kind'],'path');self.assertIsNone(r['resolved_version']);self.assertEqual(r['resolved_path'],str(dep.resolve()));self.assertIsNone(r['archive_sha256']);self.assertEqual(r['package_node_id'],packages.package_node_id(entry));self.assertEqual(r['content_sha256'],packages.tree_sha256(dep))
 def test_git_fixed_revision_identity(self):
  dep=self.base/'git-dep';library(dep)
  for args in [['init','-q'],['add','.'],['-c','user.name=fixture','-c','user.email=fixture@invalid','commit','-qm','fixed package']]:subprocess.run(['git',*args],cwd=dep,check=True,capture_output=True)
  rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=dep,text=True).strip();r=self.add('Git('+json.dumps(str(dep))+', commit='+json.dumps(rev)+')','dep');self.assertEqual(r['kind'],'git');self.assertEqual(r['resolved_revision'],rev);self.assertIsNone(r['resolved_version']);self.assertIsNone(r['resolved_path'])
 def test_resolution_failure_restores_manifest_and_lock(self):
  original=(self.root/'package.tk').read_bytes();old=b'toka-lock-v1\n';(self.root/'package.lock').write_bytes(old)
  with self.assertRaises(packages.PackageError):self.add('../absent')
  self.assertEqual((self.root/'package.tk').read_bytes(),original);self.assertEqual((self.root/'package.lock').read_bytes(),old)
 def test_missing_success_result_fails_and_rolls_back(self):
  dep=self.base/'dep';library(dep);original=(self.root/'package.tk').read_bytes();run=packages.subprocess.run
  def missing(*args,**kwargs):
   worker=run(*args,**kwargs);return subprocess.CompletedProcess(worker.args,worker.returncode,b'',worker.stderr)
  with patch.object(packages.subprocess,'run',missing):
   with self.assertRaisesRegex(packages.PackageError,'structured resolution result'):self.add('../dep')
  self.assertEqual((self.root/'package.tk').read_bytes(),original);self.assertFalse((self.root/'package.lock').exists())
 def test_mismatched_success_result_fails(self):
  dep=self.base/'dep';library(dep);run=packages.subprocess.run
  def mismatch(*args,**kwargs):
   worker=run(*args,**kwargs);data=json.loads(worker.stdout);data['nodes'][0]['package_node_id']='invalid';return subprocess.CompletedProcess(worker.args,0,json.dumps(data).encode(),worker.stderr)
  with patch.object(packages.subprocess,'run',mismatch):
   with self.assertRaisesRegex(packages.PackageError,'structured resolution result'):self.add('../dep')
 def registry(self,bad_digest=False):
  dep=self.base/'published';library(dep);archive=self.base/'dep-0.1.2.tar.gz'
  with tarfile.open(archive,'w:gz') as t:
   for p in sorted(dep.rglob('*')):
    if p.is_file():t.add(p,arcname=str(p.relative_to(dep)))
  digest=packages.file_sha256(archive);content=packages.tree_sha256(dep)
  class Handler(http.server.BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(self):
    if self.path=='/catalog.json':
     body=json.dumps({'packages':[{'name':'dep','installable':True,'latest_version':'0.1.2','versions':[{'version':'0.1.2','tarball_url':base+'/dep.tar.gz','sha256':'0'*64 if bad_digest else digest}]}]}).encode()
    elif self.path=='/dep.tar.gz':body=archive.read_bytes()
    else:self.send_error(404);return
    self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);base='http://127.0.0.1:'+str(server.server_port);thread=threading.Thread(target=server.serve_forever);thread.start();return server,thread,base,digest
 def test_registry_latest_is_verified_version(self):
  server,thread,base,digest=self.registry()
  try:
   with patch.dict(os.environ,{'TOKA_REGISTRY_URL':base}):r=self.add('dep')
   self.assertEqual(r['requested'],'dep');self.assertEqual(r['requested_version'],'latest');self.assertEqual(r['resolved_version'],'0.1.2');self.assertEqual(r['archive_sha256'],digest)
  finally:server.shutdown();server.server_close();thread.join()
 def test_bad_digest_never_reports_success(self):
  server,thread,base,digest=self.registry(True)
  try:
   with patch.dict(os.environ,{'TOKA_REGISTRY_URL':base}):
    with self.assertRaises(packages.PackageError):self.add('dep:0.1.2')
   self.assertFalse((self.root/'package.lock').exists())
  finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':unittest.main()
