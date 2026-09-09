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

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = str(Path(args.build_dir).resolve() / "bin/tokac")
    with tempfile.TemporaryDirectory(prefix="toka-negative-termination-") as temp:
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

        real_source = ROOT / "tests/fail/member_access_sema_prefix_fail.tk"
        for mode in ([], ["--stage1-legacy-ordinary-cede"]):
            for flags, suffix in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                target = Path(temp) / ("invalid-member" + suffix)
                result = subprocess.run([compiler, *mode, *flags, str(real_source), "-o", str(target)],
                                        cwd=ROOT, env=dict(os.environ, TOKA_LIB=str(ROOT / "lib")),
                                        capture_output=True, text=True, timeout=30)
                assert result.returncode == 1, (result.returncode, result.stderr)
                assert "error[E0406]" in result.stderr and "error[E0461]" in result.stderr
                assert not target.exists()
    print("negative termination: crash cannot pass or bless; real invalid initializer rejects without artifacts")


if __name__ == "__main__":
    main()
