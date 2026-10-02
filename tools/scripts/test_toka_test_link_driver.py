#!/usr/bin/env python3
"""Actual exec errors, link rejection, and the inherited supervision boundary."""
import json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'lib/toolchain'))
import toka_test as runner
class Controls(unittest.TestCase):
 def test_actual_exec_outcomes(self):
  with tempfile.TemporaryDirectory() as name:
   root=Path(name);search=root/'search';search.mkdir()
   for variant in ('missing','permission','interpreter','reject','pass'):
    cc=search/'cc'
    if cc.exists():cc.unlink()
    if variant!='missing':
     cc.write_text('#!/absent-r08-interpreter\n' if variant in ('permission','interpreter') else '#!/bin/sh\nexit '+('1' if variant=='reject' else '0')+'\n')
     cc.chmod(0o600 if variant=='permission' else 0o700)
    status=root/(variant+'.json');code=runner.link_driver_worker(status,str(search),['input.o','-o','out'])
    r=json.loads(status.read_text())
    if variant in ('missing','permission','interpreter'):
     self.assertEqual(code,127);self.assertEqual(r['state'],'launch_failed');self.assertEqual(r['os_error'],13 if variant=='permission' else 2);self.assertIsNone(r['pid'])
    else:
     self.assertEqual(code,1 if variant=='reject' else 0);self.assertEqual(r['state'],'completed');self.assertEqual(r['exit_code'],code);self.assertIsNone(r['os_error'])
 def test_bridge_preserves_driver_path_arguments_and_group(self):
  if not sys.platform.startswith('linux'):self.skipTest('Linux compiler driver profile')
  with tempfile.TemporaryDirectory(prefix='r08 space ') as name:
   root=Path(name);search=root/'search';search.mkdir();witness=root/'witness.json'
   cc=search/'cc';cc.write_text('#!'+sys.executable+'\nimport json,os,sys\nfrom pathlib import Path\nPath('+repr(str(witness))+').write_text(json.dumps({"args":sys.argv[1:],"PATH":os.environ["PATH"],"pgid":os.getpgrp()}))\n');cc.chmod(0o700)
   env=dict(os.environ,PATH=str(search));status=runner.link_driver_bridge(root,env)
   s=runner.Supervisor();raw=s.run([str(root/'link-driver/cc'),'space arg','-o','out file'],root,root,'compile',env,5000)
   r=json.loads(witness.read_text());self.assertEqual(r['args'],['space arg','-o','out file']);self.assertEqual(r['PATH'],str(search));self.assertEqual(r['pgid'],raw['pid']);self.assertEqual(raw['exit_code'],0);self.assertEqual(raw['cleanup']['status'],'confirmed');self.assertEqual(runner.link_driver_result(status)['state'],'completed')
if __name__=='__main__':unittest.main()
