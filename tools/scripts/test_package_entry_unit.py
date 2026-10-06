import json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'lib/toolchain'))
import toka_package as packages

class EntryControls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name).resolve();self.lib=self.root/'dep';(self.lib/'lib/dep').mkdir(parents=True)
        (self.lib/'package.tk').write_text('pub const PACKAGE=(name="dep",version="1.0.0",dependencies=())')
        (self.lib/'lib/dep/mod.tk').write_text('pub fn answer()->i32 { return 42 }')
        (self.root/'package.tk').write_text('pub const PACKAGE=(name="app",version="1.0.0",dependencies=(dep="./dep",))')
        packages.Resolver(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',offline=True,refresh=False).run()
        (self.root/'src').mkdir();self.source=self.root/'src/main.tk';self.source.write_text('import dep::{answer}')
    def test_exact_mapping(self):
        self.assertEqual(packages.prepare_entries(self.root,[self.source]),['dep='+str(self.lib/'lib/dep/mod.tk')])
    def test_comments_and_strings(self):
        self.source.write_text('// import dep/mod::{answer}\n/* import dep/mod::{answer} */\nauto x="import dep/mod::{answer}"\nimport dep::{answer}')
        self.assertEqual(len(packages.prepare_entries(self.root,[self.source])),1)
    def test_existing_official(self):
        (self.lib/'lib/dep/mod.tk').unlink();(self.lib/'lib/official').mkdir();(self.lib/'lib/official/dep.tk').write_text('pub fn answer()->i32 { return 42 }')
        packages.Resolver(self.root/'package.tk',self.root/'package.lock',self.root/'.toka',offline=True,refresh=False).run();self.source.write_text('import official/dep::{answer}')
        self.assertEqual(packages.prepare_entries(self.root,[self.source]),['official/dep='+str(self.lib/'lib/official/dep.tk')])
    def test_submodule_not_preempted(self):
        self.source.write_text('import dep/sub::{answer}');self.assertEqual(len(packages.prepare_entries(self.root,[self.source])),1)
    def test_permission_not_entry_error(self):
        with patch.object(Path,'read_text',side_effect=PermissionError('controlled read denied')):
            with self.assertRaises(PermissionError):packages.validate_package_imports(self.root,self.root/'package.lock',self.root/'.toka',[self.source])
    def test_integrity_not_entry_error(self):
        (self.lib/'lib/dep/mod.tk').write_text('changed')
        with self.assertRaises(packages.PackageConfigurationError) as raised:packages.prepare_entries(self.root,[self.source])
        self.assertEqual(raised.exception.code,'test.lock_mismatch')
    def test_empty_project_no_lock(self):
        (self.root/'package.tk').write_text('pub const PACKAGE=(name="app",version="1.0.0",dependencies=())');(self.root/'package.lock').unlink()
        self.source.write_text('fn main()->i32 { return 0 }');self.assertEqual(packages.prepare_entries(self.root,[self.source]),[]);self.assertFalse((self.root/'package.lock').exists())
    def test_explicit_selection(self):
        self.source.write_text('import dep/mod::{answer}')
        selected=self.root/'chosen.tk';selected.write_text('import dep::{answer}')
        self.assertEqual(len(packages.prepare_entries(self.root,[selected])),1)

    def test_relative_reachable_error(self):
        bridge=self.source.parent/'bridge.tk';bridge.write_text('import dep/mod::{answer}')
        for request in ('./bridge','../src/bridge','./../src/bridge','src/bridge'):
            self.source.write_text('import '+request+'::{answer}')
            with self.assertRaises(packages.PackageConfigurationError) as caught:
                packages.prepare_entries(self.root,[self.source])
            self.assertEqual(caught.exception.code,'package.import_invalid')
            self.assertEqual(caught.exception.details['package_entry']['source']['file'],str(bridge))
    def test_relative_cycle_is_visited_once(self):
        bridge=self.source.parent/'bridge.tk';cycle=self.source.parent/'cycle.tk'
        self.source.write_text('import ./bridge');bridge.write_text('import ./cycle');cycle.write_text('import ../src/bridge')
        original=packages.import_requests;opened=[]
        def observed(paths):
            paths=list(paths);opened.extend(p.resolve() for p in paths);return original(paths)
        with patch.object(packages,'import_requests',side_effect=observed):
            packages.prepare_entries(self.root,[self.source])
        self.assertEqual(sorted(opened),sorted([self.source,bridge,cycle]))

if __name__=='__main__':unittest.main()
