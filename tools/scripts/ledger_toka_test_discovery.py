#!/usr/bin/env python3
"""Close exact discovery rows only when every required recorded variant passes."""
import argparse,json
from pathlib import Path
CASES={'D03':['D03'],'D04':['D04'],'D05':['D05'],'D06':['D06'],
 'D10':['D10-no-tests','D10-helper-only'],'D11':['D11-no-tests','D11-helper-only'],
 'D12':['D12-discovery','D12-explicit','D12-hardlinks'],'D14':['D14'],
 'D15':['D15-file','D15-file-allow','D15-directory','D15-directory-allow'],
 'D16':['D16-outside','D16-parent','D16-directory','D16-missing','D16-suffix'],
 'D17':['D17-A04'],'A04':['D17-A04'],'D19':['D19'],
 'D22':['D22-missing-main','D22-missing-main-explicit','D22-signature','D22-signature-explicit'],
 'D26':['D26-file','D26-directory','D26-tests-root'],'D27':['D27-discovery','D27-explicit']}
def main():
 p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--discovery',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 ledger=json.loads(a.prior.read_text());summary=json.loads((a.discovery/'result.json').read_text());results={r['name']:r for r in summary['records']};assert len(results)==len(summary['records'])==len(set(c for v in CASES.values() for c in v))
 for row in ledger['rows']:
  if row['id'] not in CASES:continue
  items=[];closed=True
  for name in CASES[row['id']]:
   path=a.discovery/name/'result.json';record=json.loads(path.read_text());assert row['id'] in record['rows']
   assert record['checks']==results[name]['checks'] and record['contract_pass']==all(record['checks'].values())
   if row['id']=='D26':
    witness=json.loads((a.discovery/name/'permission-witness.json').read_text());assert witness['uid']!=0 and witness['mode']==0 and witness['errno']==13
   closed &= record['contract_pass'] and record.get('fixture_available',True)
   items.append({'path':str(path),'case':name,'actual_exit_code':record['exit_code'],'contract_pass':record['contract_pass'],'fixture_available':record.get('fixture_available',True),'failed_assertions':[k for k,v in record['checks'].items() if not v],'evidence_layer':'installed CLI; actual filesystem/process outcomes'})
  row['prior_coverage']=row['coverage'];row['prior_evidence']=row['evidence'];row['evidence']=items
  row['coverage']='covered' if closed else 'partial';row['gap']=None if closed else 'Required assertions failed or a real filesystem fixture was unavailable; inspect case receipts. No failure or unavailable fixture counts as completed coverage.'
 ledger['coverage_counts']={k:sum(r['coverage']==k for r in ledger['rows']) for k in ('covered','partial','uncovered','not_applicable','outside_i2c')}
 ledger.update(full_matrix_pass=False,preview_removal_authorized=False,discovery_increment_contract_pass=summary['contract_pass'])
 a.output.write_text(json.dumps(ledger,indent=2)+'\n')
if __name__=='__main__':main()
