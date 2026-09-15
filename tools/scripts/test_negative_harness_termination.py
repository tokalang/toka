#!/usr/bin/env python3
"""An expected diagnostic must never turn a compiler crash into a passing test."""
import argparse
import contextlib
import io
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from unittest import mock
from compare_thread_sync_baseline import pass_abnormal, suite

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = str(Path(args.build_dir).resolve() / "bin/tokac")
    with tempfile.TemporaryDirectory(prefix="toka-negative-termination-") as temp:
        signal = 'test_pass.sh: line 179: 90368 Segmentation fault: 11 "$TOKAC" "$test_path"'
        crash = signal + '\n[FAIL] g09_context.tk\n error: Compilation failed\n'
        events = pass_abnormal(crash)
        assert len(events) == 1 and events[0]['case'] == 'g09_context.tk' and events[0]['phase'] == 'compiler'
        assert not pass_abnormal('[FAIL] ordinary.tk\n error[E0408]: mismatch\n')
        ambiguous = pass_abnormal(signal + '\n[PASS] another.tk\n[FAIL] rejected.tk\n')
        assert len(ambiguous) == 1 and ambiguous[0]['case'] is None
        explicit = pass_abnormal(crash + ' g09_context.tk: Compiler abnormal exit (139)\n')
        assert len(explicit) == 1 and explicit[0]['attribution'] == 'case-report'
        runtime = '[FAIL] run.tk\n error: Runtime crash (139) without panic info.\n'
        assert len(pass_abnormal(runtime + runtime)) == 1
        assert not pass_abnormal('[FAIL] failed_assertion.tk\n Runtime crash (1)\n')
        (Path(temp) / 'pass.log').write_text(crash + 'Summary:\n Passed: 1\n Failed: 1\n')
        parsed = suite(Path(temp), 'pass')
        assert parsed['failed'] == 1 and parsed['abnormal'][0]['phase'] == 'compiler'
        source = Path(temp) / "case.tk"
        source.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
        golden = source.with_suffix(".stderr")
        expected = "error[E0406]: expected failure\n"
        golden.write_text(expected, encoding="utf-8")
        for rc, bless, status, message in (
                (-11, "0", 1, "Compiler Abnormal Exit"),
                (-11, "1", 1, "Compiler Abnormal Exit"),
                (0, "0", 1, "Unexpectedly Passed"),
                (1, "0", 0, "Passed:")):
            output = io.StringIO()
            result = subprocess.CompletedProcess([], rc, "", expected)
            with mock.patch.dict(os.environ, {"TOKAC": compiler, "BLESS": bless}), \
                 mock.patch.object(sys, "argv", ["test_verify_fail.py", str(source)]), \
                 mock.patch("subprocess.run", return_value=result), \
                 contextlib.redirect_stdout(output):
                try:
                    runpy.run_path(str(ROOT / "tools/scripts/test_verify_fail.py"), run_name="__main__")
                except SystemExit as exit:
                    assert exit.code == status, output.getvalue()
            assert message in output.getvalue(), output.getvalue()
            assert golden.read_text(encoding="utf-8") == expected

        # Simulate a signal-style status without actually crashing a process.
        fake_compiler = Path(temp) / 'compiler-exit-139.sh'
        fake_compiler.write_text('#!/bin/sh\nexit 139\n')
        fake_compiler.chmod(0o755)
        worker = subprocess.run(['bash', str(ROOT / 'tools/scripts/test_pass.sh'),
                                 '--worker', str(source)], cwd=ROOT,
                                env=dict(os.environ, TOKAC=str(fake_compiler), USE_TOKA='0'),
                                capture_output=True, text=True, timeout=30)
        assert worker.returncode == 1 and 'Compiler abnormal exit (139)' in worker.stdout
        worker_events = pass_abnormal(worker.stdout)
        assert len(worker_events) == 1 and worker_events[0]['case'] == 'case.tk'
        assert worker_events[0]['attribution'] == 'case-report'

        # The former fixture now intentionally fails at removed quote syntax.
        # Keep this control in Sema: an invalid initializer must never reach
        # CodeGen or leave an artifact, independent of the lexer migration.
        real_source = Path(temp) / "invalid-member.tk"
        real_source.write_text("shape Point(x:i32)\nfn main() -> i32 {\n"
                               "auto point = Point(x=1)\nauto invalid = point.missing\n"
                               "return 0\n}\n", encoding="utf-8")
        for mode in ([], ["--stage1-legacy-ordinary-cede"]):
            for flags, suffix in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                target = Path(temp) / ("invalid-member" + suffix)
                result = subprocess.run([compiler, *mode, *flags, str(real_source), "-o", str(target)],
                                        cwd=ROOT, env=dict(os.environ, TOKA_LIB=str(ROOT / "lib")),
                                        capture_output=True, text=True, timeout=30)
                assert result.returncode == 1, (result.returncode, result.stderr)
                assert "error[E0417]" in result.stderr and "E01268" not in result.stderr
                assert not target.exists()
    print("negative termination: crash cannot pass or bless; real invalid initializer rejects without artifacts")


if __name__ == "__main__":
    main()
