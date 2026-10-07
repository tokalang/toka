#!/usr/bin/env python3
"""Local configure/packager boundary controls; no Intel build or SDK qualification."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from test_release_workflow import shell_run_blocks

ROOT = Path(__file__).resolve().parents[2]


class Profile(unittest.TestCase):
    def command(self, argv, cwd, env=None, expected=0):
        child = subprocess.run(argv, cwd=cwd, env=env, capture_output=True)
        receipt = dict(argv=argv, cwd=str(cwd), exit_code=child.returncode,
                       stdout=child.stdout.decode(errors='replace'),
                       stderr=child.stderr.decode(errors='replace'))
        evidence = os.environ.get('TOKA_PROFILE_CONTROL_LOGS')
        if evidence:
            directory = Path(evidence); directory.mkdir(parents=True, exist_ok=True)
            number = len(list(directory.glob('*.json')))
            prefix = directory / ('%03d' % number)
            prefix.with_suffix('.json').write_text(json.dumps(receipt, indent=2))
            prefix.with_suffix('.stdout').write_bytes(child.stdout)
            prefix.with_suffix('.stderr').write_bytes(child.stderr)
        self.assertEqual(child.returncode, expected, json.dumps(receipt))
        return child

    def test_workflow_configure_and_packaging_boundary(self):
        blocks = shell_run_blocks((ROOT / '.github/workflows/optional_macos_x64.yml').read_text())
        build = next(block for block in blocks if 'cmake -S' in block)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); shim = root / 'shim'; shim.mkdir()
            # Execute the actual workflow block up to the build boundary. The shim
            # records configure argv; it deliberately stops before building anything.
            cmake = shim / 'cmake'
            cmake.write_text('#!/usr/bin/env python3\nimport json,os,sys\n'
                             'if "--build" in sys.argv:sys.exit(77)\n'
                             'open(os.environ["CAPTURE_ARGS"],"w").write(json.dumps(sys.argv[1:]))\n')
            cmake.chmod(0o755)
            caches = {}
            for tag, expected in [('v0.12.0', 'ON'), ('v0.13.0', 'OFF')]:
                capture = root / (tag + '.argv.json')
                env = dict(os.environ, TAG_NAME=tag, CAPTURE_ARGS=str(capture),
                           PATH=str(shim) + os.pathsep + os.environ['PATH'])
                self.command(['/bin/bash', '-c', textwrap.dedent(build)], ROOT, env, 77)
                args = json.loads(capture.read_text())
                flags = [a for a in args if a.startswith('-DBUILD_TESTING=')]
                self.assertEqual(flags, ['-DBUILD_TESTING=OFF'] if expected == 'OFF' else [])
                directory = root / ('configure-' + tag)
                args[args.index('-B') + 1] = str(directory)
                # Real native host configuration only, using the extracted flags.
                self.command(['cmake', *args], ROOT)
                cache = (directory / 'CMakeCache.txt').read_text()
                self.assertIn('BUILD_TESTING:BOOL=' + expected + '\n', cache)
                caches[expected] = directory / 'CMakeCache.txt'

            # Exercise the full real packager with independent, synthetic files.
            # These stubs are controls, never release SDK or installation evidence.
            fixture = root / 'packager'; fixture.mkdir()
            for relative in ('lib/sys', 'lib/toolchain', 'tools/scripts', 'docs', 'build/bin'):
                (fixture / relative).mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / 'tools/scripts/package_release.sh', fixture / 'tools/scripts/package_release.sh')
            for helper in ('toka_build.py', 'semantic_diff_preview.py'):
                (fixture / 'tools/scripts' / helper).write_text('# synthetic control\n')
            for doc in ('ai_completion_card.md', 'package_entry_contract.md', 'package_entry_example.md',
                        'diagnostic_d1_d2_migration.md', 'toka_test_lock_codes.md'):
                (fixture / 'docs' / doc).write_text('Synthetic packaging boundary control\n')
            for item in ('toka_rt.o', 'llvm_shim.o'):
                (fixture / 'lib/sys' / item).write_bytes(b'synthetic object')
            for item in ('README.md', 'LICENSE'):
                (fixture / item).write_text('Synthetic control\n')
            for tool in ('tokac', 'toka', 'tokafmt', 'tokalsp'):
                path = fixture / 'build/bin' / tool
                path.write_text('#!/bin/sh\necho "synthetic tool 0.13.0"\n'); path.chmod(0o755)
            (fixture / '.gitignore').write_text('build/\n')
            self.command(['git', 'init', '-q'], fixture)
            self.command(['git', 'add', '.'], fixture)
            self.command(['git', '-c', 'user.name=Control', '-c', 'user.email=control@invalid',
                          '-c', 'commit.gpgsign=false', 'commit', '-qm', 'Synthetic fixture'], fixture)
            env = dict(os.environ, OS='macos', ARCH='x64')
            shutil.copyfile(caches['ON'], fixture / 'build/CMakeCache.txt')
            child = self.command(['bash', 'tools/scripts/package_release.sh', 'v0.13.0'], fixture, env, 1)
            self.assertIn(b'requires a standard BUILD_TESTING=OFF build', child.stderr)
            self.assertFalse((fixture / 'build/toka-v0.13.0-macos-x64.tar.gz').exists())
            shutil.copyfile(caches['OFF'], fixture / 'build/CMakeCache.txt')
            self.command(['bash', 'tools/scripts/package_release.sh', 'v0.13.0'], fixture, env)
            identity = json.loads((fixture / 'build/toka-v0.13.0-macos-x64/sdk.json').read_text())
            self.assertIs(identity['build_testing'], False)
            self.assertTrue((fixture / 'build/toka-v0.13.0-macos-x64.tar.gz').is_file())
            shutil.copyfile(caches['ON'], fixture / 'build/CMakeCache.txt')
            self.command(['bash', 'tools/scripts/package_release.sh', 'v0.12.0'], fixture, env)
            self.assertFalse((fixture / 'build/toka-v0.12.0-macos-x64/sdk.json').exists())


if __name__ == '__main__':
    unittest.main()
