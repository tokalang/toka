"""A1 independent C6 fixtures, narrow resolver codes and consumer rejections."""
import copy
import json
import tempfile
import subprocess
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch

import toka_test_lock_contract as contract
from test_toka_test_i2b import runner
from test_toka_test_i2c_batch import Batch

EXPECTED = ('test.lock_missing', 'test.lock_invalid', 'test.lock_mismatch')


def fixed_report(code='test.lock_missing'):
    phase = {'state':'not_started','duration_ms':None,'process':None,
             'exit_code':None,'signal':None,'os_error':None}
    return {'schema':'toka.test-report','version':1,'run_id':'fixed-A1',
        'preview':True,'finalized':True,'result':'configuration_error',
        'reason':'independent fixture','exit_code':2,'project_root':'/project',
        'artifact_root':'/artifact','identity':{'status':'failed',**dict.fromkeys((
            'tokac_path','tokac_version','tokac_sha256','runtime_object_path',
            'runtime_object_sha256','sdk_root','sdk_version','sdk_revision','lock_path','lock_sha256'))},
        'selection':{'mode':'discovery','filters':[],'candidate_count':1,'selected_count':1,'excluded':[]},
        'supervision':{'backend':'posix_process_group','scope':'direct_child_and_process_group'},
        'timeouts':{'compile_ms':30000,'run_ms':5000,'compile_source':'default','run_source':'default',
                    'terminate_grace_ms':2000,'kill_wait_ms':5000},'diagnostics':[],
        'timings':{name:{'name':name,**phase,'state':'not_started' if name=='execution' else 'aborted' if name=='dependencies' else 'completed',
            'duration_ms':None if name=='execution' else 1.0} for name in ('argument_parse','project','artifact_setup',
            'selection','identity','dependencies','execution','report_preparation','total')},
        'summary':{'total':1,'passed':0,'failed':0,'infrastructure_error':0,
                   'interrupted':0,'not_run':1},
        'tests':[{'id':'tests/a_test.tk','entry':'/project/tests/a_test.tk','result':'not_run',
                  'reason':None,'trigger':'none','interrupt_signal':None,'compile_mode':'unknown',
                  'cleanup':{'status':'not_needed','scope':'none','requested_signals':[],
                    'leader_reaped':None,'group_absent':None,'output_complete':None,'duration_ms':None},
                  'logs':dict.fromkeys(('compile_stdout','compile_stderr','run_stdout','run_stderr')),
                  'diagnostics':[],'phases':{k:{'name':k,**phase} for k in
                    ('compile_link','compile','link','run')}}],
        'errors':[{'code':code,'category':'configuration_error','phase':'context',
                   'message':'Explicit dependency repair required.', 'os_error':None,
                   'source':{'path':None,'origin':'unknown','classification_basis':'none','package_node_id':None}}],
        'termination':{'reason':'configuration_error','phase':'context','trigger':'none','signal':None,'cleanup':None},
        'preparation':{'context':{'phase':{'name':'context','state':'completed','duration_ms':1.0,'exit_code':2,'signal':None,'os_error':None,
            'process':{'leader_pid':42,'pgid':42,'target_pid':42,'target_role':'helper'}}}}}


class LockCodeControls(unittest.TestCase):
    def test_three_fixed_contract_codes_and_message_variations(self):
        self.assertEqual(set(contract.LOCK_CODES.values()),set(EXPECTED))
        for code in EXPECTED:
            for message in ('Explicit fetch required.','请先修复项目依赖锁。','Different wording, no command token.'):
                report=fixed_report(code);report['errors'][0]['message']=message
                contract.require_lock_failure(report,code,2)

    def test_legacy_null_and_unknown_preserve_original_general_failure(self):
        for code,label in [(None,'specific attribution unavailable'),('future.lock','unknown code: future.lock')]:
            report=fixed_report(code);original=copy.deepcopy(report)
            self.assertIs(contract.validate(report,2),report)
            self.assertEqual(contract.attribution(code),label);self.assertEqual(report,original)
            with self.assertRaises(contract.ReportContractError):
                contract.require_lock_failure(report,'test.lock_missing',2)

    def test_candidate_code_absent_empty_wrong_are_rejected(self):
        for code in (None,'','test.lock_invalid','future.lock'):
            with self.subTest(code=code),self.assertRaises(contract.ReportContractError):
                contract.require_lock_failure(fixed_report(code),'test.lock_missing',2)
        report=fixed_report();del report['errors'][0]['code']
        with self.assertRaises(contract.ReportContractError):contract.validate(report,2)

    def test_wrong_protocol_stage_status_and_required_facts(self):
        changes=[('version',2),('version',True),('exit_code',1),('result','passed')]
        for key,value in changes:
            report=fixed_report();report[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(contract.ReportContractError):contract.validate(report,2)
        for field in ('schema','exit_code','errors','summary','termination'):
            report=fixed_report();del report[field]
            with self.subTest(missing=field),self.assertRaises(contract.ReportContractError):contract.validate(report,2)
        for path,value in [(('errors',0,'phase'),'compile_link'),(('errors',0,'category'),'infrastructure_error'),
                           (('termination','phase'),'run'),(('termination','trigger'),'timeout'),
                           (('preparation','context','phase','exit_code'),0),
                           (('preparation','context','phase','signal'),6),
                           (('tests',0,'phases','run','state'),'completed'),
                           (('tests',0,'result'),'compile_failed'),(('summary','not_run'),0)]:
            report=fixed_report();target=report
            for key in path[:-1]:target=target[key]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(contract.ReportContractError):contract.validate(report,2)
        report=fixed_report();del report['tests'][0]['phases']['compile_link']['process']
        with self.assertRaises(contract.ReportContractError):contract.validate(report,2)
        for actual in (0,1):
            with self.subTest(actual=actual),self.assertRaises(contract.ReportContractError):contract.validate(fixed_report(),actual)

    def test_other_faults_cannot_impersonate_lock_or_semantic_rejection(self):
        for message in ('run toka fetch','helper missing','network failed','cache missing','integrity failed','lock wait timeout'):
            report=fixed_report(None);report['errors'][0]['message']=message
            report['result']='infrastructure_error';report['errors'][0]['category']='infrastructure_error'
            contract.validate(report,2)
            with self.assertRaises(contract.ReportContractError):contract.require_lock_failure(report,'test.lock_missing',2)
            report['result']='failed';report['exit_code']=1
            with self.assertRaises(contract.ReportContractError):contract.validate(report,1)

    def test_format_validation_codes_are_generated_at_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock=Path(tmp)/'package.lock'
            for payload in (b'invalid\n',b'toka-lock-v9\n',b'toka-lock-v1\npackage\tx\n',b'\xff'):
                lock.write_bytes(payload)
                with self.subTest(payload=payload),self.assertRaises(runner.packages.PackageConfigurationError) as caught:
                    runner.packages.read_lock(lock)
                self.assertEqual(caught.exception.code,'test.lock_invalid')
            with patch.object(Path,'read_text',side_effect=PermissionError(13,'controlled read denial')):
                with self.assertRaises(PermissionError) as caught:runner.packages.read_lock(lock)
                self.assertIsNone(getattr(caught.exception,'code',None))

    def test_worker_transfers_code_without_message_decoding(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);run=root/'run';run.mkdir()
            error=runner.packages.PackageConfigurationError('unrelated wording',code='test.lock_mismatch')
            with patch.object(runner,'project_context',side_effect=error):
                self.assertEqual(runner.prepare_worker('context',root,root,run,2000),2)
            data=json.loads((run/'context-result.json').read_text())
            self.assertEqual(data['code'],'test.lock_mismatch');self.assertEqual(data['error'],'unrelated wording')

    def test_parent_keeps_unknown_code_and_accepts_legacy_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            phase={'cleanup':{'status':'confirmed'},'interrupt_signal':None,'trigger':None,'exit_code':2,'signal':None,'stderr':'fixture.stderr'}
            supervisor=SimpleNamespace(run=lambda *args:phase)
            for code in ('future.lock',None):
                data={'error':'opaque wording','type':'FutureConfigurationCause','category':'configuration_error'}
                if code is not None:data['code']=code
                (root/'context-result.json').write_text(json.dumps(data))
                with self.assertRaises(runner.ConfigurationError) as caught:
                    runner.run_preparation(supervisor,'context',root,root,root,{},2000,2000)
                self.assertEqual(caught.exception.code,code)

    def test_decode_failures_keep_original_status_and_bytes(self):
        for index,stdout in enumerate((b'null',b'{}',b'[]',b'{invalid',b'{}\n{}')):
            with self.subTest(stdout=stdout),tempfile.TemporaryDirectory() as tmp:
                batch=Batch.__new__(Batch);batch.output=Path(tmp);batch.results=[]
                result=subprocess.CompletedProcess(['toka','test','--json'],2,stdout,b'original stderr')
                with self.assertRaises(contract.ReportContractError):
                    batch.decode_save('bad',Path('/project'),result.args,result)
                folder=batch.output/'bad';data=json.loads((folder/'result.json').read_text())
                self.assertEqual(data['exit_code'],2);self.assertEqual(data['cwd'],'/project')
                self.assertEqual((folder/'stdout').read_bytes(),stdout);self.assertEqual((folder/'stderr').read_bytes(),b'original stderr')
                self.assertIn('consumer_contract_error',data)

    def test_consumer_contract_error_preserves_raw_cli_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            batch=Batch.__new__(Batch);batch.output=Path(tmp);batch.results=[]
            report=fixed_report();report['version']=2;stdout=json.dumps(report).encode();stderr=b'raw stderr\xff'
            with self.assertRaises(contract.ReportContractError):
                batch.save('bad-protocol',Path('/project'),['toka','test','--json'],2,stdout,stderr,report)
            folder=batch.output/'bad-protocol';data=json.loads((folder/'result.json').read_text())
            self.assertEqual(data['exit_code'],2);self.assertEqual(data['report'],report)
            self.assertEqual(data['command'],['toka','test','--json']);self.assertEqual(data['cwd'],'/project')
            self.assertEqual((folder/'stdout').read_bytes(),stdout);self.assertEqual((folder/'stderr').read_bytes(),stderr)
            self.assertEqual(data['consumer_contract_error']['phase'],'consumer_report_validation')


if __name__=='__main__':unittest.main(verbosity=2)
