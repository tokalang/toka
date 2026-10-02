#!/usr/bin/env python3
"""Attach exact row assertions/evidence layers to an accepted prior coverage ledger."""
import argparse,json
from pathlib import Path
ROWS={'R03':['R03'],'R04':['R04'],'R08':['R08-compiler'],'R09':['R09'],'R10':['R10'],
      'R11':['R11-'+str(i) for i in range(5)]+['R11-non-project'],'R13':['R13-J03'],
      'J01':['J01','J01-compiler-volume'],'J03':['R13-J03'],'J09':['J09'],
      'P13':['P13-0','P13-1'],'P14b':['P14b-0','P14b-1'],'A01':['A01-0','A01-1'],
      'A02':['A02'],'A05':['A05'],'T11e':['T11e'],'T12':['T12-posix']}

def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--batch',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--regression',type=Path,required=True)
 a=p.parse_args();ledger=json.loads(a.prior.read_text());summary=json.loads((a.batch/'result.json').read_text());assert summary['result']=='pass'
 for row in ledger['rows']:
  if row['id'] not in ROWS:continue
  attached=[]
  for case in ROWS[row['id']]:
   path=a.batch/case/'result.json';record=json.loads(path.read_text())
   assert row['id'] in record['rows']
   attached.append({'path':str(path),'case':case,'actual_exit_code':record['exit_code'],
                    'evidence_layer':record.get('evidence_layer','incomplete delivery observation'),
                    'incomplete':record.get('incomplete',False)})
  row['prior_evidence']=row['evidence'];row['prior_coverage']=row['coverage']
  row.update(coverage='covered',gap=None,evidence=attached)
  if row['id']=='R08':row['profile_scope']='compiler startup covered; separately spawned linker is not applicable to the bundled-LLD core SDK profile; real link failure separately R03'
  if row['id']=='T12':row['profile_scope']='POSIX managed start refusal covered with injected OS failure; Windows managed backend unsupported and outside core SDK profile'
  if row['id']=='J09':row['completion_policy']='closed stdout evidence is incomplete, never accepted as completed/pass'
  if row['id']=='J01':row['compiler_volume_layer']='controlled proxy delegates real compilation and adds note volume; explicitly not asserted to be original compiler diagnostics'
 # Combine already-valid same-SDK evidence and assert every missing semantic field.
 combined={
 'D08': [('i2b-installed','filter-no-match')],
 'D24': [('matrix-cases','help-json'),('matrix-cases','duplicate-json'),('matrix-cases','duplicate-allow')],
 'P06': [('i2b-installed','success')],
 'M02': [('i2b-installed','success')],
 'R14': [('i2a-installed','compiler-crash')],
 'P12': [('i2a-installed','probe-timeout')]}
 for row in ledger['rows']:
  if row['id'] not in combined:continue
  evidence=[]
  for directory,case in combined[row['id']]:
   path=a.regression/directory/case/'result.json';record=json.loads(path.read_text())
   r=record.get('report') or record.get('receipt')
   if row['id']=='D08':assert r['exit_code']==2 and r['reason']=='no_matches' and r['selection']['candidate_count']>0 and r['summary']['passed']==0
   if row['id']=='D24':assert r['exit_code']==2 and r['tests']==[] and r['errors']
   if row['id']=='P06':assert r['exit_code']==0 and r['identity']['lock_path'] is None and r['identity']['lock_sha256'] is None
   if row['id']=='M02':
    test=r['tests'][0];assert test['compile_mode']=='combined' and test['phases']['compile_link']['state']=='completed'
    for name in ('compile','link'):
     phase=test['phases'][name];assert phase['state']=='not_started' and all(phase[k] is None for k in ('duration_ms','process','exit_code','signal','os_error'))
   if row['id']=='R14':assert r['exit_code']==2 and r['tests'][0]['compile_link']['signal'] and r['tests'][0]['compile_link']['trigger'] is None and r['tests'][1]['result']=='not_run'
   if row['id']=='P12':assert r['exit_code']==2 and r['preparation']['probe']['trigger']=='timeout' and all(t['result']=='not_run' for t in r['tests'])
   evidence.append({'path':str(path),'case':case,'actual_exit_code':r['exit_code'],'evidence_layer':'same-candidate installed regression assertions combined'})
  row['prior_coverage']=row['coverage'];row['prior_evidence']=row['evidence'];row.update(coverage='covered',gap=None,evidence=evidence)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')}
 ledger['full_matrix_pass']=False;ledger['preview_removal_authorized']=False
 ledger['prior_mapping_basis']='accepted 15267dcb row definitions replayed by the same-run regression job on the new SDK; no old runtime result relabeled'
 ledger['note']='New exact result/report/shared-state assertions attached; remaining partial/uncovered rows are still open. SDK-only job has no repository checkout.'
 a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
