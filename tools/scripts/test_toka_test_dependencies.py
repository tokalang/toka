#!/usr/bin/env python3
"""Installed-SDK graph/lock/identity observations; fixtures never replace P01."""
import argparse,contextlib,fcntl,hashlib,http.server,json,os,shutil,subprocess,sys,tarfile,threading,time
from pathlib import Path
from unittest.mock import patch
from test_toka_test_i2c_batch import Batch,manifest,source,sha
from toka_test_lock_contract import LOCK_CODES, require_lock_failure
OK='fn main() -> i32 { return 0 }\n'
REG='import official/reg::{answer}\nfn main() -> i32 { if answer()!=42 { return 1 } return 0 }\n'
class Dependencies(Batch):
 def invoke(self,name,root,args=(),code=0,rows=(),env=None,active=None,extra=None):
  before=(root/'package.lock').read_bytes() if (root/'package.lock').is_file() else None
  command=[str((active or self.sdk)/'bin/toka'),'test','--json',*args];r=subprocess.run(command,cwd=root,env=env or self.env,capture_output=True,timeout=45)
  report=self.decode_save(name,root,command,r,rows=rows)
  after=(root/'package.lock').read_bytes() if (root/'package.lock').is_file() else None
  checks={'exit_code':r.returncode==code,'lock_unchanged':before==after}
  if code==2 and name.startswith(('P04','P05','P08','P11','P14a')):
   checks.update(all_not_run=all(t['result']=='not_run' and t['phases']['compile_link']['state']=='not_started' for t in report['tests']),selected=report['summary']['total']==1)
  if extra:
   try:checks.update(extra(report))
   except (AssertionError,KeyError,TypeError,ValueError) as failure:
    record=self.results[-1];checks['consumer_contract']=False
    record.update(checks=checks,contract_pass=False,consumer_contract_error={'phase':'consumer_P05_validation','message':str(failure),
      'stdout':str(self.output/name/'stdout'),'stderr':str(self.output/name/'stderr'),
      'next_check':'Compare the fixed expected lock code and context facts against context-result.json; do not infer from message text.'})
    (self.output/name/'result.json').write_text(json.dumps(record,indent=2)+'\n');raise
  if before is not None:(self.output/name/'lock.before').write_bytes(before)
  if after is not None:(self.output/name/'lock.after').write_bytes(after)
  record=self.results[-1];record.update(checks=checks,contract_pass=all(checks.values()),expected_exit_code=code,lock_before_sha256=hashlib.sha256(before).hexdigest() if before is not None else None,lock_after_sha256=hashlib.sha256(after).hexdigest() if after is not None else None)
  (self.output/name/'result.json').write_text(json.dumps(record,indent=2)+'\n');return report
 def notes(self,name,data): (self.output/name/'witness.json').write_text(json.dumps(data,indent=2)+'\n')
 def fetch(self,root,env=None):
  p=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=env or self.env,capture_output=True,timeout=45)
  folder=root/'fetch-evidence';folder.mkdir();(folder/'stdout').write_bytes(p.stdout);(folder/'stderr').write_bytes(p.stderr);assert p.returncode==0,p.stderr
 def variant(self,name):
  root=self.output/('variant-'+name);(root/'bin').mkdir(parents=True);(root/'lib').mkdir()
  shutil.copyfile(self.sdk/'bin/toka',root/'bin/toka');(root/'bin/toka').chmod(0o755);(root/'bin/tokac').symlink_to(self.sdk/'bin/tokac')
  for p in (self.sdk/'lib').iterdir():
   if p.name=='toolchain':shutil.copytree(p,root/'lib/toolchain')
   else:(root/'lib'/p.name).symlink_to(p,target_is_directory=p.is_dir())
  return root
 def registry(self):
  location=self.output/'registry';location.mkdir();self.tarballs={};self.content={}
  for version,answer in [('1.0.0',42),('2.0.0',99)]:
   package=location/version;manifest(package);source(package,'lib/official/reg.tk','pub fn answer()->i32 { return '+str(answer)+' }\n')
   archive=location/(version+'.tar.gz')
   with tarfile.open(archive,'w:gz') as tar:
    for p in sorted(package.rglob('*')):
     if p.is_file():tar.add(p,arcname=p.relative_to(package).as_posix())
   self.tarballs[version]=archive;self.content[version]=self.packages.tree_sha256(package)
  self.requests=[];self.latest='1.0.0';self.catalog_hash=None;self.bad_download=False
  owner=self
  class Handler(http.server.BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(h):
    owner.requests.append({'path':h.path,'monotonic_ns':time.monotonic_ns()})
    if h.path=='/catalog.json':
     versions=[{'version':v,'tarball_url':owner.url+'/'+v+'.tar.gz','sha256':owner.catalog_hash or sha(owner.tarballs[v])} for v in ('1.0.0','2.0.0')]
     body=json.dumps({'packages':[{'name':'reg','version':owner.latest,'latest_version':owner.latest,'installable':True,'versions':versions}]}).encode()
    elif h.path.lstrip('/')[:-7] in owner.tarballs:
     body=b'corrupt-download' if owner.bad_download else owner.tarballs[h.path.lstrip('/')[:-7]].read_bytes()
    else:h.send_error(404);return
    h.send_response(200);h.end_headers();h.wfile.write(body)
  self.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);self.url='http://127.0.0.1:'+str(self.server.server_port);self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start();self.net=dict(self.env,TOKA_REGISTRY_URL=self.url)
 def locked(self,name):
  root=self.project(name,[('a_test.tk',REG)]);manifest(root,'reg="reg:latest",')
  entry=self.packages.LockEntry('reg','registry','reg','1.0.0',sha(self.tarballs['1.0.0']),self.content['1.0.0'],[])
  (root/'package.lock').write_text(self.packages.encode_lock({'reg':entry}));return root
 def nodes(self,root):return {s.partition('=')[0]:s.partition('=')[2] for s in self.packages.compiler_node_mappings(root/'package.lock')}
 def flags(self,r,root):
  raw=json.loads((Path(r['artifact_root'])/'preview.json').read_text());return {'lock_hash':r['identity']['lock_sha256']==sha(root/'package.lock'),'nodes':all(item in raw['compiler_flags'] for item in self.packages.compiler_node_mappings(root/'package.lock')),'identity_complete':r['identity']['status']=='complete'}
 def error_fact(self,r,root,scope=None,actual=None):
  fact=r['errors'][0].get('dependency',{});entry=self.packages.read_lock(root/'package.lock')['reg'];expected=self.nodes(root)['reg']
  checks={'infrastructure_error':r['result']=='infrastructure_error','dependency_node':fact.get('package_node_id')==expected,'dependency_version':fact.get('resolved')=='1.0.0'}
  if scope is not None:checks.update(integrity_scope=fact.get('integrity',{}).get('scope')==scope,expected_digest=fact.get('integrity',{}).get('expected_sha256')==(entry.content_sha256 if scope in ('installed_content','extracted_content') else entry.archive_sha256),actual_digest=fact.get('integrity',{}).get('actual_sha256')==actual)
  return checks
 def run(self):
  self.registry()
  try:
   dep=self.work/'local-dep';leaf=self.work/'leaf';manifest(leaf);source(leaf,'lib/official/leaf.tk','pub fn value()->i32 { return 42 }\n');manifest(dep,'leaf="../leaf",');source(dep,'lib/official/dep.tk','import official/leaf::{value}\npub fn answer()->i32 { return value() }\n')
   root=self.project('local-graph',[('a_test.tk','import official/dep::{answer}\nimport std/env\nimport std/io::{println}\nfn main()->i32 { println("{}",env::current_dir())\n if answer()!=42 { return 1 } return 0 }\n')]);manifest(root,'dep="../local-dep",');self.fetch(root)
   def local(r):
    flags=self.flags(r,root);flags.update(cwd=Path(r['tests'][0]['logs']['run_stdout']).read_text().strip()==str(root),transitive_graph=self.packages.read_lock(root/'package.lock')['dep'].dependencies==['leaf']);return flags
   self.invoke('P02-before',root,rows=['P02'],extra=local);(root/'tests/moved').mkdir();(root/'tests/a_test.tk').rename(root/'tests/moved/b_test.tk');self.invoke('P02-moved',root,rows=['P02'],extra=local)
   root=self.locked('offline');self.invoke('seed-cache',root,env=self.net,extra=lambda r:self.flags(r,root));offline=dict(self.net,TOKA_OFFLINE='1')
   for variant in ('installed','archive-only'):
    if variant=='archive-only':shutil.rmtree(root/'.toka/packages/reg-1.0.0')
    count=len(self.requests);name='P03-'+variant;self.invoke(name,root,rows=['P03'],env=offline,extra=lambda r:{**self.flags(r,root),'no_network':len(self.requests)==count,'cache_hash':self.packages.tree_sha256(root/'.toka/packages/reg-1.0.0')==self.content['1.0.0']});self.notes(name,{'requests_before':count,'requests_after':len(self.requests),'archive_sha256':sha(self.tarballs['1.0.0']),'content_sha256':self.content['1.0.0']})
   root=self.locked('offline-missing');count=len(self.requests);self.invoke('P04',root,code=2,rows=['P04'],env=offline,extra=lambda r:{**self.error_fact(r,root),'no_network':len(self.requests)==count})
   for kind in ('missing','malformed','stale'):
    root=self.locked('invalid-lock-'+kind)
    if kind=='missing':(root/'package.lock').unlink()
    elif kind=='malformed':(root/'package.lock').write_bytes(b'invalid lock\n')
    else:manifest(root,'reg="reg:2.0.0",')
    count=len(self.requests)
    def lock_checks(r,kind=kind):
     require_lock_failure(r,LOCK_CODES[kind],2)
     return {'lock_error_code':r['errors'][0]['code']==LOCK_CODES[kind],
             'lock_error_phase':r['errors'][0]['phase']=='context',
             'configuration_error':r['result']=='configuration_error',
             'repair_hint_readable':bool(r['errors'][0]['message'].strip()),
             'no_network':len(self.requests)==count}
    self.invoke('P05-'+kind,root,code=2,rows=['P05'],env=self.net,extra=lock_checks)
   self.latest='2.0.0';root=self.locked('pinned-old');count=len(self.requests);self.invoke('P07',root,rows=['P07'],env=self.net,extra=lambda r:{**self.flags(r,root),'pinned_version':self.packages.read_lock(root/'package.lock')['reg'].resolved=='1.0.0','cache_hash':self.packages.tree_sha256(root/'.toka/packages/reg-1.0.0')==self.content['1.0.0'],'download_old':any(x['path']=='/1.0.0.tar.gz' for x in self.requests[count:]),'never_new':all(x['path']!='/2.0.0.tar.gz' for x in self.requests[count:])});self.notes('P07',{'catalog_latest':self.latest,'requests':self.requests[count:]})
   for kind in ('installed','archive','download','catalog','extracted'):
    root=self.locked('integrity-'+kind);self.catalog_hash=None;self.bad_download=False;env=self.net
    if kind in ('installed','archive'):self.invoke('seed-'+kind,root,env=self.net)
    if kind=='installed':
     source(root,'.toka/packages/reg-1.0.0/lib/official/reg.tk','corrupt contents');scope='installed_content';actual=self.packages.tree_sha256(root/'.toka/packages/reg-1.0.0')
    elif kind=='archive':
     shutil.rmtree(root/'.toka/packages/reg-1.0.0');(root/'.toka/cache/archives'/(sha(self.tarballs['1.0.0'])+'.tar.gz')).write_bytes(b'corrupt-cache');scope='cached_archive';actual=hashlib.sha256(b'corrupt-cache').hexdigest();env=offline
    elif kind=='download':self.bad_download=True;scope='downloaded_archive';actual=hashlib.sha256(b'corrupt-download').hexdigest()
    elif kind=='catalog':self.catalog_hash='a'*64;scope='catalog_archive';actual=self.catalog_hash
    else:
     lock=self.packages.read_lock(root/'package.lock');lock['reg'].content_sha256='b'*64;(root/'package.lock').write_text(self.packages.encode_lock(lock));scope='extracted_content';actual=self.content['1.0.0']
    self.invoke('P08-'+kind,root,code=2,rows=['P08'],env=env,extra=lambda r,root=root,scope=scope,actual=actual:self.error_fact(r,root,scope,actual));self.notes('P08-'+kind,{'scope':scope,'actual_sha256':actual})
   self.catalog_hash=None;self.bad_download=False
   self.provenance(dep)
   self.identities(dep)
   root=self.project('lock-wait',[('a_test.tk',OK)]);(root/'.toka').mkdir();fd=(root/'.toka/test-context.lock').open('w');fcntl.flock(fd,fcntl.LOCK_EX)
   try:
    before=time.monotonic();r=self.invoke('P14a',root,['--compile-timeout-ms','2000'],code=2,rows=['P14a'],extra=lambda r:{'infrastructure_error':r['result']=='infrastructure_error','effective_budget':r['timeouts']['compile_ms']==2000,'lock_wait_ms':2000<=r.get('dependencies',{}).get('lock_wait_ms',-1)<5000});self.notes('P14a',{'elapsed_ms':(time.monotonic()-before)*1000,'held_lock':str(root/'.toka/test-context.lock'),'compile_ms':2000})
   finally:fd.close()
  finally:self.server.shutdown();self.server.server_close();self.thread.join(timeout=5)
  for variant in self.output.glob('variant-*'):
   if (variant/'bin/tokac').is_symlink():(variant/'bin/tokac').unlink()
   for path in (variant/'lib').iterdir() if (variant/'lib').is_dir() and not (variant/'lib').is_symlink() else []:
    if path.is_symlink():path.unlink()
   if (variant/'lib').is_symlink():(variant/'lib').unlink()
  records=[{'name':r['name'],'rows':r['rows'],'checks':r['checks'],'contract_pass':r['contract_pass'],'exit_code':r['exit_code']} for r in self.results if r['rows']]
  (self.output/'result.json').write_text(json.dumps({'schema':'toka.dependencies-acceptance','contract_pass':all(r['contract_pass'] for r in records),'records':records,'requests':self.requests},indent=2)+'\n')
 def provenance(self,dep):
  root=self.project('provenance',[('a_test.tk',OK)]);manifest(root,'dep="../local-dep",');self.fetch(root)
  variant=self.output/'variant-provenance';(variant/'bin').mkdir(parents=True);(variant/'lib').symlink_to(self.sdk/'lib',target_is_directory=True);shutil.copyfile(self.sdk/'bin/toka',variant/'bin/toka');(variant/'bin/toka').chmod(0o755)
  files={'user':root/'tests/a_test.tk','dependency':dep/'lib/official/dep.tk','sdk':self.sdk/'lib/std/io.tk','unknown':source(self.work,'unowned.tk',OK)}
  for origin,path in files.items():
   wrapper='#!'+sys.executable+'\nimport json,subprocess,sys\noriginal='+repr(str(self.sdk/'bin/tokac'))+'\nr=subprocess.run([original,*sys.argv[1:]],capture_output=True)\nif sys.argv[1:]==["--version"]:sys.stdout.buffer.write(r.stdout);sys.stderr.buffer.write(r.stderr);sys.exit(r.returncode)\nif r.returncode:sys.stdout.buffer.write(r.stdout);sys.stderr.buffer.write(r.stderr);sys.exit(r.returncode)\nprint(json.dumps({"schema":"toka.diagnostics","version":2,"diagnostics":[{"code":"E9001","message":"controlled provenance diagnostic","severity":"error","primary":{"file":'+repr(str(path))+'}}]}))\nsys.exit(1)\n'
   (variant/'bin/tokac').write_text(wrapper);(variant/'bin/tokac').chmod(0o755)
   def facts(r,origin=origin,path=path):
    d=next((d for d in r['diagnostics'] if d['code']=='E9001'),None);f=d['source'] if d else {}; node=self.nodes(root)['dep'] if origin=='dependency' else self.packages.workspace_node(root/'package.tk',root/'package.lock') if origin=='user' else None
    basis={'user':'workspace_node','dependency':'locked_package_node','sdk':'sdk_root','unknown':'none'}[origin]
    return {'origin':f.get('origin')==origin,'path':f.get('path')==str(path.resolve()),'node':f.get('package_node_id')==node,'basis':f.get('classification_basis')==basis,'compile_failed':r['tests'][0]['result']=='compile_failed','no_run':r['tests'][0]['phases']['run']['state']=='not_started'}
   name='P09-'+origin;self.invoke(name,root,code=1,rows=['P09'],active=variant,extra=facts);shutil.copyfile(variant/'bin/tokac',self.output/name/'proxy.py');self.notes(name,{'controlled_compiler_proxy':True,'delegates_actual_compilation':True,'diagnostic_path':str(path),'proxy_sha256':sha(variant/'bin/tokac'),'not_original_compiler_diagnostic':True})
 def identities(self,dep):
  for name in ('test','package','process','report','safe','native','cc','pkg-config','git','compiler','python'):
   root=self.project('tool-'+name,[('a_test.tk',OK)]);variant=self.variant(name);env=self.env
   if name in ('test','package','process','report','safe'):(variant/'lib/toolchain'/('toka_'+name+'.py' if name in ('test','package') else 'toka_safe_extract.py' if name=='safe' else 'toka_test_'+name+'.py')).unlink()
   elif name=='compiler':(variant/'bin/tokac').unlink()
   elif name=='python':
    empty=self.output/'empty-path';empty.mkdir(exist_ok=True);env=dict(self.env,PATH=str(empty))
   elif name=='git':
    gitpkg=self.work/'git-package';manifest(gitpkg);source(gitpkg,'lib/official/gitpkg.tk','pub fn value()->i32 { return 42 }\n')
    for args in (['init','--quiet'],['add','.'],['-c','user.name=Acceptance','-c','user.email=acceptance@example.invalid','commit','--quiet','-m','fixture']):subprocess.run(['git',*args],cwd=gitpkg,check=True,capture_output=True)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=gitpkg,text=True).strip();manifest(root,'gitpkg=Git({url},commit={commit}),'.format(url=json.dumps(gitpkg.as_uri()),commit=json.dumps(revision)));self.fetch(root)
    shutil.rmtree(next((root/'.toka/packages').iterdir()));path=self.output/'path-git';path.mkdir();(path/'python3').symlink_to(sys.executable);env=dict(self.env,PATH=str(path))
   else:
    native=self.work/('native-dep-'+name);manifest(native);source(native,'lib/official/native.tk','pub fn value()->i32 { return 42 }\n');(native/'package.tk').write_text('pub const PACKAGE=(name="native",version="1.0.0",dependencies=(),native=(required=true,sources=("native/value.c")))\n');source(native,'native/value.c','int value(void){return 42;}\n');manifest(root,'native="../native-dep-'+name+'",');
    if name=='pkg-config':(native/'package.tk').write_text('pub const PACKAGE=(name="native",version="1.0.0",dependencies=(),native=(required=true,pkg_config=("zlib")))\n')
    self.fetch(root)
    if name=='native':(variant/'lib/toolchain/toka_build.py').unlink()
    else:
     path=self.output/('path-'+name);path.mkdir();(path/'python3').symlink_to(sys.executable);env=dict(self.env,PATH=str(path),CC='cc');env.pop('PKG_CONFIG',None)
   r=self.invoke('P10-'+name,root,code=2,rows=['P10'],active=variant,env=env,extra=lambda r:{'no_test_compile':all(t['phases']['compile_link']['state']=='not_started' for t in r['tests'])})
   tool={'test':'toka_test.py','package':'toka_package','process':'toka_test_process','report':'toka_test_report','safe':'toka_safe_extract.py','cc':'cc','pkg-config':'pkg-config','git':'git','native':'toka_build.py','compiler':'tokac','python':'python3'}[name]
   record=self.results[-1];record['checks']['tool_traceable']=(tool if name!='python' else 'Python 3') in (self.output/('P10-'+name)/'stderr').read_text(errors='replace') or any((tool if name!='python' else 'Python 3') in e['message'] for e in r['errors']) or (r['artifact_root'] is not None and any(tool in p.read_text(errors='replace') for p in Path(r['artifact_root']).rglob('*.stderr')));
   if name in ('python','compiler','cc','pkg-config','git'):record['checks']['native_errno']=any(e['os_error']==2 for e in r['errors'])
   record['contract_pass']=all(record['checks'].values());(self.output/('P10-'+name)/'result.json').write_text(json.dumps(record,indent=2)+'\n');self.notes('P10-'+name,{'negative_installed_SDK_variant':True,'missing_tool':tool,'frozen_SDK_modified':False,'source_tree_fallback_available':False})
  for kind in ('compiler-read','runtime-read'):
   root=self.project('identity-'+kind,[('a_test.tk',OK)]);target=self.sdk/('bin/tokac' if kind=='compiler-read' else 'lib/sys/toka_rt.o');original=Path.read_bytes
   @contextlib.contextmanager
   def denied():
    def read(p):
     if p==target:raise PermissionError(13,'controlled identity byte-read failure',str(target))
     return original(p)
    with patch.object(Path,'read_bytes',read):yield
   r=self.helper_fault('P11-'+kind,root,denied,rows=['P11']);record=self.results[-1];checks={'identity_failed':r['identity']['status']=='failed','all_not_run':all(t['result']=='not_run' for t in r['tests']),'no_hash_fabricated':r['identity']['tokac_sha256' if kind=='compiler-read' else 'runtime_object_sha256'] is None};record.update(checks=checks,contract_pass=all(checks.values()));(self.output/('P11-'+kind)/'result.json').write_text(json.dumps(record,indent=2)+'\n')
  root=self.project('identity-version',[('a_test.tk',OK)]);variant=self.variant('empty-version');(variant/'bin/tokac').unlink();(variant/'bin/tokac').write_text('#!'+sys.executable+'\nimport sys\nsys.exit(0)\n');(variant/'bin/tokac').chmod(0o755)
  self.invoke('P11-version',root,code=2,rows=['P11'],active=variant,extra=lambda r:{'identity_failed':r['identity']['status']=='failed','no_version_fabricated':r['identity']['tokac_version'] is None})
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--observe',action='store_true');a=p.parse_args();b=Dependencies(a.sdk.resolve(),a.output.resolve());b.run();d=json.loads((b.output/'result.json').read_text());print(json.dumps({'cases':len(d['records']),'contract_pass':d['contract_pass'],'failed':[(r['name'],[k for k,v in r['checks'].items() if not v]) for r in d['records'] if not r['contract_pass']]}));return 0 if a.observe or d['contract_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
