#!/usr/bin/env python3
import argparse,json
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--closure',type=Path,required=True);p.add_argument('--contract',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();ledger=json.loads(a.prior.read_text());result=json.loads((a.closure/'result.json').read_text());assert result['result']=='pass_for_closure_candidate'
 identity=json.loads((a.closure/'unicode-0.1.2-identity.json').read_text());assert identity['lock_unchanged'] and identity['version']=='0.1.2'
 fs=json.loads((a.closure/'D27-filesystem.json').read_text());assert len(fs['records'])==3 and all(r['explicit_status']=='passed_rejection' and r['normal_name_creation_succeeded'] for r in fs['records'])
 for record in fs['records']:
  assert record['discovery_status']==('passed_rejection' if record['invalid_name_created'] else 'not_applicable_filesystem_precondition')
  if not record['invalid_name_created']:assert record['creation_error']['errno'] in (1,22,92)
 contract={line.split('|')[1].strip():[p.strip() for p in line.split('|')[2:-1]] for line in a.contract.read_text().splitlines() if line.startswith('| D27 ')}
 for row in ledger['rows']:
  if row['id'] not in ('P01','D27'):continue
  names=['unicode-0.1.2-identity.json','unicode-0.1.2-check/result.json','unicode-0.1.2-build/result.json','unicode-0.1.2-test/result.json','unicode-0.1.2-run/result.json','unicode-0.1.2-offline-test/result.json','unicode-0.1.2-corpus-map.json'] if row['id']=='P01' else ['D27-filesystem.json']
  row.update(prior_coverage=row['coverage'],prior_evidence=row['evidence'],coverage='covered',gap=None,evidence=[{'path':str(a.closure/name),'scope':'real published package migration' if row['id']=='P01' else 'conditional filesystem precondition contract amendment; not an unrun discovery pass'} for name in names])
  if row['id']=='D27':row.update(prior_contract=row['contract'],contract=contract['D27'])
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')};ledger.update(full_matrix_pass=False,preview_removal_authorized=False,closure_contract_independent_review='pending');a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
