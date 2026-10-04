#!/usr/bin/env python3
"""Real published Unicode migration and conditional filesystem discovery closure."""
import argparse,base64,errno,json,os,shutil,subprocess,sys,tarfile,re,hashlib
from pathlib import Path
from toka_test_filesystem import query as filesystem_query
from test_toka_test_i2c_batch import Batch,manifest,source,sha,check
SDK_SHA='8341ab9bedaf8d10703cf9bfb89911bd6c648e0a'
PACKAGES={'0.1.1':('c68569e6efbd9eb9bf85226eca68de3a0187d4300e320aeb13857be73b5ad28a','8c82ff393812d1ddd9a8b1f6d71d8ab49863a68b6193af1d784ea722e052fe76'), '0.1.2':('b9aeb5db121875e094584597f7548e5ad992d794ecd7b513fc090ef368671e94','41ebf895976533b5d68f7bb57a46e3e736c4c94a99b828fbad64c68ba99559fc')}
CODE='import official/unicode::{grapheme_count,grapheme_slice}\nfn main()->i32 {\n if grapheme_count("ÄB").unwrap() != 2:usize { return 1 }\n auto first=grapheme_slice("ÄB",0:usize,1:usize).unwrap()\n if first.is_none() { return 2 }\n if !first.unwrap().equals("Ä") { return 3 }\n return 0\n}\n'
B0='import official/unicode::{grapheme_count}\nfn main() -> i32 {\n    auto total# = 0:usize\n    auto i# = 0:i32\n    loop i < 2000 {\n        total += grapheme_count("hello").unwrap()\n        i += 1\n    }\n    if total != 10000:usize { return 1 }\n    return 0\n}\n'
class Closure(Batch):
 def command(self,root,name,argv,expected,env=None,guard=240):
  result=subprocess.run(argv,cwd=root,env=env or self.env,capture_output=True,timeout=guard)
  folder=self.output/name;folder.mkdir();(folder/'stdout').write_bytes(result.stdout);(folder/'stderr').write_bytes(result.stderr)
  data={'name':name,'argv':argv,'cwd':str(root),'exit_code':result.returncode,'expected_exit_code':expected,'guard_seconds':guard,'stdout_sha256':sha(folder/'stdout'),'stderr_sha256':sha(folder/'stderr')}
  if '--json' in argv:
   report=json.loads(result.stdout);check(report);data['report']=report
   if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
  (folder/'result.json').write_text(json.dumps(data,indent=2)+'\n');assert result.returncode==expected,(name,result.stderr.decode(errors='replace'));return data
 def unicode(self,version):
  root=self.project('unicode-'+version,[('main_test.tk',B0 if version=='0.1.1' else CODE)]);manifest(root,'unicode="unicode:'+version+'",');source(root,'src/main.tk',B0 if version=='0.1.1' else CODE)
  source(root,'build.tk','import build::{Executable,run_build}\nfn main()->i32 {\n auto app#=Executable::make(c"unicode_consumer",c"src/main.tk")\n return run_build(app)\n}\n')
  self.command(root,'unicode-'+version+'-fetch',[str(self.sdk/'bin/toka'),'fetch'],0)
  lock=root/'package.lock';before=lock.read_bytes();entries=self.packages.read_lock(lock);entry=entries['unicode'];expected=PACKAGES[version]
  assert entry.resolved==version and entry.archive_sha256==expected[0] and entry.content_sha256==expected[1]
  package=self.packages.package_root(entry,root/'.toka');assert self.packages.tree_sha256(package)==expected[1]
  if version=='0.1.2':
   # Retain the verified installed package content, including its published tests.
   source(root,'tests/unicode_api_test.tk',(package/'tests/unicode_v1.tk').read_text())
   source(root,'corpus/grapheme_corpus.tk',(package/'tests/grapheme_break_corpus.tk').read_text())
  results=[]
  for stage,args in [('check',['check','tests/main_test.tk']),('build',['build']),('test',['test','--json'])]:
   data=self.command(root,'unicode-'+version+'-'+stage,[str(self.sdk/'bin/toka'),*args],1 if version=='0.1.1' else 0);results.append(data);assert lock.read_bytes()==before
  if version=='0.1.1':
   r=results[-1]['report'];codes={d['code'] for d in r['diagnostics']};assert {'E0454','E0455'}<=codes and all(t['result']=='compile_failed' and t['phases']['run']['state']=='not_started' for t in r['tests'])
  else:
   self.command(root,'unicode-0.1.2-run',[str(root/'target/debug/unicode_consumer')],0)
   offline=dict(self.env,TOKA_OFFLINE='1')
   self.command(root,'unicode-0.1.2-offline-fetch',[str(self.sdk/'bin/toka'),'fetch'],0,offline)
   data=self.command(root,'unicode-0.1.2-offline-test',[str(self.sdk/'bin/toka'),'test','--json'],0,offline);assert data['report']['summary']['passed']==2
   original=(package/'tests/grapheme_break_corpus.tk').read_text();matches=list(re.finditer(r'^fn corpus_case_(\d+)\(\) -> bool \{',original,re.M));main=original.index('fn main()');assert len(matches)==766
   header=original[:matches[0].start()];bodies=[original[m.start():(matches[i+1].start() if i+1<len(matches) else main)] for i,m in enumerate(matches)];mapping=[]
   for start in range(0,len(bodies),64):
    stop=min(start+64,len(bodies));name='corpus/shard_'+str(start//64)+'.tk';shard_source=header+''.join(bodies[start:stop])+'fn main()->i32 {\n'+''.join(' if !corpus_case_'+str(i)+'() { return 1 }\n' for i in range(start,stop))+' return 0\n}\n';source(root,name,shard_source)
    corpus=self.command(root,'unicode-0.1.2-corpus-'+str(start//64),[str(self.sdk/'bin/toka'),'test','--json',name,'--compile-timeout-ms','180000','--run-timeout-ms','30000'],0,offline);assert corpus['report']['summary']['passed']==1
    mapping.append({'entry':name,'source_sha256':sha(root/name),'case_ids':list(range(start,stop)),'original_function_bodies_sha256':[hashlib.sha256(body.encode()).hexdigest() for body in bodies[start:stop]]})
   assert lock.read_bytes()==before
   (self.output/'unicode-0.1.2-corpus-map.json').write_text(json.dumps({'original_source_sha256':sha(package/'tests/grapheme_break_corpus.tk'),'case_count':766,'shard_size':64,'function_bodies_unchanged':True,'all_case_ids':list(range(766)),'main_failure_code':1,'shards':mapping,'earlier_monolithic_timeout_not_reclassified':True},indent=2)+'\n')
  node=self.packages.package_node_id(entry)
  for item in results:
   if item.get('report'):
    facts=item['report']['dependencies']['nodes'];assert len(facts)==1 and facts[0]['package_node_id']==node and facts[0]['resolved']==version
  (self.output/('unicode-'+version+'-identity.json')).write_text(json.dumps({'version':version,'archive_sha256':entry.archive_sha256,'content_sha256':entry.content_sha256,'package_node_id':node,'lock_sha256':sha(lock),'lock_unchanged':lock.read_bytes()==before,'package_source_files':{str(p.relative_to(package)):sha(p) for p in package.rglob('*') if p.is_file()},'historical_failure_preserved':version=='0.1.1','expected_new_consumer_compatibility':version=='0.1.2','migration_explicit':True},indent=2)+'\n')
  (self.output/('unicode-'+version+'-package.lock')).write_bytes(before)
 def paths(self):
  facts=[]
  for index,invalid in enumerate((b'\xff',b'\x80',b'\xc3')):
   root=self.project('invalid-path-'+str(index),[]);(root/'tests').mkdir();raw=os.fsencode(root/'tests')+b'/bad-'+invalid+b'_test.tk';created=False;error=None
   normal=root/'tests/normal.tmp';normal.write_bytes(b'control');normal.unlink()
   try:
    fd=os.open(raw,os.O_CREAT|os.O_WRONLY|os.O_EXCL,0o600);os.write(fd,b'fn main()->i32 { return 0 }\n');os.close(fd);created=True
   except OSError as e:
    if e.errno not in (errno.EPERM,errno.EINVAL,errno.EILSEQ):raise
    error={'errno':e.errno,'message':str(e)}
   filesystem=filesystem_query(root/'tests',self.output/'filesystem-query')
   record={'filesystem':filesystem,'raw_path_base64':base64.b64encode(raw).decode(),'normal_name_creation_succeeded':True,'invalid_name_created':created,'creation_error':error,'platform':sys.platform}
   try:
    for kind in ('explicit','discovery'):
     if kind=='discovery' and not created:
      record['discovery_status']='not_applicable_filesystem_precondition';continue
     args=[str(self.sdk/'bin/toka'),'test','--json']+([os.fsdecode(raw)] if kind=='explicit' else [])
     item=self.command(root,'D27-'+str(index)+'-'+kind,args,2);r=item['report'];assert r['result']=='configuration_error' and r['summary']['total'] is None and r['tests']==[] and raw in [base64.b64decode(x) for x in r['raw_inputs_base64']]
     assert r['identity']['status']=='not_checked' and r.get('preparation',{})=={}
     record[kind+'_status']='passed_rejection'
   finally:
    if created:os.rename(raw,os.fsencode(root/'tests/retained-invalid-source.bytes'))
   facts.append(record)
  filesystem=filesystem_query(self.output,self.output/'filesystem-query')
  (self.output/'D27-filesystem.json').write_text(json.dumps({**filesystem,'uid':os.getuid(),'uname':list(os.uname()),'records':facts,'contract_amendment':'conditional discovery precondition; explicit raw-argv rejection remains mandatory','no_unrun_discovery_claimed_pass':True},indent=2)+'\n')
 def verify_sdk(self):
  identity=json.loads((self.sdk/'preview-sdk.json').read_text());assert identity['candidate_sha']==SDK_SHA and len(identity['components'])==146
  for name,item in identity['components'].items():assert sha(self.sdk/name)==item['sha256']
 def run(self):
  self.verify_sdk()
  self.unicode('0.1.1');self.unicode('0.1.2');self.paths()
  (self.output/'result.json').write_text(json.dumps({'result':'pass_for_closure_candidate','SDK_source_sha':SDK_SHA,'script_sha256':sha(Path(__file__)),'Preview':True,'Accepted':False,'old_0_1_1_still_rejected':True,'new_0_1_2_real_published_package':True,'D27_contract_amendment_requires_review':True},indent=2)+'\n')
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--paths-only',action='store_true');a=p.parse_args();probe=Closure(a.sdk.resolve(),a.output.resolve())
 if a.paths_only:
  probe.verify_sdk();probe.paths();(probe.output/'result.json').write_text(json.dumps({'result':'pass_for_D27_metadata_revision','SDK_source_sha':SDK_SHA,'script_sha256':sha(Path(__file__)),'paths_only':True,'Unicode_replayed':False,'Preview':True,'Accepted':False},indent=2)+'\n')
 else:probe.run()
if __name__=='__main__':main()
