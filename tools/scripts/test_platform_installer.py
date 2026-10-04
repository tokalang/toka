#!/usr/bin/env python3
"""V07/V08 selection/checksum controls with explicit synthetic platform packages."""
import hashlib,json,os,subprocess,tarfile,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]


class Install(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def install(self,system,machine,target,present=True,tampered=False):
        root=self.root/target;root.mkdir();fake=root/'fake';fake.mkdir();home=root/'home';home.mkdir();version='v0.12.0';name='toka-%s-%s.tar.gz'%(version,target);sdk=root/name[:-7];(sdk/'bin').mkdir(parents=True);(sdk/'lib').mkdir()
        for tool in ('tokac','toka','tokafmt','tokalsp'):
            path=sdk/'bin'/tool;path.write_text('#!/bin/sh\nprintf "%s\\n" "selection fixture '+target+'"\n');path.chmod(0o755)
        archive=root/name
        with tarfile.open(archive,'w:gz') as package:package.add(sdk,arcname=sdk.name)
        sums=root/'SHA256SUMS';sums.write_text(('0'*64 if tampered else hashlib.sha256(archive.read_bytes()).hexdigest())+'  '+name+'\n' if present else '')
        (fake/'uname').write_text('#!/bin/sh\ncase "$1" in -s) printf "%s\\n" "$TEST_SYSTEM" ;; -m) printf "%s\\n" "$TEST_MACHINE" ;; esac\n');(fake/'uname').chmod(0o755)
        (fake/'curl').write_text('#!/bin/sh\nfor arg in "$@"; do case "$arg" in https:*) printf "%s\\n" "$arg" >> "$TEST_REQUESTS" ;; esac; done\nwant=0\nfor arg in "$@"; do if [ "$want" = 1 ]; then output=$arg; want=0; fi; if [ "$arg" = -o ]; then want=1; fi; done\ncase "$output" in */SHA256SUMS) cp "$TEST_SUMS" "$output" ;; *) cp "$TEST_ARCHIVE" "$output" ;; esac\n');(fake/'curl').chmod(0o755)
        env=dict(os.environ,HOME=str(home),SHELL='/bin/sh',PATH=str(fake)+os.pathsep+os.environ['PATH'],TEST_SYSTEM=system,TEST_MACHINE=machine,TEST_REQUESTS=str(root/'requests'),TEST_SUMS=str(sums),TEST_ARCHIVE=str(archive))
        r=subprocess.run(['sh',str(ROOT/'tools/install.sh'),version],env=env,capture_output=True);(root/'stdout').write_bytes(r.stdout);(root/'stderr').write_bytes(r.stderr);requests=(root/'requests').read_text()
        if present and not tampered:
            self.assertEqual(r.returncode,0,r.stderr);installed=home/'.toka/bin/toka';self.assertEqual(subprocess.check_output([installed]).strip(),('selection fixture '+target).encode());self.assertIn('/'+name,requests)
        else:self.assertNotEqual(r.returncode,0);self.assertFalse((home/'.toka').exists())
        return r,requests
    def test_V07_all_three_core_packages_and_checksums(self):
        for system,machine,target in [('Linux','x86_64','linux-x64'),('Linux','aarch64','linux-arm64'),('Darwin','arm64','macos-arm64')]:
            with self.subTest(target=target):self.install(system,machine,target)
    def test_V08_intel_absence_does_not_request_other_binary_or_version(self):
        r,requests=self.install('Darwin','x86_64','macos-x64',False);self.assertIn(b'v0.12.0 has no macos-x64 binary',r.stdout);self.assertIn(b'Build the exact tag v0.12.0',r.stdout);self.assertNotIn('.tar.gz',requests);self.assertNotIn('latest',requests)
    def test_V07_tampered_core_archive_not_activated(self):self.install('Linux','x86_64','linux-x64',tampered=True)


if __name__=='__main__':unittest.main()
