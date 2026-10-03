#!/usr/bin/env python3
import argparse,json
from pathlib import Path
CASES={'T09':['T09'],'T10a':['T10a-'+option+'-'+str(i) for option in ('compile','run') for i in range(5)],'T10b':['T10b'],'T11a':['T11a'],'T11b':['T11b'],'T11c':['T11c'],'T11d':['T11d'],'T13':['T13']}
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--observations',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 ledger=json.loads(a.prior.read_text());data=json.loads((a.observations/'result.json').read_text());assert data['result']=='pass' and len(data['records'])==17
 records={r['name']:r for r in data['records']};assert set(records)==set(sum(CASES.values(),[]))
 for row in ledger['rows']:
  if row['id'] not in CASES:continue
  items=[]
  for name in CASES[row['id']]:
   path=a.observations/name/'result.json';r=json.loads(path.read_text());assert r['contract_pass']==all(r['checks'].values()) and r['contract_pass'] and row['id'] in r['rows']
   items.append({'path':str(path),'case':name,'actual_exit_code':r['exit_code'],'contract_pass':True,'evidence_layer':r['evidence_layer']})
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],coverage='covered',gap=None,evidence=items)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')}
 assert len(ledger['rows'])==120;ledger.update(full_matrix_pass=False,preview_removal_authorized=False)
 a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
