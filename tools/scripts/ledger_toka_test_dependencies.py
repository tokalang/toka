#!/usr/bin/env python3
import argparse,json
from pathlib import Path
CASES={'P02':['P02-before','P02-moved'],'P03':['P03-installed','P03-archive-only'],'P04':['P04'],
'P05':['P05-missing','P05-malformed','P05-stale'],'P07':['P07'],
'P08':['P08-installed','P08-archive','P08-download','P08-catalog','P08-extracted'],
'P09':['P09-user','P09-dependency','P09-sdk','P09-unknown'],
'P10':['P10-'+s for s in ('test','package','process','report','safe','native','cc','pkg-config','git','compiler','python')],
'P11':['P11-compiler-read','P11-runtime-read','P11-version'],'P14a':['P14a']}
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--dependencies',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();ledger=json.loads(a.prior.read_text());summary=json.loads((a.dependencies/'result.json').read_text());assert len(summary['records'])==33
 for row in ledger['rows']:
  if row['id'] not in CASES:continue
  items=[];valid=True
  for name in CASES[row['id']]:
   path=a.dependencies/name/'result.json';r=json.loads(path.read_text());assert row['id'] in r['rows'] and r['contract_pass']==all(r['checks'].values());valid &= r['contract_pass']
   items.append({'path':str(path),'case':name,'actual_exit_code':r['exit_code'],'contract_pass':r['contract_pass'],'evidence_layer':r['evidence_layer'],'failed_assertions':[k for k,v in r['checks'].items() if not v]})
  row.update(prior_evidence=row['evidence'],prior_coverage=row['coverage'],coverage='covered' if valid else 'partial',gap=None if valid else 'Required dependency assertions failed; original results preserved.',evidence=items)
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')};ledger.update(full_matrix_pass=False,preview_removal_authorized=False)
 a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
