#!/usr/bin/env python3
"""Installed CLI discovery/input acceptance. Observation mode retains failures."""
import argparse,base64,errno,json,os,stat,subprocess,sys
from pathlib import Path
from test_toka_test_i2c_batch import Batch,source
OK='fn main() -> i32 { return 0 }\n'
class Discovery(Batch):
 def invoke(self,name,root,args=(),code=0,rows=(),ids=None,reason=None,selection_error=False,extra=None,cwd=None):
  command=[str(self.sdk/'bin/toka'),'test','--json',*args]
  child=subprocess.run(command,cwd=cwd or root,env=self.env,capture_output=True,timeout=45)
  report,end=json.JSONDecoder().raw_decode(child.stdout.decode('utf-8'));assert not child.stdout.decode('utf-8')[end:].strip()
  self.save(name,root,command,child.returncode,child.stdout,child.stderr,report,rows=rows)
  checks={'exit_code':child.returncode==code,'root':report['project_root']==str(root)}
  if ids is not None:checks.update(ids=[t['id'] for t in report['tests']]==ids,passed=report['summary']['passed']==len(ids),total=report['summary']['total']==len(ids))
  if reason is not None:checks['reason']=report['reason']==reason
  if selection_error:
   checks.update(configuration_error=report['result']=='configuration_error',total_unknown=report['summary']['total'] is None,no_entries=report['tests']==[],no_children=report['preparation']=={} and report['identity']['status']=='not_checked',selection_incomplete=report['timings']['selection']['state']=='aborted')
  if extra:checks.update(extra(report))
  record=self.results[-1];record.update(checks=checks,contract_pass=all(checks.values()),cwd=str(cwd or root),expected_exit_code=code)
  (self.output/name/'result.json').write_text(json.dumps(record,ensure_ascii=True,indent=2)+'\n')
  return report
 def run(self):
  root=self.project('explicit-helper',[('helper.tk',OK),('bad_test.tk','bad source')]);self.invoke('D03',root,['tests/helper.tk'],rows=['D03'],ids=['tests/helper.tk'],extra=lambda r:{'explicit':r['selection']['mode']=='explicit','candidate_count':r['selection']['candidate_count']==1})
  root=self.project('explicit-outside-tests',[('default_test.tk','bad source'),('one.tk',OK)]);source(root,'apps/two.tk',OK)
  self.invoke('D04',root,['tests/one.tk','apps/two.tk'],rows=['D04'],ids=['apps/two.tk','tests/one.tk'])
  root=self.project('three-candidates',[('a_test.tk',OK),('nested/b_test.tk',OK),('c_test.tk',OK)])
  self.invoke('D05',root,['--filter','nested/'],rows=['D05'],ids=['tests/nested/b_test.tk'],extra=lambda r:{'filters':r['selection']['filters']==['nested/'],'candidate_count':r['selection']['candidate_count']==3,'selected_count':r['selection']['selected_count']==1})
  self.invoke('D06',root,['--filter','a_','--filter','b_','--filter','a_'],rows=['D06'],ids=['tests/a_test.tk','tests/nested/b_test.tk'],extra=lambda r:{'filters':r['selection']['filters']==['a_','b_','a_'],'candidate_count':r['selection']['candidate_count']==3})
  for name,entries in [('no-tests',[]),('helper-only',[('helper.tk','bad source')])]:
   root=self.project(name,entries)
   self.invoke('D10-'+name,root,code=2,rows=['D10'],ids=[],reason='no_tests',extra=lambda r:{'configuration_error':r['result']=='configuration_error','no_children':r['preparation']=={}})
   self.invoke('D11-'+name,root,['--allow-empty'],rows=['D11'],ids=[],reason='no_tests',extra=lambda r:{'empty':r['result']=='empty','no_children':r['preparation']=={}})
  files=['z_test.tk','中文_test.tk','Upper_test.tk','a_test.tk']
  root=self.project('ordering',[(name,OK) for name in files]);ordered=sorted(['tests/'+name for name in files],key=lambda s:s.encode('utf-8'))
  self.invoke('D12-discovery',root,rows=['D12'],ids=ordered)
  args=['tests/中文_test.tk','tests/z_test.tk',str(root/'tests/a_test.tk'),'tests/./a_test.tk','tests/Upper_test.tk',str(root/'tests/z_test.tk')]
  alias=root/'tests/upper_test.tk';insensitive=alias.exists() and alias.samefile(root/'tests/Upper_test.tk')
  if insensitive:args.append('tests/upper_test.tk')
  r=self.invoke('D12-explicit',root,args,rows=['D12'],ids=ordered,extra=lambda r:{'explicit':r['selection']['mode']=='explicit','candidate_count':r['selection']['candidate_count']==4})
  (self.output/'D12-explicit/filesystem.json').write_text(json.dumps({'case_insensitive_alias_exists':insensitive,'case_alias_argument_exercised':insensitive,'creation_order':files,'expected_utf8_byte_order':ordered},indent=2)+'\n')
  root=self.project('hard-links',[('b_test.tk',OK)]);os.link(root/'tests/b_test.tk',root/'tests/a_test.tk')
  self.invoke('D12-hardlinks',root,rows=['D12'],ids=['tests/a_test.tk','tests/b_test.tk'],extra=lambda r:{'same_inode':(root/'tests/a_test.tk').samefile(root/'tests/b_test.tk')})
  root=self.project('links',[('ok_test.tk',OK)]);outside=self.work/'link-target';outside.mkdir();source(outside,'bad_test.tk','bad source')
  (root/'tests/file_test.tk').symlink_to(outside/'bad_test.tk');(root/'tests/linked').symlink_to(outside,target_is_directory=True)
  self.invoke('D14',root,rows=['D14'],ids=['tests/ok_test.tk'],extra=lambda r:{'excluded_file':{'path':'tests/file_test.tk','reason':'symlink'} in r['selection']['excluded'],'excluded_directory':{'path':'tests/linked','reason':'symlink'} in r['selection']['excluded']})
  for name,entry in [('file','tests/file_test.tk'),('directory','tests/linked/bad_test.tk')]:
   for allowed in (False,True):self.invoke('D15-'+name+('-allow' if allowed else ''),root,[entry]+(['--allow-empty'] if allowed else []),code=2,rows=['D15'],reason='invalid_entry',selection_error=True)
  root=self.project('invalid-explicit',[('ok_test.tk',OK)]);source(root,'data.txt','ordinary data');foreign=source(self.work,'foreign.tk',OK)
  for name,arg in [('outside',str(foreign)),('parent','../foreign.tk'),('directory','tests'),('missing','missing.tk'),('suffix','data.txt')]:
   normalized=str((root/arg).resolve())
   self.invoke('D16-'+name,root,[arg],code=2,rows=['D16'],selection_error=True,extra=lambda r,arg=arg,path=normalized:{'input_retained':any(arg in e['message'] for e in r['errors']),'normalized_retained':any(path in e['message'] for e in r['errors'])})
  body='import std/fs\nimport std/env\nimport std/io::{println}\nfn main() -> i32 {\n println("{}",fs::read_to_string(string::from("resource.txt")).unwrap())\n println("{}",env::var(string::from("TOKA_TEST_RUN_DIR")).unwrap())\n println("{}",env::var(string::from("TOKA_TEST_CASE_DIR")).unwrap())\n println("{}",env::current_dir())\n return 0\n}\n'
  root=self.project('resource-cwd',[('a_test.tk',body)]);(root/'resource.txt').write_text('RESOURCE_FROM_ROOT');(root/'src').mkdir()
  def resources(r):
   test=r['tests'][0];expected=['RESOURCE_FROM_ROOT',r['artifact_root'],str(Path(test['logs']['run_stdout']).parent),str(root)]
   return {'resource_and_env':Path(test['logs']['run_stdout']).read_text().splitlines()==expected}
  self.invoke('D17-A04',root,['../tests/a_test.tk'],rows=['D17','A04'],ids=['tests/a_test.tk'],cwd=root/'src',extra=resources)
  root=self.project('nested-project',[('ok_test.tk',OK)]);source(root,'tests/nested/package.tk','pub const PACKAGE=(name="nested",version="1.0.0",dependencies=())\n');source(root,'tests/nested/b_test.tk',OK)
  self.invoke('D19',root,['tests/nested/b_test.tk'],code=2,rows=['D19'],reason='invalid_entry',selection_error=True)
  bads=[('missing-main','pub fn helper() -> i32 { return 0 }\n'),('signature','fn main() -> Result<i32,string> { return Result<i32,string>::Err(string::from("entry failure")) }\n')]
  for name,body in bads:
   for explicit in (False,True):
    root=self.project('entry-'+name+str(explicit),[('a_test.tk',body)])
    def failed(r):
     t=r['tests'][0];return {'compile_failed':t['result']=='compile_failed','not_run':t['phases']['run']['state']=='not_started','compiler_started':t['phases']['compile_link']['process'] is not None,'original_diagnostic':bool(Path(t['logs']['compile_stdout']).read_bytes()+Path(t['logs']['compile_stderr']).read_bytes())}
    self.invoke('D22-'+name+('-explicit' if explicit else ''),root,['tests/a_test.tk'] if explicit else [],code=1,rows=['D22'],extra=failed)
  self.unreadable()
  for explicit in (False,True):
   root=self.project('invalid-utf8-'+str(explicit),[]);(root/'tests').mkdir();path=os.fsencode(root/'tests')+b'/invalid-\xff_test.tk'
   fixture_available=True;creation_error=None
   try:
    fd=os.open(path,os.O_WRONLY|os.O_CREAT,0o600);os.write(fd,OK.encode());os.close(fd)
   except OSError as error:
    if sys.platform!='darwin' or error.errno not in (errno.EPERM,errno.EINVAL,errno.EILSEQ):raise
    normal=root/'tests/valid-name.bytes';normal.write_bytes(OK.encode());normal.unlink()
    fixture_available=False;creation_error={'errno':error.errno,'valid_name_creation_succeeded':True}
   name='D27-'+('explicit' if explicit else 'discovery');argument=os.fsdecode(path)
   if not fixture_available and not explicit:
    folder=self.output/name;folder.mkdir();record={'name':name,'rows':['D27'],'checks':{'fixture_unavailable_recorded':True},'contract_pass':True,'fixture_available':False,'exit_code':None,'report':None,'evidence_layer':'filesystem precondition only; no discovery CLI acceptance claimed','creation_error':creation_error};self.results.append(record);(folder/'result.json').write_text(json.dumps(record,indent=2)+'\n')
   else:self.invoke(name,root,[argument] if explicit else [],code=2,rows=['D27'],selection_error=True,extra=lambda r,path=path:{'raw_bytes_recoverable':path in [base64.b64decode(x) for x in r['raw_inputs_base64']]})
   if fixture_available:os.rename(path,os.fsencode(root/'tests/raw-invalid-entry.bytes'))
   else:
    record=self.results[-1];record.update(fixture_available=False,creation_error=creation_error);(self.output/name/'result.json').write_text(json.dumps(record,ensure_ascii=True,indent=2)+'\n')
   (self.output/name/'path-bytes.json').write_text(json.dumps({'original_path_base64':base64.b64encode(path).decode(),'renamed_for_artifact_transport':'consumer-projects/'+root.name+'/tests/raw-invalid-entry.bytes'},indent=2)+'\n')
  records=[{'name':r['name'],'rows':r['rows'],'exit_code':r['exit_code'],'contract_pass':r['contract_pass'],'checks':r['checks'],'fixture_available':r.get('fixture_available',True)} for r in self.results]
  (self.output/'result.json').write_text(json.dumps({'schema':'toka.discovery-acceptance','contract_pass':all(r['contract_pass'] for r in records),'records':records},indent=2)+'\n')
 def unreadable(self):
  assert os.geteuid()!=0,'D26 requires a non-root user with real permission denial'
  for kind in ('file','directory','tests-root'):
   root=self.project('unreadable-'+kind,[('a_good_test.tk',OK)])
   if kind=='file':target=source(root,'tests/z_blocked_test.tk',OK)
   elif kind=='directory':target=root/'tests/z_blocked';source(root,'tests/z_blocked/b_test.tk',OK)
   else:target=root/'tests'
   old=stat.S_IMODE(target.stat().st_mode);target.chmod(0)
   try:
    try:
     if target.is_dir():list(target.iterdir())
     else:target.read_bytes()
     raise AssertionError('OS unexpectedly permitted read')
    except PermissionError as error:witness={'uid':os.geteuid(),'path':str(target),'mode':stat.S_IMODE(target.stat().st_mode),'errno':error.errno}
    name='D26-'+kind
    self.invoke(name,root,code=2,rows=['D26'],selection_error=True,extra=lambda r,target=target:{'error_path':any(str(target) in e['message'] for e in r['errors']),'errno':any(e['os_error']==13 for e in r['errors'])})
    (self.output/name/'permission-witness.json').write_text(json.dumps(witness,indent=2)+'\n')
   finally:target.chmod(old)

def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--observe',action='store_true');a=p.parse_args()
 b=Discovery(a.sdk.resolve(),a.output.resolve());b.run();data=json.loads((b.output/'result.json').read_text());print(json.dumps({'observations':len(data['records']),'contract_pass':data['contract_pass'],'failed':[(r['name'],[k for k,v in r['checks'].items() if not v]) for r in data['records'] if not r['contract_pass']]}))
 return 0 if a.observe or data['contract_pass'] else 1
if __name__=='__main__':raise SystemExit(main())
