#!/usr/bin/env python3
"""Installed SDK artifact/diagnostic boundary variants missing from prior receipts."""
import argparse,contextlib,io,json,os,shutil,sys
from pathlib import Path
from unittest.mock import patch
from test_toka_test_i2c_batch import Batch,manifest,source,sha,check
SDK_SHA='8341ab9bedaf8d10703cf9bfb89911bd6c648e0a'
OK='fn main()->i32 { return 0 }\n'
class Boundaries(Batch):
 def commit(self,name,code,out,err,r,root,rows,checks,extra=None,layer='installed SDK helper fault injection'):
  folder=self.output/name;folder.mkdir();(folder/'stdout').write_bytes(out);(folder/'stderr').write_bytes(err);check(r)
  if (root/'.toka/test-runs').is_dir():shutil.copytree(root/'.toka/test-runs',folder/'test-runs')
  checks.update(exit_matches=code==r['exit_code'],memory_JSON=json.loads(out)==r,finalized=r['finalized'],preview=r['preview'])
  record={'name':name,'rows':rows,'exit_code':code,'report':r,'checks':checks,'contract_pass':all(checks.values()),'evidence_layer':layer,**(extra or {})}
  (folder/'result.json').write_text(json.dumps(record,indent=2)+'\n');self.results.append(record);assert record['contract_pass'],checks
 def artifact_fault(self,kind):
  root=self.project('artifact-'+kind,[('a_test.tk',OK),('b_test.tk',OK)]);runner=self.runner;events=[];original_open=Path.open;original_write=runner.packages.atomic_write
  if kind=='mkdir':(root/'.toka').write_bytes(b'controlled non-directory artifact parent')
  class Stream:
   def __init__(self,stream):self.stream=stream
   def __getattr__(self,key):return getattr(self.stream,key)
   def __enter__(self):return self
   def __exit__(self,*args):
    self.stream.close()
    if kind=='log-close':events.append({'fault':'raw_log_close_confirmation','errno':5,'underlying_fd_closed':True});raise OSError(5,'controlled raw log close confirmation failure')
   def fileno(self):
    if kind=='log-write':
     self.stream.write(b'CONTROLLED_PARTIAL_LOG_WRITE\n');self.stream.flush();events.append({'fault':'raw_log_write_target','errno':28,'partial_bytes_written':28});raise OSError(28,'controlled raw log write target failure')
    return self.stream.fileno()
  def opened(path,*args,**kwargs):
   mode=args[0] if args else kwargs.get('mode','r')
   if path.name=='compile.stdout' and mode=='xb':
    if kind=='log-create':events.append({'fault':'raw_log_create','errno':28});raise OSError(28,'controlled raw log allocation failure')
    if kind in ('log-write','log-close'):return Stream(original_open(path,*args,**kwargs))
   return original_open(path,*args,**kwargs)
  def write(path,data):
   if kind=='report' and Path(path).name=='report.json':events.append({'fault':'report_persistence','errno':28});raise OSError(28,'controlled report persistence failure')
   return original_write(path,data)
  stdout,stderr=io.StringIO(),io.StringIO()
  with patch.object(Path,'open',opened),patch.object(runner.packages,'atomic_write',write),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):code=runner.execute_preview(['--json'],self.sdk/'lib',self.sdk/'bin/tokac',root)
  r=json.loads(stdout.getvalue());checks={'infrastructure':code==2 and r['result']=='infrastructure_error','errors_kept':bool(r['errors']),'not_success':r['result']!='passed'}
  if kind=='mkdir':checks.update(no_artifact=r['artifact_root'] is None,no_test=r['tests']==[])
  elif kind=='report':checks.update(not_persisted=not list((root/'.toka/test-runs').glob('*/report.json')),available_logs=all(Path(t['logs']['compile_stdout']).is_file() for t in r['tests']),persistence_failure=any('persistence' in e['message'] for e in r['errors']))
  else:
   checks['stop_schedule']=[t['result'] for t in r['tests']]==['infrastructure_error','not_run']
   if kind=='log-write':checks['partial_log_retained']=Path(r['tests'][0]['logs']['compile_stdout']).read_bytes()==b'CONTROLLED_PARTIAL_LOG_WRITE\n'
   if kind=='log-close':checks['cleanup_failure']=r['tests'][0]['cleanup']['status']=='failed'
  self.commit('A06-'+kind,code,stdout.getvalue().encode(),stderr.getvalue().encode(),r,root,['A06']+(['J07'] if kind in ('mkdir','report') else []),checks,{'fault_observations':events,'report_persisted':bool(list((root/'.toka/test-runs').glob('*/report.json'))),'fault_kind':kind})
 def diagnostics(self,variant):
  name='J10' if variant=='known' else 'J04-'+variant;root=self.project(name,[('a_test.tk',OK if variant=='known' else 'fn main()->i32 { return no_such_report_symbol }\n')]);dep=root/'dep';manifest(dep);source(dep,'lib/official/dep.tk','pub fn answer()->i32 { return 42 }\n')
  manifest(root,'dep="./dep",')
  if variant=='ambiguous':
   source(dep,'lib/official/outer.tk','pub fn answer()->i32 { return 42 }\n');inner=dep/'nested';manifest(inner);source(inner,'lib/official/inner.tk','pub fn answer()->i32 { return 43 }\n');manifest(root,'outer="./dep",inner="./dep/nested",')
  import subprocess
  fetch=subprocess.run([str(self.sdk/'bin/toka'),'fetch'],cwd=root,env=self.env,capture_output=True,timeout=30);assert fetch.returncode==0,fetch.stderr
  location={'missing':None,'virtual':'<generated:report-fixture>','system':'/__toka_report_fixture_unmapped__/file.tk','ambiguous':str(dep/'nested/lib/official/inner.tk')}.get(variant)
  records=[]
  if variant=='known':
   for index,(origin,path,severity) in enumerate([('user',root/'tests/a_test.tk','note'),('dependency',dep/'lib/official/dep.tk','warning'),('sdk',self.sdk/'lib/std/io.tk','note')]):
    assert path.is_file();records.append({'code':'WREPORT'+str(index),'message':'controlled known '+origin,'severity':severity,'primary':{'file':str(path),'range':{'start':{'line':0,'character':0},'end':{'line':0,'character':1}}}})
  else:
   item={'code':'EREPORT_'+variant,'message':'controlled unknown '+variant,'severity':'error'}
   if location is not None:item['primary']={'file':location}
   records.append(item)
  proxy=self.output/(name+'-compiler');(proxy/'bin').mkdir(parents=True);(proxy/'lib').symlink_to(self.sdk/'lib',target_is_directory=True);shutil.copyfile(self.sdk/'bin/toka',proxy/'bin/toka');(proxy/'bin/toka').chmod(0o755)
  script='#!'+sys.executable+'\nimport json,subprocess,sys\noriginal='+repr(str(self.sdk/'bin/tokac'))+'\nargs=sys.argv[1:]\nr=subprocess.run([original,*args],capture_output=True)\nif args==["--version"]:sys.stdout.buffer.write(r.stdout);sys.stderr.buffer.write(r.stderr);sys.exit(r.returncode)\ndata=json.loads(r.stdout);data["diagnostics"] += '+repr(records)+'\nsys.stdout.write(json.dumps(data));sys.stderr.buffer.write(r.stderr);sys.exit(r.returncode)\n'
  (proxy/'bin/tokac').write_text(script);(proxy/'bin/tokac').chmod(0o755)
  child=subprocess.run([str(proxy/'bin/toka'),'test','--json'],cwd=root,env=self.env,capture_output=True,timeout=30);r=json.loads(child.stdout);selected=[d for d in r['diagnostics'] if d['code'] in {x['code'] for x in records}]
  checks={'count':len(selected)==len(records),'real_compiler_result':child.returncode==(0 if variant=='known' else 1),'trusted_compile_phase':all(d['phase']=='compile_link' for d in selected),'original_records_preserved':all(item in json.loads(Path(r['tests'][0]['logs']['compile_stdout']).read_text())['diagnostics'] for item in records)}
  if variant=='known':
   for diag,(origin,item) in zip(selected,zip(('user','dependency','sdk'),records)):
    checks[origin+'_classification']=diag['source']['origin']==origin and diag['source']['path']==str(Path(item['primary']['file']).resolve()) and diag['severity']==item['severity']
    checks[origin+'_basis']=diag['source']['classification_basis']=={'user':'workspace_node','dependency':'locked_package_node','sdk':'sdk_root'}[origin]
    checks[origin+'_node']=bool(diag['source']['package_node_id']) if origin!='sdk' else diag['source']['package_node_id'] is None
   checks['passes_without_warning_promotion']=all(t['result']=='passed' for t in r['tests'])
  else:
   checks['unknown']=all(d['source']['origin']=='unknown' and d['source']['package_node_id'] is None and d['source']['classification_basis']=='none' for d in selected)
   checks['compile_failure']=r['tests'][0]['result']=='compile_failed' and r['tests'][0]['phases']['run']['state']=='not_started'
   if variant=='ambiguous':
    facts=r['dependencies']['nodes'];checks['distinct_locked_nodes']=len({f['package_node_id'] for f in facts})==2 and all(Path(location).is_relative_to(f['root']) for f in facts)
  self.commit(name,child.returncode,child.stdout,child.stderr,r,root,[name.split('-')[0]],checks,{'compiler_proxy_sha256':sha(proxy/'bin/tokac'),'synthetic_diagnostics':records,'injection_scope':'controlled compiler stdout; actual compilation delegated to immutable SDK tokac; not a claim original compiler emitted fixture codes'},layer='real installed CLI with controlled compiler diagnostic proxy')
  (proxy/'lib').unlink()
 def run(self):
  descriptor=json.loads((self.sdk/'preview-sdk.json').read_text());assert descriptor['candidate_sha']==SDK_SHA and len(descriptor['components'])==146
  for n,v in descriptor['components'].items():assert sha(self.sdk/n)==v['sha256']
  for kind in ('mkdir','log-create','log-write','log-close','report'):self.artifact_fault(kind)
  for variant in ('missing','virtual','system','ambiguous','known'):self.diagnostics(variant)
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();batch=Boundaries(a.sdk.resolve(),a.output.resolve());batch.run();assert len(batch.results)==10
 (batch.output/'result.json').write_text(json.dumps({'result':'pass','SDK_source_sha':SDK_SHA,'cases':10,'records':[{'name':r['name'],'path':str(batch.output/r['name']/'result.json'),'exit_code':r['exit_code'],'checks':r['checks']} for r in batch.results],'script_sha256':sha(Path(__file__)),'preview':True,'Accepted':False},indent=2)+'\n')
if __name__=='__main__':main()
