#!/usr/bin/env python3
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 for extra in ('toka_test_lock_contract.py','test_toka_test_r08.py','test_toka_test_discovery.py','ledger_toka_test_discovery.py','test_toka_test_dependencies.py','ledger_toka_test_dependencies.py','test_toka_test_lifecycle.py','ledger_toka_test_lifecycle.py'):
  shutil.copyfile(ROOT/'tools/scripts'/extra,a.output/extra)
 name='test_toka_test_i2c_batch.py';shutil.copyfile(ROOT/'tools/scripts'/name,a.output/name)
 shutil.copyfile(ROOT/'tools/scripts/ledger_toka_i2c_batch.py',a.output/'ledger_toka_i2c_batch.py')
 source=ROOT/'docs/toka_test_v1_acceptance.md';rows=[]
 for line in source.read_text().splitlines():
  fields=[x.strip() for x in line.split('|')[1:-1]]
  if fields and re.fullmatch(r'[DPRTAJSMFVB]\d+[a-z]?',fields[0]):rows.append({'id':fields[0],'contract':fields[1:]})
 assert len(rows)==120
 (a.output/'rows.json').write_text(json.dumps(rows,indent=2)+'\n')
 sha=subprocess.check_output(['git','-c','core.fsmonitor=false','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
 (a.output/'harness-identity.json').write_text(json.dumps({'harness_source_sha':sha,'harness_sha256':hashlib.sha256((a.output/name).read_bytes()).hexdigest(),'contract_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'discovery_files_sha256':{n:hashlib.sha256((a.output/n).read_bytes()).hexdigest() for n in ('test_toka_test_discovery.py','ledger_toka_test_discovery.py')},'dependency_files_sha256':{n:hashlib.sha256((a.output/n).read_bytes()).hexdigest() for n in ('test_toka_test_dependencies.py','ledger_toka_test_dependencies.py')},'lifecycle_files_sha256':{n:hashlib.sha256((a.output/n).read_bytes()).hexdigest() for n in ('test_toka_test_lifecycle.py','ledger_toka_test_lifecycle.py')},'r08_script_sha256':hashlib.sha256((a.output/'test_toka_test_r08.py').read_bytes()).hexdigest(),'support_files_sha256':{'toka_test_lock_contract.py':hashlib.sha256((a.output/'toka_test_lock_contract.py').read_bytes()).hexdigest()},'bundled_inputs':['toka_test_lock_contract.py','test_toka_test_r08.py','test_toka_test_discovery.py','ledger_toka_test_discovery.py','test_toka_test_dependencies.py','ledger_toka_test_dependencies.py','test_toka_test_lifecycle.py','ledger_toka_test_lifecycle.py',name,'rows.json','ledger_toka_i2c_batch.py'],'ledger_script_sha256':hashlib.sha256((a.output/'ledger_toka_i2c_batch.py').read_bytes()).hexdigest(),'no_repository_runtime_imports':True},indent=2)+'\n')
if __name__=='__main__':main()
