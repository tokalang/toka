#!/usr/bin/env python3
"""Assemble exact prior installed receipts before scheduling missing report variants."""
import argparse,hashlib,json,shutil
from pathlib import Path
ROWS={'A03','A06','J02','J04','J05','J06','J07','J08','J10','M03'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def checked(path):
 d=json.loads(path.read_text());r=d['report'];assert r['schema']=='toka.test-report' and r['version']==1 and r['finalized'] and d['exit_code']==r['exit_code']
 assert json.loads((path.parent/'stdout').read_text())==r
 counts=r['summary'];assert counts['total'] is None or counts['total']==len(r['tests'])==sum(v for k,v in counts.items() if k!='total')
 for t in r['tests']:
  for p in t['phases'].values():
   if p['state']=='not_started':assert all(p[k] is None for k in ('duration_ms','exit_code','signal','os_error','process'))
 return d,r

def main():
 p=argparse.ArgumentParser();p.add_argument('--batch',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--cleanup',type=Path,required=True);p.add_argument('--prior',type=Path,required=True);p.add_argument('--new',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(exist_ok=False);proofs={row:[] for row in ROWS};sources={};original_paths={}
 def use(label,path):
  if label not in sources:
   key=str(path.resolve())
   if key in original_paths:sources[label]=original_paths[key]
   else:
    target=a.output/'reused'/label;target.parent.mkdir(exist_ok=True);shutil.copytree(path.parent,target);sources[label]=target/'result.json';original_paths[key]=sources[label]
  return checked(sources[label])
 def prove(row,label,path,assertions):
  d,r=use(label,path);facts=assertions(d,r);assert all(facts.values()),(row,label,facts)
  proofs[row].append({'path':str(sources[label]),'case':label,'actual_exit_code':r['exit_code'],'checks':facts,'evidence_layer':'reused immutable installed-SDK receipt; no new scenario execution','source_report_sha256':sha(path)})
 reg=a.regression/'i2c-installed/i2b-installed';batch=a.batch/'i2c-batch'
 compile_path=reg/'compile-failure/result.json'
 cases={'R02':compile_path,'R04':batch/'R04/result.json','R06':batch/'lifecycle/R06/result.json','R08':batch/'R08-compiler/result.json','T03':batch/'lifecycle/T03/result.json'}
 cases.update({'R11-'+str(i):batch/('R11-'+str(i))/'result.json' for i in range(5)});cases['R11-non-project']=batch/'R11-non-project/result.json'
 if (batch/'r08-driver').is_dir():cases.update({'R08-linker-'+v:batch/'r08-driver'/('R08-linker-'+v)/'result.json' for v in ('missing','permission','interpreter')})
 for label,path in cases.items():
  def assertions(d,r,label=label):
   facts={'exit_code':r['exit_code']==(130 if label=='T03' else 1 if label in ('R02','R04','R06') else 2),'schema':r['schema']=='toka.test-report' and r['version']==1}
   if label=='R02':facts['compile_failed']=r['tests'][0]['result']=='compile_failed' and r['tests'][0]['phases']['compile_link']['exit_code']==1
   if label=='R04':facts['raw_runtime']=r['tests'][0]['result']=='run_failed' and r['tests'][0]['phases']['run']['exit_code']==7
   if label=='R06':facts['compile_timeout']=r['tests'][0]['trigger']=='timeout' and r['tests'][0]['phases']['compile_link']['signal']==15
   if label=='T03':facts['interrupt']=r['tests'][0]['result']=='interrupted' and r['termination']['signal']==2
   return facts
  prove('J02',label,path,assertions)
 for label,path in [('bad-source',compile_path),('exit-seven',batch/'R04/result.json'),('compile-timeout',batch/'lifecycle/R06/result.json'),('runtime-timeout',batch/'lifecycle/R07/result.json')]:
  def logs(d,r,path=path):
   n=0;valid=True;payloads={}
   for t in r['tests']:
    for key,log in t['logs'].items():
     phase=t['phases']['compile_link' if key.startswith('compile') else 'run']
     if phase['state']=='not_started':valid &= log is None;continue
     assert log is not None;parts=Path(log).parts;actual=path.parent/Path(*parts[parts.index('test-runs'):]);assert actual.is_file();n+=1;payloads[key]=sha(actual)
   if r['tests'][0]['trigger']=='timeout':
    key='compile_stdout' if r['tests'][0]['phases']['run']['state']=='not_started' else 'run_stdout';log=r['tests'][0]['logs'][key];parts=Path(log).parts;assert b'READY_' in (path.parent/Path(*parts[parts.index('test-runs'):])).read_bytes()
   return {'failed_code':r['exit_code']==1,'phase_logs_exist_and_hashed':n>=2,'unstarted_logs_null':bool(valid),'payload_digests_recorded':bool(payloads)}
  prove('A03',label,path,logs)
 prove('J05','timeout-kill',batch/'lifecycle/T02/result.json',lambda d,r:{'exit':r['exit_code']==1,'timeout':r['tests'][0]['result']=='timed_out' and r['tests'][0]['trigger']=='timeout','raw_SIGKILL':r['tests'][0]['phases']['run']['signal']==9,'cleanup':r['tests'][0]['cleanup']['status']=='confirmed'})
 for fault in ('permission','wait','output'):
  prove('J06','interrupt-'+fault,a.cleanup/('interrupt-'+fault)/'result.json',lambda d,r:{'infra':r['exit_code']==2 and r['result']=='infrastructure_error','SIGINT':r['termination']['signal']==2,'trigger':r['tests'][0]['trigger']=='interrupt','cleanup_failed':r['tests'][0]['cleanup']['status']=='failed'})
 prove('J08','unknown-before-json',reg/'configuration-error/result.json',lambda d,r:{'argv_order':d['command'].index('--unknown')<d['command'].index('--json'),'exit':r['exit_code']==2,'unknown_total':r['summary']['total'] is None,'no_tests':r['tests']==[],'zero_counts':all(v==0 for k,v in r['summary'].items() if k!='total')})
 for label,path in [('compile-not-run',compile_path),('launch-not-run',batch/'R08-compiler/result.json'),('scheduler-not-run',batch/'R13-J03/result.json')]:
  prove('M03',label,path,lambda d,r,label=label:{'exit':r['exit_code']==(1 if label=='compile-not-run' else 2),'absent_run_duration':all(t['phases']['run']['duration_ms'] is None for t in r['tests'] if t['phases']['run']['state']=='not_started'),'not_run_phases':all(all(p['duration_ms'] is None and p['process'] is None for p in t['phases'].values()) for t in r['tests'] if t['result']=='not_run')})
 new=json.loads((a.new/'result.json').read_text());assert new['result']=='pass' and new['cases']==10
 for item in new['records']:
  path=Path(item['path']);d,r=checked(path);assert d['contract_pass'] and all(d['checks'].values())
  for row in d['rows']:
   proofs[row].append({'path':str(path),'case':d['name'],'actual_exit_code':r['exit_code'],'checks':d['checks'],'evidence_layer':d['evidence_layer'],'source_report_sha256':sha(path)})
 assert all(proofs.values());ledger=json.loads(a.prior.read_text())
 for row in ledger['rows']:
  if row['id'] not in ROWS:continue
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],coverage='covered',gap=None,evidence=proofs[row['id']])
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')};ledger.update(full_matrix_pass=False,preview_removal_authorized=False)
 (a.output/'coverage-ledger.json').write_text(json.dumps(ledger,indent=2)+'\n')
 (a.output/'row-proof.json').write_text(json.dumps({'result':'pass','rows':proofs,'reused_unique_receipts':len(set(sources.values())),'new_scenarios':10,'preview':True,'Accepted':False},indent=2)+'\n')
if __name__=='__main__':main()
