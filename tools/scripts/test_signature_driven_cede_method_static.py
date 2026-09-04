#!/usr/bin/env python3
"""Qualify default method/static signature-driven cede routes."""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/signature_driven_cede_direct"
FLAG = "--experimental-signature-driven-cede"
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"


def run(command):
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    runtime = FIXTURES / "method_static_runtime.tk"
    default = run([str(tokac), "--check-only", str(runtime)])
    require(default.returncode == 0 and "E04509" not in default.stderr and
            "E04570" not in default.stderr,
            "default method/static routes did not type-check")

    enabled = run([str(tokac), FLAG, "--check-only", str(runtime)])
    require(enabled.returncode == 0 and "E04509" not in enabled.stderr and
            "E04570" not in enabled.stderr,
            "deprecated compatibility flag changed default behavior")

    with tempfile.TemporaryDirectory(prefix="toka-cede-method-static-") as temp:
        executable = pathlib.Path(temp) / "runtime"
        compiled = run([str(tokac), str(runtime), "-o", str(executable)])
        require(compiled.returncode == 0,
                "method/static slice failed CodeGen/link")
        executed = run([str(executable)])
        require(executed.returncode == 0,
                f"method/static runtime failed: {executed.returncode}")

        explicit_executable = pathlib.Path(temp) / "explicit-unique"
        explicit_source = FIXTURES / "method_unique_explicit_runtime.tk"
        explicit_compiled = run([str(tokac), str(explicit_source), "-o",
                                 str(explicit_executable)])
        require(explicit_compiled.returncode == 0,
                "explicit unique method route failed CodeGen/link")
        explicit_executed = run([str(explicit_executable)])
        require(explicit_executed.returncode == 0,
                "explicit unique method route failed at runtime")

    for fixture in ("method_use_after_implicit.tk",
                    "static_use_after_implicit.tk"):
        moved = run([str(tokac), "--check-only",
                     str(FIXTURES / fixture)])
        require(moved.returncode != 0 and "E0438" in moved.stderr and
                "E04509" not in moved.stderr and "E04570" not in moved.stderr,
                f"implicit route did not invalidate its source: {fixture}")

    multi = run([str(tokac), "--check-only",
                 str(FIXTURES / "method_multi_arg_out_of_slice.tk")])
    require(multi.returncode == 0 and "E04509" not in multi.stderr,
            "qualified multi-argument method remained out of slice")

    print("Signature-driven method/static cede tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven method/static cede tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
