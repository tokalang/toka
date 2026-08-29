#!/usr/bin/env python3
"""Qualify default atomic multi-argument signature-driven cede admission."""

import argparse
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/signature_driven_cede_direct"
FLAG = "--experimental-signature-driven-cede"


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

    runtime = FIXTURES / "multi_runtime.tk"
    default = run([str(tokac), "--check-only", str(runtime)])
    require(default.returncode == 0 and "E04570" not in default.stderr,
            "default atomic multi-argument call did not type-check")
    enabled = run([str(tokac), FLAG, "--check-only", str(runtime)])
    require(enabled.returncode == 0 and "E04570" not in enabled.stderr,
            "deprecated compatibility flag changed default behavior")

    with tempfile.TemporaryDirectory(prefix="toka-cede-multi-") as temp:
        executable = pathlib.Path(temp) / "runtime"
        compiled = run([str(tokac), str(runtime), "-o", str(executable)])
        require(compiled.returncode == 0,
                "atomic multi-argument call failed CodeGen/link")
        executed = run([str(executable)])
        require(executed.returncode == 0,
                "atomic multi-argument call failed at runtime")

    moved = run([str(tokac), "--check-only",
                 str(FIXTURES / "multi_use_after_implicit.tk")])
    require(moved.returncode != 0 and "E0438" in moved.stderr and
            "E04570" not in moved.stderr,
            "successful multi-argument transfer did not invalidate sources")

    failures = (
        ("multi_type_failure_atomic.tk", "E04571"),
        ("multi_alias_failure_atomic.tk", "E0475"),
        ("multi_borrow_failure_atomic.tk", "E0475"),
        ("multi_ineligible_failure_atomic.tk", "E04570"),
    )
    for fixture, diagnostic in failures:
        failed = run([str(tokac), "--check-only",
                      str(FIXTURES / fixture)])
        require(failed.returncode != 0 and diagnostic in failed.stderr,
                f"missing expected failure for {fixture}")
        require("E0438" not in failed.stderr,
                f"rejected call partially invalidated a source: {fixture}")

    print("Signature-driven multi-argument atomic tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven multi-argument atomic tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
