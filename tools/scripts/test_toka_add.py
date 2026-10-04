#!/usr/bin/env python3
"""I3 add resolution controls with verified package locks and fail-closed reports."""
import base64,contextlib,http.server,io,json,os,subprocess,sys,tarfile,tempfile,threading,unittest
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
 def test_invalid_manifest_encoding_is_configuration_error(self):
  original=b'\xff\r\n';(self.root/'package.tk').write_bytes(original)
  with self.assertRaises(packages.PackageConfigurationError):self.add('../dep')
  self.assertEqual((self.root/'package.tk').read_bytes(),original)
 def test_invalid_lock_encoding_preserves_bytes_and_worker_error(self):
  dep=self.base/'dep';library(dep);original=(self.root/'package.tk').read_bytes();old=b'toka-lock-v1\r\n\xff\x00';(self.root/'package.lock').write_bytes(old)
  with self.assertRaises(packages.PackageConfigurationError) as caught:self.add('../dep')
  self.assertIn(b'package.lock must be valid UTF-8',base64.b64decode(caught.exception.details['worker']['stderr_base64']))
  self.assertEqual(caught.exception.details['worker_report']['errors'][0]['category'],'configuration_error')
  self.assertEqual((self.root/'package.tk').read_bytes(),original);self.assertEqual((self.root/'package.lock').read_bytes(),old)
 def test_field_lookup_ignores_comments_and_strings_and_preserves_offsets(self):
  dep=self.base/'dep';library(dep)
  for prefix in ['// dependencies=()\n','// 中文 dependencies=()\r\n','pub const NOTE="dependencies=() \\"quoted\\"";\n']:
   with self.subTest(prefix=prefix):
    manifest(self.root);original=prefix+(self.root/'package.tk').read_text();(self.root/'package.tk').write_text(original);(self.root/'package.lock').unlink(missing_ok=True)
    result=self.add('../dep');self.assertEqual(result['result'],'added');updated=(self.root/'package.tk').read_bytes().decode();self.assertTrue(updated.startswith(prefix));self.assertEqual(updated.replace('\n        dep = "../dep",','',1),original)
 def test_all_worker_validation_errors_keep_raw_details(self):
  dep=self.base/'dep';library(dep);run=packages.subprocess.run
  for fault in ('missing-alias','content','bad-lock'):
   with self.subTest(fault=fault):
    manifest(self.root);original=(self.root/'package.tk').read_bytes();(self.root/'package.lock').unlink(missing_ok=True)
    def corrupt(*args,**kwargs):
     child=run(*args,**kwargs)
     if fault=='missing-alias':
      (self.root/'package.lock').write_text(packages.LOCK_HEADER+'\n');body=json.dumps({'schema':'toka.resolve-report','version':1,'nodes':[]}).encode()
     else:
      body=child.stdout
      if fault=='content':(dep/'lib/official/dep.tk').write_text('changed\n')
      else:(self.root/'package.lock').write_bytes(b'\xff')
     return subprocess.CompletedProcess(child.args,0,body,child.stderr)
    with patch.object(packages.subprocess,'run',corrupt):
     with self.assertRaises(packages.PackageError) as caught:self.add('../dep')
    self.assertEqual(caught.exception.details['worker']['exit_code'],0);self.assertIn('stdout_base64',caught.exception.details['worker']);self.assertEqual((self.root/'package.tk').read_bytes(),original);self.assertFalse((self.root/'package.lock').exists())
 def test_rollback_failure_does_not_mask_worker_and_restores_other_file(self):
  old=b'toka-lock-v1\r\n';(self.root/'package.lock').write_bytes(old);real=packages.atomic_write
  def fail_restore(path,content):
   if path==self.root/'package.tk' and isinstance(content,bytes):raise PermissionError(13,'injected restore failure')
   return real(path,content)
  with patch.object(packages,'atomic_write',fail_restore):
   with self.assertRaises(packages.PackageError) as caught:self.add('../absent')
  self.assertIn('dependency resolution failed',str(caught.exception));self.assertEqual(caught.exception.details['worker']['exit_code'],1);self.assertEqual(caught.exception.details['rollback_errors'][0]['errno'],13);self.assertEqual((self.root/'package.lock').read_bytes(),old)
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
