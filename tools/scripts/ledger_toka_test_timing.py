#!/usr/bin/env python3
import argparse,json
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--timing',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();ledger=json.loads(a.prior.read_text());data=json.loads((a.timing/'result.json').read_text());assert data['result']=='pass' and data['measured_samples']==10 and data['clock_controls']==1
 for row in ledger['rows']:
  if row['id'] not in ('M04','M05'):continue
  names=['M04'] if row['id']=='M04' else ['M05-'+mode+'-'+str(i) for i in range(1,6) for mode in ('cold','warm')]
  items=[]
  for name in names:
   path=a.timing/name/'result.json';r=json.loads(path.read_text());assert r['contract_pass'] and all(r['checks'].values());items.append({'path':str(path),'case':name,'actual_exit_code':r['exit_code'],'contract_pass':True,'evidence_layer':r['evidence_layer']})
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],coverage='covered',gap=None,evidence=items)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')};ledger.update(full_matrix_pass=False,preview_removal_authorized=False);a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
