"""A1 P2 controls: real standalone bundles and explicit legacy/A1 observation."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch

from test_toka_test_dependencies import Dependencies
from test_toka_test_lock_codes import fixed_report
from toka_test_lock_contract import require_p05_failure, ReportContractError

ROOT=Path(__file__).resolve().parents[2]


class DeliveryControls(unittest.TestCase):
    def test_actual_bundles_start_without_checkout(self):
        with tempfile.TemporaryDirectory(prefix='a1-isolated-bundles-') as temp:
            outside=Path(temp);env=dict(os.environ);env.pop('PYTHONPATH',None)
            for kind in ('dependencies','discovery','full'):
                bundle=outside/kind
                if kind=='full':
                    subprocess.run([sys.executable,str(ROOT/'tools/scripts/bundle_toka_i2c_batch.py'),
                                    '--output',str(bundle)],cwd=outside,env=env,check=True,capture_output=True)
                    identity=json.loads((bundle/'harness-identity.json').read_text())
                    self.assertIn('toka_test_lock_contract.py',identity['bundled_inputs'])
                    hashes=identity['support_files_sha256']
                else:
                    workflow=ROOT/('.github/workflows/test_0_12_i2c_'+kind+'.yml')
                    block=re.search(r"python3 - <<'PYCODE'\n(.*?)\n +PYCODE",workflow.read_text(),re.S).group(1)
                    setup=dict(env,RUNNER_TEMP=str(outside/('prepare-'+kind)),GITHUB_SHA=subprocess.check_output(['git','-c','core.fsmonitor=false','rev-parse','HEAD'],cwd=ROOT,text=True).strip())
                    Path(setup['RUNNER_TEMP']).mkdir()
                    subprocess.run([sys.executable,'-c',textwrap.dedent(block)],cwd=ROOT,env=setup,check=True,capture_output=True)
                    (Path(setup['RUNNER_TEMP'])/'harness').rename(bundle)
                    hashes=json.loads((bundle/'identity.json').read_text())['files']
                module=bundle/'toka_test_lock_contract.py'
                self.assertEqual(hashes[module.name],hashlib.sha256(module.read_bytes()).hexdigest())
                for script in bundle.glob('*.py'):
                    if script.name=='toka_test_lock_contract.py':continue
                    command=[sys.executable,str(script),'--help']
                    result=subprocess.run(command,cwd=outside,env=env,capture_output=True)
                    retain=os.environ.get('TOKA_A1_DELIVERY_EVIDENCE')
                    if retain:
                        folder=Path(retain)/(kind+'-'+script.stem);folder.mkdir(parents=True,exist_ok=True)
                        (folder/'stdout').write_bytes(result.stdout);(folder/'stderr').write_bytes(result.stderr)
                        (folder/'receipt.json').write_text(json.dumps({'argv':command,'cwd':str(outside),
                            'exit_code':result.returncode,'module_sha256':hashes[module.name],
                            'checkout_on_PYTHONPATH':False,'bundle':str(bundle)},indent=2))
                    with self.subTest(kind=kind,script=script.name):
                        self.assertEqual(result.returncode,0,result.stderr.decode())
                if os.environ.get('TOKA_A1_DELIVERY_EVIDENCE'):
                    import shutil
                    shutil.copytree(bundle,Path(os.environ['TOKA_A1_DELIVERY_EVIDENCE'])/('actual-bundle-'+kind))

    def test_explicit_contract_not_selected_by_returned_code(self):
        legacy=fixed_report(None)
        require_p05_failure(legacy,'missing',2,'legacy')
        with self.assertRaises(ReportContractError):require_p05_failure(legacy,'missing',2,'a1')
        for mode in ('legacy','a1'):
            bad=fixed_report(None);bad['result']='infrastructure_error';bad['errors'][0]['category']='infrastructure_error'
            with self.subTest(mode=mode),self.assertRaises(ReportContractError):require_p05_failure(bad,'missing',2,mode)

    def test_observe_continues_and_keeps_A1_gaps(self):
        with tempfile.TemporaryDirectory() as tmp:
            batch=Dependencies.__new__(Dependencies);batch.output=Path(tmp);batch.sdk=Path('/fixed-sdk')
            batch.env={};batch.results=[];batch.p05_contract='a1';batch.observe=True
            root=batch.output/'project';root.mkdir()
            for name,code in [('missing',None),('malformed',''),('stale','future.lock')]:
                report=fixed_report(code);report['artifact_root']=None
                result=subprocess.CompletedProcess([],2,json.dumps(report).encode(),b'original stderr')
                with patch('test_toka_test_dependencies.subprocess.run',return_value=result):
                    batch.invoke('P05-'+name,root,code=2,rows=['P05'],
                        extra=lambda r,name=name:{'P05':require_p05_failure(r,name,2,'a1') is r})
                record=batch.results[-1];self.assertFalse(record['contract_pass'])
                self.assertEqual(record['report']['errors'][0]['code'],code)
                self.assertEqual(record['exit_code'],2);self.assertIn('consumer_contract_error',record)
            report=fixed_report();report['artifact_root']=None;del report['errors'][0]['code']
            result=subprocess.CompletedProcess([],2,json.dumps(report).encode(),b'field missing')
            with patch('test_toka_test_dependencies.subprocess.run',return_value=result):
                batch.invoke('P05-field-absent',root,code=2,rows=['P05'])
            self.assertFalse(batch.results[-1]['contract_pass'])
            self.assertEqual(len(batch.results),4)

    def test_strict_A1_does_not_suppress_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            batch=Dependencies.__new__(Dependencies);batch.output=Path(tmp);batch.sdk=Path('/fixed-sdk')
            batch.env={};batch.results=[];batch.p05_contract='a1';batch.observe=False
            root=batch.output/'project';root.mkdir();report=fixed_report(None);report['artifact_root']=None
            result=subprocess.CompletedProcess([],2,json.dumps(report).encode(),b'original stderr')
            with patch('test_toka_test_dependencies.subprocess.run',return_value=result),self.assertRaises(ReportContractError):
                batch.invoke('P05-missing',root,code=2,rows=['P05'],extra=lambda r:{'P05':require_p05_failure(r,'missing',2,'a1') is r})
            self.assertFalse(batch.results[-1]['contract_pass'])


if __name__=='__main__':unittest.main(verbosity=2)
