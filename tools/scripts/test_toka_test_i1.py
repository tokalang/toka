#!/usr/bin/env python3
"""I1 discovery/context/serial/artifact tests; lifecycle/JSON are explicitly not covered."""
import argparse
import contextlib
import io
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'lib/toolchain'))
import toka_test as runner
import toka_package as packages


def manifest(root, dependencies=''):
    root.mkdir(parents=True, exist_ok=True)
    (root / 'package.tk').write_text('pub const PACKAGE = (name="i1", version="1.0.0", dependencies=(%s))\n' % dependencies)


def source(root, name='tests/ok_test.tk', content='fn main() -> i32 { return 0 }\n'):
    path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(content); return path


@unittest.skipUnless(os.name == 'posix', 'I1 executable preview is native POSIX only')
class I1Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='toka-i1-')
        self.root = Path(self.temp.name) / 'project'; manifest(self.root)
        self.sdk = Path(self.temp.name) / 'sdk'; self.sdk.mkdir()
        self.compiler = Path(self.temp.name) / 'compiler'
        self.compiler.write_text('''#!/usr/bin/env python3
import json,os,pathlib,sys
args=sys.argv[1:]
entry=next(pathlib.Path(a) for a in args if a.endswith('.tk') and '=' not in a)
data=entry.read_text()
log=pathlib.Path(os.environ['TOKA_TEST_RUN_DIR'])/'compile-order.jsonl'
with log.open('a') as s:s.write(json.dumps({'entry':str(entry),'args':args,'cwd':os.getcwd()})+'\\n')
if 'COMPILE_FAIL' in data:
 print('controlled compile failure',file=sys.stderr);sys.exit(1)
exe=pathlib.Path(args[args.index('-o')+1])
code=7 if 'RUN_FAIL' in data else 0
exe.write_text('#!'+sys.executable+'\\nimport os,sys\\nprint(os.getcwd())\\nprint(os.environ["TOKA_TEST_CASE_DIR"])\\nsys.exit('+str(code)+')\\n')
exe.chmod(0o755)
'''); self.compiler.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def invoke(self, args, cwd=None):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return runner.execute_preview(args,self.sdk,self.compiler,cwd or self.root)

    def receipts(self):
        return [json.loads(p.read_text()) for p in sorted((self.root/'.toka/test-runs').glob('*/preview.json'))]

    def test_discovery_order_helpers_nested_hidden(self):
        source(self.root,'tests/z_test.tk');source(self.root,'tests/a_test.tk')
        source(self.root,'tests/helper.tk','COMPILE_FAIL')
        source(self.root,'tests/.hidden/x_test.tk')
        manifest(self.root/'tests/nested');source(self.root,'tests/nested/ignored_test.tk','COMPILE_FAIL')
        self.assertEqual(self.invoke([]),0)
        r=self.receipts()[0];self.assertEqual([t['id'] for t in r['tests']],['tests/.hidden/x_test.tk','tests/a_test.tk','tests/z_test.tk'])
        self.assertIn({'path':'tests/nested','reason':'nested_project'},r['selection']['excluded'])
        orders=[json.loads(x)['entry'] for x in (Path(r['artifact_root'])/'compile-order.jsonl').read_text().splitlines()]
        self.assertEqual(orders,[t['entry'] for t in r['tests']])

    def test_explicit_replaces_discovery_deduplicates_spaces(self):
        path=source(self.root,'fixtures/space name.tk');source(self.root,content='COMPILE_FAIL')
        self.assertEqual(self.invoke([str(path),'fixtures/space name.tk']),0)
        self.assertEqual(len(self.receipts()[0]['tests']),1)

    def test_literal_or_filters_empty_policy(self):
        source(self.root,'tests/a_test.tk');source(self.root,'tests/b_test.tk');source(self.root,'tests/c_test.tk')
        self.assertEqual(self.invoke(['--filter','a_','--filter','b_']),0)
        self.assertEqual(len(self.receipts()[0]['tests']),2)
        self.assertEqual(self.invoke(['--filter','*']),2)
        self.assertEqual(self.invoke(['--filter','*','--allow-empty']),0)
        empty=self.receipts()[-1] if self.receipts()[-1]['result']=='empty' else next(r for r in self.receipts() if r['result']=='empty')
        self.assertEqual(empty['selection']['selected_count'],0)

    def test_empty_and_help_no_execution(self):
        self.assertEqual(self.invoke([]),2); self.assertEqual(self.invoke(['--allow-empty']),0)
        count=len(self.receipts());self.assertEqual(self.invoke(['--help']),0);self.assertEqual(len(self.receipts()),count)
        self.assertFalse((self.root/'.toka/does-not-exist').exists())

    def test_symlinks_and_outside_and_nested_explicit(self):
        path=source(self.root);link=self.root/'tests/link_test.tk';link.symlink_to(path)
        self.assertEqual(self.invoke([]),0)
        with self.assertRaises(runner.PreviewError):self.invoke([str(link),'--allow-empty'])
        outside=source(Path(self.temp.name)/'outside')
        with self.assertRaises(runner.PreviewError):self.invoke([str(outside)])
        manifest(self.root/'inner');nested=source(self.root/'inner')
        with self.assertRaises(runner.PreviewError):self.invoke([str(nested)])
        with self.assertRaises(runner.PreviewError):self.invoke([str(path),'missing.tk','--filter','ok'])

    def test_relative_subdirectory_and_dash_entry(self):
        source(self.root);directory=self.root/'src';directory.mkdir()
        self.assertEqual(self.invoke(['../tests/ok_test.tk'],directory),0)
        source(self.root,'-entry.tk');self.assertEqual(self.invoke(['--','-entry.tk']),0)
        with self.assertRaises(runner.PreviewError):self.invoke(['--','--json'])

    def test_options_do_not_promise_i2(self):
        for args in [['--json'],['--run-timeout-ms','5'],['--compile-timeout-ms','5'],['--allow-empty','--allow-empty'],['--filter',''],['--help','ok.tk']]:
            with self.assertRaises(runner.PreviewError):self.invoke(args)

    def test_failures_continue_serial_and_logs_retained(self):
        source(self.root,'tests/1_test.tk','COMPILE_FAIL');source(self.root,'tests/2_test.tk','RUN_FAIL');source(self.root,'tests/3_test.tk')
        self.assertEqual(self.invoke([]),1);r=self.receipts()[0]
        self.assertEqual([t['result'] for t in r['tests']],['compile_failed','run_failed','passed'])
        self.assertEqual(r['tests'][1]['run']['exit_code'],7)
        for t in r['tests']:
            for name in ['compile_link','run']:
                if name in t:
                    for stream in ['stdout','stderr']:self.assertTrue(Path(t[name][stream]).is_file())
        self.assertNotIn('run',r['tests'][0])
        self.assertTrue(r['preview']);self.assertEqual(r['supervision'],'not_implemented_i1')

    def test_manifest_changes_and_extra_locked_nodes_are_rejected(self):
        dep=Path(self.temp.name)/'dep';manifest(dep);source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
        manifest(self.root,'dep="../dep",');source(self.root)
        packages.Resolver(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',offline=False,refresh=False).run()
        original=(self.root/'package.lock').read_bytes()
        manifest(self.root)
        with self.assertRaises(packages.PackageError):self.invoke([])
        self.assertEqual((self.root/'package.lock').read_bytes(),original)
        self.assertTrue(dep.is_dir())

    def test_state_symlink_is_rejected_and_unicode_paths_work(self):
        source(self.root,'tests/汉字_test.tk');self.assertEqual(self.invoke([]),0)
        outside=Path(self.temp.name)/'elsewhere';outside.mkdir()
        shutil.rmtree(self.root/'.toka');(self.root/'.toka').symlink_to(outside,target_is_directory=True)
        with self.assertRaises(runner.PreviewError):self.invoke([])
        self.assertEqual(list(outside.iterdir()),[])

    def test_failed_launch_stops(self):
        source(self.root,'tests/a_test.tk');source(self.root,'tests/b_test.tk');self.compiler.unlink()
        with self.assertRaises(OSError):self.invoke([])
        r=self.receipts()[0];self.assertEqual(r['exit_code'],2)
        self.assertEqual([t['result'] for t in r['tests']],['infrastructure_error','not_run'])

    def test_lock_is_frozen_and_context_matches_package_helper(self):
        dep=Path(self.temp.name)/'dep';manifest(dep)
        source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
        manifest(self.root,'dep="../dep",');source(self.root)
        resolver=packages.Resolver(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',offline=False,refresh=False)
        resolver.run();before=(self.root/'package.lock').read_bytes()
        self.assertEqual(self.invoke([]),0)
        r=self.receipts()[0];args=r['tests'][0]['compile_link']['command']
        for value in packages.compiler_mappings(self.root/'package.lock',self.root/'.toka'):self.assertIn(value,args)
        for value in packages.compiler_node_mappings(self.root/'package.lock'):self.assertIn(value,args)
        self.assertEqual((self.root/'package.lock').read_bytes(),before)
        source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 43 }\n')
        with self.assertRaises(packages.PackageError):self.invoke([])
        self.assertEqual((self.root/'package.lock').read_bytes(),before)

    def test_missing_stale_malformed_lock_never_rewritten(self):
        dep=Path(self.temp.name)/'dep';manifest(dep);manifest(self.root,'dep="../dep",');source(self.root)
        with self.assertRaises(packages.PackageError):self.invoke([])
        self.assertFalse((self.root/'package.lock').exists())
        (self.root/'package.lock').write_text('bad lock')
        with self.assertRaises(packages.PackageError):self.invoke([])
        self.assertEqual((self.root/'package.lock').read_text(),'bad lock')

    def test_concurrent_calls_independent_artifacts(self):
        source(self.root,'tests/space name_test.tk')
        helper=ROOT/'lib/toolchain/toka_test.py'
        command=[sys.executable,str(helper),'--sdk-lib',str(self.sdk),'--tokac',str(self.compiler),'--']
        a=subprocess.Popen(command,cwd=self.root,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        b=subprocess.Popen(command,cwd=self.root,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        ao,ae=a.communicate(timeout=15);bo,be=b.communicate(timeout=15)
        self.assertEqual(a.returncode,0,(ao,ae));self.assertEqual(b.returncode,0,(bo,be))
        records=self.receipts();self.assertEqual(len(records),2)
        self.assertNotEqual(records[0]['artifact_root'],records[1]['artifact_root'])
        for r in records:
            case=Path(r['tests'][0]['run']['stdout']).read_text();self.assertIn(r['artifact_root'],case);self.assertIn(str(self.root),case)
        self.assertFalse((self.root/'.toka_test_exe').exists());self.assertFalse((self.root/'.toka_test_runner.sh').exists())

    def test_missing_sdk_module_is_infrastructure_error(self):
        helper=Path(self.temp.name)/'broken_sdk/toka_test.py';helper.parent.mkdir()
        shutil.copyfile(ROOT/'lib/toolchain/toka_test.py',helper)
        env=dict(os.environ);env.pop('PYTHONPATH',None)
        p=subprocess.run([sys.executable,str(helper),'--sdk-lib',str(self.sdk),'--tokac',str(self.compiler),'--'],cwd=self.root,env=env,capture_output=True,text=True,timeout=10)
        self.assertEqual(p.returncode,2)
        self.assertIn('package helper is unavailable',p.stderr)
        self.assertNotIn('[FAILED (Compile)]',p.stdout)

    def test_native_inputs_require_packaged_helper(self):
        dep=Path(self.temp.name)/'dep';manifest(dep)
        source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
        (dep/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=(),native=(required=true,pkg_config=("zlib")))\n')
        manifest(self.root,'dep="../dep",');source(self.root)
        packages.Resolver(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',offline=False,refresh=False).run()
        with self.assertRaisesRegex(runner.PreviewError,'native build helper'):self.invoke([])
        self.assertEqual(self.receipts()[0]['tests'][0]['result'],'not_run')


def installed_sdk_test(sdk, output):
    """Real relocated SDK; no compiler/stdlib source directory or custom -I invocation."""
    env={k:v for k,v in os.environ.items() if not k.startswith('TOKA')}
    env['PATH']=str(sdk/'bin')+os.pathsep+env['PATH']
    with tempfile.TemporaryDirectory(prefix='i1-sdk-external-') as tmp:
        root=Path(tmp);project=root/'consumer'; manifest(project)
        source(project,'tests/ok_test.tk');source(project,'tests/fail_test.tk','fn main() -> i32 { return 7 }\n')
        source(project,'tests/helpers.tk','this is not an executable entry\n')
        command=['toka','test','--filter','ok_']
        r=subprocess.run(command,cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if r.returncode!=0:raise RuntimeError(r.stdout+r.stderr)
        if 'Preview:' not in r.stderr:raise RuntimeError('installed runner hid Preview status')
        dep=root/'dependency';manifest(dep)
        source(dep,'lib/official/dep.tk','pub fn answer() -> i32 { return 42 }\n')
        manifest(project,'dep="../dependency",')
        fetch=subprocess.run(['toka','fetch'],cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if fetch.returncode:raise RuntimeError(fetch.stdout+fetch.stderr)
        before=(project/'package.lock').read_bytes()
        source(project,'tests/dep_test.tk','import official/dep::{answer}\nfn main() -> i32 { if answer() != 42 { return 1 } return 0 }\n')
        child=project/'src';child.mkdir();r=subprocess.run(['toka','test','../tests/dep_test.tk'],cwd=child,env=env,capture_output=True,text=True,timeout=120)
        if r.returncode or (project/'package.lock').read_bytes()!=before:raise RuntimeError(r.stdout+r.stderr)
        r=subprocess.run(['toka','test','tests/fail_test.tk'],cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if r.returncode!=1:raise RuntimeError(r.stdout+r.stderr)
        r=subprocess.run(['toka','test','--filter','nothing','--allow-empty'],cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        # A controlled immutable registry fixture validates cache/lock plumbing;
        # it does not replace or repair the separately retained Unicode failure.
        registry=root/'registry';registry.mkdir();package=root/'registry_source';manifest(package)
        source(package,'lib/official/reg.tk','pub fn answer() -> i32 { return 42 }\n')
        archive=registry/'reg-1.0.0.tar.gz'
        with tarfile.open(archive,'w:gz') as stream:
            for path in sorted(package.rglob('*')):
                if path.is_file():stream.add(path,arcname=path.relative_to(package).as_posix())
        archive_hash=hashlib.sha256(archive.read_bytes()).hexdigest()
        (registry/'catalog.json').write_text(json.dumps({'packages':[{'name':'reg','version':'1.0.0','latest_version':'1.0.0','installable':True,'versions':[{'version':'1.0.0','tarball_url':archive.as_uri(),'sha256':archive_hash}]}]}))
        manifest(project,'dep="../dependency",reg="reg:1.0.0",')
        online=dict(env,TOKA_REGISTRY_URL=registry.as_uri())
        fetch=subprocess.run(['toka','fetch'],cwd=project,env=online,capture_output=True,text=True,timeout=120)
        if fetch.returncode:raise RuntimeError(fetch.stdout+fetch.stderr)
        before=(project/'package.lock').read_bytes()
        source(project,'tests/registry_test.tk','import official/reg::{answer}\nfn main() -> i32 { if answer() != 42 { return 1 } return 0 }\n')
        shutil.rmtree(project/'.toka/packages/reg-1.0.0')
        offline=dict(online,TOKA_OFFLINE='1')
        r=subprocess.run(['toka','test','tests/registry_test.tk'],cwd=project,env=offline,capture_output=True,text=True,timeout=120)
        if r.returncode or (project/'package.lock').read_bytes()!=before:raise RuntimeError(r.stdout+r.stderr)
        # Keep the registry node cached when adding the native dependency below.
        env=offline
        # Reuse the SDK's established native plan/build helpers for a locked C dependency.
        native=root/'native_dependency';manifest(native)
        (native/'package.tk').write_text('pub const PACKAGE=(name="native_dep",version="1.0.0",dependencies=(),native=(required=true,sources=("native/answer.c")))\n')
        (native/'native').mkdir()
        (native/'native/answer.c').write_text('int b0_native_answer(void) { return 42; }\n')
        source(native,'lib/official/native_dep.tk','extern fn b0_native_answer() -> i32\npub fn answer() -> i32 { return b0_native_answer() }\n')
        manifest(project,'dep="../dependency",reg="reg:1.0.0",native_dep="../native_dependency",')
        fetch=subprocess.run(['toka','fetch'],cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if fetch.returncode:raise RuntimeError(fetch.stdout+fetch.stderr)
        before=(project/'package.lock').read_bytes()
        source(project,'tests/native_test.tk','import official/native_dep::{answer}\nfn main() -> i32 { if answer() != 42 { return 1 } return 0 }\n')
        r=subprocess.run(['toka','test','tests/native_test.tk'],cwd=project,env=env,capture_output=True,text=True,timeout=120)
        if r.returncode or (project/'package.lock').read_bytes()!=before:
            output.mkdir(parents=True,exist_ok=True)
            shutil.copytree(project/'.toka/test-runs',output/'failed-test-runs')
            errors='\n'.join(p.read_text() for p in (project/'.toka/test-runs').glob('*/native.stderr'))
            raise RuntimeError(r.stdout+r.stderr+errors)
        output.mkdir(parents=True,exist_ok=True)
        shutil.copytree(project/'.toka/test-runs',output/'test-runs')
        (output/'installed-result.json').write_text(json.dumps({'result':'pass','preview':True,'checks':['SDK PATH self-location','helper packaged','discovery excludes helpers','explicit subdirectory entry','locked local dependency','locked native C dependency','offline pinned registry fixture','run nonzero','allow empty'],'stable_v1':False},indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    if a.sdk:
        installed_sdk_test(a.sdk.resolve(),a.output.resolve())
    else:
        unittest.main(argv=[sys.argv[0]],verbosity=2)
