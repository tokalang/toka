#!/usr/bin/env python3
import argparse,json
from pathlib import Path
from test_toka_test_eperm_policy import CASES as POLICY_CASES
CASES={'T05':['interrupt-'+fault for fault in ('permission','wait','output')],
       'T06':['timeout-'+fault for fault in ('permission','wait','output')],
       'T07':['residual-runtime','residual-compiler'],'T08':list(POLICY_CASES)}
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--cleanup',type=Path,required=True);p.add_argument('--contract',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 ledger=json.loads(a.prior.read_text());data=json.loads((a.cleanup/'result.json').read_text());assert data['result']=='pass' and len(data['records'])==16
 records={r['name']:r for r in data['records']};assert set(records)==set(sum(CASES.values(),[]))
 contract={line.split('|')[1].strip():[part.strip() for part in line.split('|')[2:-1]] for line in a.contract.read_text().splitlines() if line.startswith('| T')}
 for row in ledger['rows']:
  if row['id'] not in CASES:continue
  items=[]
  for name in CASES[row['id']]:
   item=records[name];path=Path(item['path']);record=json.loads(path.read_text());assert all(record['checks'].values()) and record['exit_code']==item['exit_code']
   items.append({'path':str(path),'case':name,'actual_exit_code':record['exit_code'],'contract_pass':True,'evidence_layer':'installed SDK actual CLI residual' if name.startswith('residual-') else 'installed SDK helper fault injection; actual compiler/runtime and OS probes'})
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],prior_contract=row['contract'],contract=contract[row['id']],coverage='covered',gap=None,evidence=items)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')}
 assert len(ledger['rows'])==120;ledger.update(full_matrix_pass=False,preview_removal_authorized=False)
 a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
