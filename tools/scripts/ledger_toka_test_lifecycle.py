#!/usr/bin/env python3
import argparse,json
from pathlib import Path
CASES={'R06':['R06'],'R07':['R07'],'T01':['T01'],'T02':['T02'],'T03':['T03'],'T04':['T04-compile','T04-dependencies','T04-selection']}
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--lifecycle',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();ledger=json.loads(a.prior.read_text());data=json.loads((a.lifecycle/'result.json').read_text());assert len(data['records'])==8
 for row in ledger['rows']:
  if row['id'] not in CASES:continue
  items=[];valid=True
  for name in CASES[row['id']]:
   path=a.lifecycle/name/'result.json';r=json.loads(path.read_text());assert row['id'] in r['rows'] and r['contract_pass']==all(r['checks'].values());valid &= r['contract_pass']
   items.append({'path':str(path),'case':name,'actual_exit_code':r['exit_code'],'contract_pass':r['contract_pass'],'failed_assertions':[k for k,v in r['checks'].items() if not v],'evidence_layer':r['evidence_layer']})
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],coverage='covered' if valid else 'partial',gap=None if valid else 'Required lifecycle assertions failed; original failure retained.',evidence=items)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')};ledger.update(full_matrix_pass=False,preview_removal_authorized=False);a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
