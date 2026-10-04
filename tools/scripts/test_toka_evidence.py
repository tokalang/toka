#!/usr/bin/env python3
"""Output-only projection controls; installed tests separately run the real compiler."""
import json
import base64
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'lib/toolchain'))
import toka_evidence as evidence


class Scope(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.source=str(self.root/'main.tk');self.other=str(self.root/'dep.tk')
        self.record={'rule':'PAL-CALL-001','operation':'Borrow','decision':'Allow','reason':'DisjointPaths','subject':'x','origin':'y',
                     'primary_location':{'file':self.source,'line':1,'column':1},'origin_location':{'file':self.other,'line':2,'column':1}}
        self.unrelated=dict(self.record,primary_location={'file':self.other,'line':3,'column':1},origin_location={'file':'','line':0,'column':0})
        self.document={'schema':'toka.semantic-evidence','version':1,'records':[self.record,self.unrelated]}
    def tearDown(self):self.tmp.cleanup()
    def project(self,kind='file',target=None,decision=None,exit_code=0):
        return evidence.view(self.document,kind,target or self.source,decision,self.source,None,str(self.root/'sdk'),subprocess.CompletedProcess([],exit_code,b'',b''))
    def test_file_keeps_cross_file_reason_and_does_not_mutate_analysis(self):
        original=json.dumps(self.document);r=self.project();self.assertEqual(len(r['records']),1);self.assertTrue(r['scope']['output_filtered']);self.assertEqual(r['analysis']['records_total'],2);self.assertEqual(json.dumps(self.document),original);self.assertEqual(r['records'][0]['origin_location']['file'],self.other)
    def test_rejections_outside_target_are_retained(self):
        self.unrelated['decision']='Reject';r=self.project(exit_code=1);self.assertEqual(len(r['records']),2);self.assertEqual(r['analysis']['exit_code'],1);self.assertEqual(len(r['analysis']['retained_rejections']),1)
    def test_decision_identifier_and_reason_are_content_stable(self):
        full=self.project('all');chosen=full['records'][0];self.document['records'].reverse();r=self.project('decision',decision=chosen['decision_id']);self.assertEqual(r['records'],[chosen]);self.assertIn(chosen['reason_id'],r['reasons'])
    def test_unknown_decision_does_not_fall_back(self):
        with self.assertRaises(evidence.ConfigurationError):self.project('decision',decision='unknown')
    def test_unknown_existing_target_does_not_fall_back(self):
        with self.assertRaises(evidence.ConfigurationError):self.project(target=str(self.root/'unrelated.tk'))
    def test_empty_valid_entry_is_not_unknown_target(self):
        self.document['records']=[];self.assertEqual(self.project()['records'],[])
    def test_invalid_compiler_document_fails_closed(self):
        self.document['schema']='other'
        with self.assertRaises(RuntimeError):self.project()
    def test_invalid_utf8_argument_still_has_portable_json_and_original_bytes(self):
        output=io.StringIO()
        with contextlib.redirect_stdout(output):code=evidence.main(['--scope','\udcff'])
        report=json.loads(output.getvalue());self.assertEqual(code,2);self.assertEqual(report['result'],'configuration_error');self.assertEqual(base64.b64decode(report['scope']['input_base64'][-1]),b'\xff');self.assertNotIn('\udcff',report['scope']['input'][-1])
    def test_preparation_progress_is_not_compiler_stderr_or_report_stdout(self):
        Path(self.source).write_text('fn main()->i32 { return 0 }\n');(self.root/'package.tk').write_text('pub const PACKAGE=(dependencies=())\n')
        def prepare(*args):print('Waiting for project dependency lock',flush=True);return [],None
        raw=subprocess.CompletedProcess([],0,json.dumps(self.document).encode(),b'compiler warning\n');output=io.StringIO();errors=io.StringIO()
        with patch.object(evidence.Path,'cwd',return_value=self.root),patch.object(evidence,'project_context',prepare),patch.object(evidence.subprocess,'run',return_value=raw),contextlib.redirect_stdout(output),contextlib.redirect_stderr(errors):
            code=evidence.main(['--compiler',str(self.root/'compiler'),'--sdk-lib',str(self.root/'sdk/lib'),self.source])
        report=json.loads(output.getvalue());self.assertEqual(code,0);self.assertIn('Waiting for project dependency lock',errors.getvalue());self.assertEqual(base64.b64decode(report['compiler']['stderr_base64']),raw.stderr)
    def closed_stderr(self,preparation=False):
        Path(self.source).write_text('fn main()->i32 { return 0 }\n');(self.root/'package.tk').write_text('pub const PACKAGE=(dependencies=())\n')
        class Closed:
            def __init__(self):self.buffer=self
            def write(self,*args):raise BrokenPipeError(32,'injected closed stderr')
            def flush(self):raise BrokenPipeError(32,'injected shutdown flush failure')
        def prepare(*args):print('Waiting for project dependency lock',flush=True);return [],None
        raw=subprocess.CompletedProcess([],1,json.dumps(self.document).encode(),b'compiler error\xff\n');output=io.StringIO();closed=Closed()
        args=['--compiler',str(self.root/'compiler'),'--sdk-lib',str(self.root/'sdk/lib'),self.source]
        if not preparation:args.append('--raw-project')
        with patch.object(evidence.Path,'cwd',return_value=self.root),patch.object(evidence,'project_context',prepare),patch.object(evidence.subprocess,'run',return_value=raw) as worker,patch.object(evidence.sys,'stderr',closed),patch.object(evidence.sys,'__stderr__',closed),contextlib.redirect_stdout(output):
            code=evidence.main(args);self.assertIsNot(evidence.sys.stderr,closed);evidence.sys.stderr.close()
        report=json.loads(output.getvalue());self.assertEqual(code,2);self.assertEqual(report['result'],'infrastructure_error');self.assertEqual(report['errors'][0]['category'],'infrastructure_error')
        if preparation:self.assertFalse(worker.called);self.assertIsNone(report['compiler']);self.assertEqual(report['analysis']['result'],'not_started')
        else:
            self.assertEqual(base64.b64decode(report['compiler']['stdout_base64']),raw.stdout);self.assertEqual(base64.b64decode(report['compiler']['stderr_base64']),raw.stderr);self.assertEqual(report['analysis']['exit_code'],1);self.assertEqual(report['analysis']['records_total'],2)
    def test_closed_stderr_after_compiler_retains_capture_and_report(self):self.closed_stderr()
    def test_closed_stderr_during_preparation_has_no_secondary_exception(self):self.closed_stderr(True)
    def test_missing_stderr_after_capture_still_returns_report(self):
        Path(self.source).write_text('fn main()->i32 { return 0 }\n');raw=subprocess.CompletedProcess([],0,json.dumps(self.document).encode(),b'compiler warning\n');output=io.StringIO()
        with patch.object(evidence.subprocess,'run',return_value=raw),patch.object(evidence.sys,'stderr',None),patch.object(evidence.sys,'__stderr__',None),contextlib.redirect_stdout(output):
            code=evidence.main(['--compiler',str(self.root/'compiler'),'--sdk-lib',str(self.root/'sdk/lib'),self.source,'--raw-project']);evidence.sys.stderr.close()
        report=json.loads(output.getvalue());self.assertEqual(code,2);self.assertEqual(report['compiler']['exit_code'],0);self.assertEqual(report['errors'][-1]['channel'],'stderr');self.assertEqual(report['errors'][-1]['errno'],9)


if __name__=='__main__':unittest.main()
