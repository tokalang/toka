#!/usr/bin/env python3
"""V01-V12 policy controls; synthetic receipts never claim an SDK release passed."""
import argparse,copy,hashlib,json,os,subprocess,sys,tempfile,unittest,zipfile
from pathlib import Path
import release_platform_policy as policy
import verify_qualified_draft as draft
import verify_release_promotion as promote
from test_release_workflow import report,cancellation_receipt

ROOT=Path(__file__).resolve().parents[2];SHA='a'*40;TAG='v0.12.0'


def dump(path,value):path.write_text(json.dumps(value))


def basic(digest):
    fields=['package','basic_dep','path','/controlled/basic-dependency','/controlled/basic-dependency','-',policy.dependency_digest(),'-']
    node='pkg-v1-'+hashlib.sha256('\0'.join(('toka.package-node.v1',*fields[2:7],'')).encode()).hexdigest();lock=hashlib.sha256(('toka-lock-v1\n'+'\t'.join(fields)+'\n').encode()).hexdigest()
    dep={'lock_entry':fields,'package_node_id':node,'lock_sha256_before':lock,'lock_sha256_after':lock,'used_by':['build','run','test_pass','test_fail','test_timeout']}
    checks=[]
    for name in policy.CHECKS:
        row={'name':name,'result':'pass','exit_code':1 if name in ('test_fail','test_timeout') else 0}
        if name.startswith('test_'):
            phase={'state':'completed','exit_code':0,'signal':None,'os_error':None,'process':{'leader_pid':100}}
            running=dict(phase,exit_code=7 if name=='test_fail' else 0)
            if name=='test_timeout':running.update(state='aborted',exit_code=None,signal=15);row.update(trigger='timeout',cleanup='confirmed')
            row['facts']={'schema':'toka.test-report','version':1,'finalized':True,'report_exit_code':row['exit_code'],'report_result':'passed' if name=='test_pass' else 'failed','summary':{'total':1,'passed':1 if name=='test_pass' else 0,'failed':0 if name=='test_pass' else 1,'infrastructure_error':0,'interrupted':0,'not_run':0},'test_count':1,'test_id':'tests/basic_test.tk','test_result':{'test_pass':'passed','test_fail':'run_failed','test_timeout':'timed_out'}[name],'compile_link':phase,'run':running,'trigger':'timeout' if name=='test_timeout' else 'none','cleanup':{'status':'confirmed','leader_reaped':True,'group_absent':True,'output_complete':True},'lock_sha256':lock,'dependency_nodes':[{'alias':'basic_dep','package_node_id':node,**dict(zip(('kind','locator','resolved','archive_sha256','content_sha256'),fields[2:7]))}]}
        checks.append(row)
    return {'schema':'toka.sdk-basic-validation','version':1,'policy_id':policy.policy_id(TAG),'candidate_revision':SHA,'version_label':TAG,'source_dirty':False,'target':'macos-x64','source_run_id':21,'source_run_attempt':2,'archive_sha256':digest,'result':'pass','dependencies':dep,
            'sdk_identity':{'preview_composition':False,'version_label':TAG,'candidate_revision':SHA,'tools':{name:{'version':TAG.removeprefix('v'),'exit_code':0,'sha256':'1'*64,'stdout_sha256':'2'*64,'stderr_sha256':'3'*64} for name in policy.TOOLS}},'checks':checks}


def fixture(root,intel=False):
    targets=policy.CORE+(policy.OPTIONAL,) if intel else policy.CORE
    archives=root/'qualified';archives.mkdir();assets=root/'assets';assets.mkdir();zips=root/'zips';zips.mkdir();metadata=[];hashes={}
    for index,target in enumerate(targets):
        name='toka-%s-%s.tar.gz'%(TAG,target);body=('controlled fixture '+target).encode();directory=archives/('candidate-archive-'+target);directory.mkdir();(directory/name).write_bytes(body);(assets/name).write_bytes(body);hashes[name]=hashlib.sha256(body).hexdigest()
        packed=zips/(str(100+index)+'.zip')
        with zipfile.ZipFile(packed,'w') as archive:archive.writestr(name,body)
        metadata.append({'id':100+index,'name':'candidate-archive-'+target,'expired':False,'digest':'sha256:'+hashlib.sha256(packed.read_bytes()).hexdigest(),'workflow_run':{'id':21 if target==policy.OPTIONAL else 11,'head_sha':SHA}})
    (assets/'SHA256SUMS').write_text(''.join('%s  %s\n'%(hashes[name],name) for name in sorted(hashes)))
    optional=policy.not_run(SHA,TAG)
    if intel:optional.update(status='passed',reason='controlled positive optional receipt',validation_level='basic',receipt=basic(hashes['toka-'+TAG+'-macos-x64.tar.gz']))
    summary={'schema':'toka.release-qualification-summary','version':2,'candidate_revision':SHA,'version_label':TAG,'result':'pass','errors':[],'policy_id':policy.policy_id(TAG),'expected_targets':list(policy.CORE),'expected_core_targets':list(policy.CORE),'source_run_id':11,'source_run_attempt':1,'optional_targets':{'macos-x64':optional},
             **{key:[{'target':target,'result':result} for target in policy.CORE] for key,result in [('reports','pass'),('taskhandle_conformance','pass'),('restricted_cancellation_conformance','candidate-pass')]}}
    if TAG.startswith('v0.13.'):
        for row in summary['reports']:
            row['candidate_013']={'schema':'toka.0.13-candidate-controls','version':1,'result':'pass','candidate_revision':SHA,'version_label':TAG,'build_testing':False,'groups':['A1','B1','B1-boundaries','B1-relative','D1-D2']}
    documents={'summary':summary,'run':{'id':11,'run_attempt':1,'status':'completed','conclusion':'success','head_sha':SHA,'path':'.github/workflows/release.yml','event':'workflow_dispatch','repository':{'full_name':'tokalang/toka'},'head_repository':{'full_name':'tokalang/toka'}},
               'draft':{'tagName':TAG,'isDraft':True,'isPrerelease':False,'assets':[{'name':name} for name in [*hashes,'SHA256SUMS']]},'artifacts':{'artifacts':metadata},
               'optional_run':{'id':21,'run_attempt':2,'head_sha':SHA,'status':'completed','conclusion':'success','event':'workflow_dispatch','path':'.github/workflows/optional_macos_x64.yml'},
               'replay_run':{'id':12,'run_attempt':3,'status':'completed','conclusion':'success','event':'workflow_dispatch','path':'.github/workflows/qualified_artifact_replay.yml'},
               'replay':{'schema':'toka.qualified-artifact-replay-summary','version':2,'policy_id':policy.policy_id(TAG),'result':'pass','errors':[],'candidate_revision':SHA,'version_label':TAG,'qualification_run_id':11,'qualification_run_attempt':1,'replay_run_id':12,'replay_run_attempt':3,
                         'receipts':[{'target':target,'result':'pass','archive_sha256':hashes['toka-%s-%s.tar.gz'%(TAG,target)],'candidate_revision':SHA,'version_label':TAG,'policy_id':policy.policy_id(TAG),'asset_source':'candidate_run','qualification_run_id':11,'qualification_run_attempt':1} for target in targets]}}
    for name,value in documents.items():dump(root/(name+'.json'),value)
    a=argparse.Namespace(tag_name=TAG,candidate_sha=SHA,repository='tokalang/toka',qualification_run_id=11,replay_run_id=12,qualification_summary=root/'summary.json',qualification_run_json=root/'run.json',qualified_archives_dir=archives,draft_json=root/'draft.json',draft_assets_dir=assets,assets_dir=assets,qualification_artifacts_json=root/'artifacts.json',artifact_zips_dir=zips,optional_run_json=root/'optional_run.json',replay_run_json=root/'replay_run.json',replay_receipt=root/'replay.json')
    return a,documents


class Platforms(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def test_V01_three_core_and_explicit_not_run(self):
        a,d=fixture(self.root);self.assertEqual(policy.included_targets(d['summary'],SHA,TAG),policy.CORE);self.assertEqual(len(draft.validate(a)),3);self.assertEqual(promote.validate(a),[])
    def test_V02_core_missing_failed_dirty_and_sha(self):
        a,d=fixture(self.root)
        for key in ('reports','taskhandle_conformance','restricted_cancellation_conformance'):
            for mutation in ('missing','failed'):
                s=copy.deepcopy(d['summary'])
                if mutation=='missing':s[key].pop()
                else:s[key][0]['result']='fail'
                self.assertTrue(policy.summary_errors(s,SHA,TAG))
        self.assertTrue(policy.summary_errors(d['summary'],'b'*40,TAG))
        self.assertTrue(__import__('verify_release_qualification').report_errors(dict(report('linux-x64',SHA,TAG),source_dirty=True),SHA,TAG))
    def test_V03_failed_running_not_run_have_only_core(self):
        a,d=fixture(self.root)
        for state in ('failed','running','not_run'):
            d['summary']['optional_targets']['macos-x64']['status']=state;dump(a.qualification_summary,d['summary']);self.assertEqual(len(draft.validate(a)),3)
    def test_V04_verified_basic_intel_adds_one_package(self):
        a,d=fixture(self.root,True);self.assertEqual(len(draft.validate(a)),4);self.assertEqual(promote.validate(a),[])
    def test_V05_unvalidated_or_wrong_digest_intel_rejected(self):
        a,d=fixture(self.root,True)
        for mutation in ('receipt','digest','preview','version','cleanup'):
            s=copy.deepcopy(d['summary']);r=s['optional_targets']['macos-x64']['receipt']
            if mutation=='receipt':s['optional_targets']['macos-x64']['receipt']=None
            elif mutation=='digest':r['archive_sha256']='0'*64
            elif mutation=='preview':r['sdk_identity']['preview_composition']=True
            elif mutation=='version':r['sdk_identity']['version_label']='v0.11.0'
            else:r['checks'][-1]['cleanup']='failed'
            dump(a.qualification_summary,s)
            with self.assertRaises(ValueError):draft.validate(a)
    def test_V06_exact_assets_and_authenticated_archive_zip(self):
        a,d=fixture(self.root)
        original=(a.assets_dir/'SHA256SUMS').read_bytes();(a.assets_dir/'extra.txt').write_text('unexpected')
        with self.assertRaises(ValueError):draft.validate(a)
        (a.assets_dir/'extra.txt').unlink();d['draft']['assets'].append(d['draft']['assets'][0]);dump(a.draft_json,d['draft'])
        with self.assertRaises(ValueError):draft.validate(a)
        d['draft']['assets'].pop();dump(a.draft_json,d['draft']);packed=next(a.artifact_zips_dir.glob('*.zip'));packed.write_bytes(b'tampered')
        with self.assertRaises(ValueError):draft.validate(a)
    def test_V09_legacy_four_targets_and_intel_replay_unchanged(self):
        import test_release_promotion as legacy
        a,d=legacy.fixture(self.root);self.assertEqual(promote.validate(a),[]);d['receipt']['target']='macos-arm64';legacy.write_json(a.replay_receipt,d['receipt']);self.assertTrue(promote.validate(a));self.assertEqual(policy.core_targets('v0.11.0'),policy.LEGACY)
    def test_V10_attempt_policy_source_and_replay_bytes(self):
        a,d=fixture(self.root)
        for document,key,value in [('run','run_attempt',2),('summary','policy_id','forged'),('replay','qualification_run_attempt',2),('replay','replay_run_attempt',4),('replay','candidate_revision','b'*40)]:
            changed=copy.deepcopy(d[document]);changed[key]=value;dump(self.root/(document+'.json'),changed);self.assertTrue(promote.validate(a));dump(self.root/(document+'.json'),d[document])
        d['replay']['receipts'][0]['archive_sha256']='0'*64;dump(a.replay_receipt,d['replay']);self.assertTrue(promote.validate(a))
    def test_V11_basic_recipe_all_outcomes_and_preview_not_admitted(self):
        state=policy.not_run(SHA,TAG);state.update(status='passed',validation_level='basic',receipt=basic('1'*64));self.assertEqual(policy.optional_errors(state,SHA,TAG),[])
        for index in range(len(policy.CHECKS)):
            changed=copy.deepcopy(state);changed['receipt']['checks'][index]['result']='fail';self.assertTrue(policy.optional_errors(changed,SHA,TAG))
        changed=copy.deepcopy(state);changed['receipt']['schema']='toka.sdk-basic-control';self.assertTrue(policy.optional_errors(changed,SHA,TAG))
    def test_P2_missing_dependency_and_tool_versions_rejected(self):
        state=policy.not_run(SHA,TAG);state.update(status='passed',validation_level='basic',receipt=basic('1'*64))
        for fault in ('missing_dependency','changed_lock','wrong_node','empty_graph','missing_formatter','wrong_lsp'):
            changed=copy.deepcopy(state);r=changed['receipt']
            if fault=='missing_dependency':r.pop('dependencies')
            elif fault=='changed_lock':r['dependencies']['lock_sha256_after']='0'*64
            elif fault=='wrong_node':r['dependencies']['package_node_id']='pkg-v1-'+'0'*64
            elif fault=='empty_graph':r['checks'][-1]['facts']['dependency_nodes']=[]
            elif fault=='missing_formatter':r['sdk_identity']['tools'].pop('tokafmt')
            else:r['sdk_identity']['tools']['tokalsp']['version']='0.11.0'
            self.assertTrue(policy.optional_errors(changed,SHA,TAG),fault)
    def test_P2_wrong_test_phases_rejected(self):
        state=policy.not_run(SHA,TAG);state.update(status='passed',validation_level='basic',receipt=basic('1'*64))
        for fault in ('compile_failed','compile_timeout','not_started','wrong_run_exit','unfinished_report','wrong_test_id','wrong_pass_result'):
            changed=copy.deepcopy(state);checks={c['name']:c for c in changed['receipt']['checks']};f=checks['test_timeout' if fault=='compile_timeout' else 'test_fail']['facts']
            if fault=='compile_failed':f['test_result']='compile_failed';f['compile_link']['exit_code']=1;f['run']['state']='not_started'
            elif fault=='compile_timeout':f['compile_link'].update(state='aborted',exit_code=None,signal=15);f['run'].update(state='not_started',process=None)
            elif fault=='not_started':f['run']['process']=None
            elif fault=='wrong_run_exit':f['run']['exit_code']=8
            elif fault=='unfinished_report':f['finalized']=False
            elif fault=='wrong_test_id':f['test_id']='tests/other.tk'
            else:checks['test_pass']['facts']['test_result']='not_run'
            self.assertTrue(policy.optional_errors(changed,SHA,TAG),fault)
    def test_V12_unknown_missing_or_false_optional_status(self):
        a,d=fixture(self.root)
        for status in ('unknown',None,True):
            s=copy.deepcopy(d['summary']);s['optional_targets']['macos-x64']['status']=status;self.assertTrue(policy.summary_errors(s,SHA,TAG))
        s=copy.deepcopy(d['summary']);s.pop('optional_targets');self.assertTrue(policy.summary_errors(s,SHA,TAG))
    def test_V01_V02_actual_qualification_cli_core_and_legacy(self):
        for label,targets in [(TAG,policy.CORE),('v0.11.0',policy.LEGACY)]:
            evidence=self.root/label;evidence.mkdir()
            for target in targets:
                dump(evidence/('release-gate-'+target+'.json'),report(target,SHA,label))
                dump(evidence/('taskhandle-lifecycle-conformance-'+target+'.json'),{'schema':'toka.taskhandle-lifecycle-conformance','version':1,'candidate_revision':SHA,'result':'pass','contract':{'schema':'toka.taskhandle-lifecycle','version':2,'path':'spec/taskhandle_lifecycle.v2.json','canonical_sha256':'b'*64},'evidence':[{'result':'pass'}]})
                dump(evidence/('toka-restricted-cancellation-'+target+'.json'),cancellation_receipt(SHA))
            output=evidence/'summary.json';argv=[sys.executable,str(ROOT/'tools/scripts/verify_release_qualification.py'),'--evidence-dir',str(evidence),'--revision',SHA,'--version-label',label,'--source-run-id','11','--source-run-attempt','1','--output',str(output)]
            child=subprocess.run(argv,capture_output=True);self.assertEqual(child.returncode,0,child.stderr);summary=json.loads(output.read_text());self.assertEqual(summary['expected_targets'],list(targets));self.assertEqual(summary['version'],2 if label==TAG else 1)
            (evidence/'release-gate-linux-x64.json').unlink();self.assertNotEqual(subprocess.run(argv,capture_output=True).returncode,0)
    def test_V06_actual_asset_cli_checksums_and_optional_digest(self):
        a,d=fixture(self.root,True);argv=[sys.executable,str(ROOT/'tools/scripts/verify_release_assets.py'),'--assets-dir',str(a.assets_dir),'--version-label',TAG,'--qualification-summary',str(a.qualification_summary),'--require-checksums']
        child=subprocess.run(argv,capture_output=True);self.assertEqual(child.returncode,0,child.stderr)
        (a.assets_dir/('toka-'+TAG+'-macos-x64.tar.gz')).write_bytes(b'wrong actual bytes');self.assertNotEqual(subprocess.run(argv,capture_output=True).returncode,0)


if __name__=='__main__':unittest.main()
