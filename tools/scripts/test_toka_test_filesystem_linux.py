#!/usr/bin/env python3
"""Exercise the Linux query branch against GNU stat names and rejected output."""
import json,subprocess,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import toka_test_filesystem as filesystem

class LinuxFilesystem(unittest.TestCase):
    def test_gnu_composite_type_in_query(self):
        with tempfile.TemporaryDirectory(prefix='linux-fs-query-') as temp:
            root=Path(temp);fixture=root/'fixture';fixture.mkdir()
            result=subprocess.CompletedProcess([],0,b'ext2/ext3\n',b'')
            with patch.object(filesystem.sys,'platform','linux'),patch.object(filesystem.subprocess,'run',return_value=result) as call:
                evidence=filesystem.query(fixture,root/'evidence')
            self.assertEqual(call.call_args.args[0],['stat','-f','-c','%T',str(fixture.resolve())])
            self.assertEqual(evidence['data']['filesystem_type'],'ext2/ext3')
            self.assertEqual(evidence['data']['device'],fixture.stat().st_dev)
            self.assertTrue(evidence['validated'])
    def test_linux_names(self):
        for name in ('ext2/ext3','ext4','tmpfs','overlayfs','btrfs'):
            with self.subTest(name=name):self.assertEqual(filesystem.linux_filesystem_name((name+'\n').encode()),name)
    def test_empty_directory_marker_and_malformed_names(self):
        for output in (b'',b'\n',b'/\n',b'ext2/',b'/ext3',b'ext2//ext3',b'ext2 ext3',b'ext2\next3'):
            with self.subTest(output=output):
                with self.assertRaises(AssertionError):filesystem.linux_filesystem_name(output)
    def test_failed_query_cannot_return_valid_metadata(self):
        with tempfile.TemporaryDirectory(prefix='linux-fs-failure-') as temp:
            root=Path(temp)
            with patch.object(filesystem.sys,'platform','linux'),patch.object(filesystem.subprocess,'run',return_value=subprocess.CompletedProcess([],1,b'ext2/ext3\n',b'error')):
                with self.assertRaises(AssertionError):filesystem.query(root,root/'evidence')
if __name__=='__main__':unittest.main()
