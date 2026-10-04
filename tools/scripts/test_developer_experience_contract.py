#!/usr/bin/env python3
"""Gate negative controls and lossless command retention at semantic assertions."""
import base64,contextlib,io,json,subprocess,sys,tempfile,unittest
from pathlib import Path
import test_developer_experience as gate


def fixture(compile_failure=False):
    report={'schema':'toka.test-report','version':1,'preview':True,'finalized':True,
            'result':'failed' if compile_failure else 'configuration_error','exit_code':1 if compile_failure else 2}
    if compile_failure:
        report.update(summary={'total':1,'passed':0,'failed':1,'infrastructure_error':0,'interrupted':0,'not_run':0},tests=[{'result':'compile_failed','phases':{
            'compile_link':{'state':'completed','exit_code':1,'signal':None},
            'run':{'state':'not_started','duration_ms':None,'exit_code':None,'signal':None,'os_error':None,'process':None}}}])
    return report


class Contract(unittest.TestCase):
    def setUp(self):gate._last_command=None;gate._diagnostics_dir=None
    def validate(self,report,compile_failure=False,stdout=None,stderr='Preview: project tests\n'):
        code=1 if compile_failure else 2
        result=subprocess.CompletedProcess(['toka','test','--json'],code,json.dumps(report) if stdout is None else stdout,stderr)
        with contextlib.redirect_stderr(io.StringIO()):return gate.preview_report(result,'failed' if compile_failure else 'configuration_error',code,compile_failure)
    def test_current_configuration_and_compile_boundaries(self):
        self.validate(fixture());self.validate(fixture(True),True)
    def test_preview_status_and_finalization_required(self):
        for key,value in [('preview',False),('preview',None),('preview',1),('finalized',False),('version',True),('result','passed'),('exit_code',0)]:
            with self.subTest(key=key,value=value):
                data=fixture();data[key]=value
                with self.assertRaises(RuntimeError):self.validate(data)
        with self.assertRaises(RuntimeError):self.validate(fixture(),stderr='')
    def test_stdout_must_be_one_legal_report(self):
        for text in ('invalid','{}\n{}','progress\n'+json.dumps(fixture()),'[]'):
            with self.subTest(text=text),self.assertRaises(RuntimeError):self.validate(fixture(),stdout=text)
    def test_compile_and_unstarted_run_cannot_be_substituted(self):
        for phase,key,value in [('compile_link','exit_code',0),('compile_link','state','aborted'),('compile_link','signal',6),('run','state','completed'),('run','duration_ms',0),('run','exit_code',0),('run','process',{})]:
            with self.subTest(phase=phase,key=key):
                data=fixture(True);data['tests'][0]['phases'][phase][key]=value
                with self.assertRaises(RuntimeError):self.validate(data,True)
        data=fixture(True);data['tests'][0]['result']='run_failed'
        with self.assertRaises(RuntimeError):self.validate(data,True)
    def test_semantic_failure_retains_original_context_and_non_utf8(self):
        with tempfile.TemporaryDirectory(prefix='toka-gate-diagnostics-') as tmp:
            root=Path(tmp);gate._diagnostics_dir=root/'logs';argv=[sys.executable,'-c',"import os; os.write(1,b'out\\xff'); os.write(2,b'err\\xfe')"]
            gate.run(argv,root);stderr=io.StringIO()
            with contextlib.redirect_stderr(stderr),self.assertRaises(RuntimeError):gate.require(False,'controlled assertion failure')
            row=json.loads((root/'logs/0001.json').read_text());self.assertEqual(row['argv'],argv);self.assertEqual(row['cwd'],str(root.resolve()));self.assertEqual(row['actual_exit_code'],0);self.assertEqual(row['expected_exit_code'],0)
            self.assertEqual(base64.b64decode(row['stdout_base64']),b'out\xff');self.assertEqual(base64.b64decode(row['stderr_base64']),b'err\xfe');self.assertEqual((root/'logs/0001.stdout').read_bytes(),b'out\xff');self.assertEqual((root/'logs/0001.stderr').read_bytes(),b'err\xfe')
            self.assertIn('Failing command context:',stderr.getvalue());self.assertIn(row['stdout_base64'],stderr.getvalue())
if __name__=='__main__':unittest.main()
