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


if __name__=='__main__':unittest.main()
