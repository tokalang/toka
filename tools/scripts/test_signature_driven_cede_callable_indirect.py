#!/usr/bin/env python3
"""Qualify default callable and indirect signature-driven cede routes."""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"
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

    runtime_fixtures = ("callable_runtime.tk", "indirect_fn_runtime.tk",
                        "indirect_dyn_runtime.tk")
    with tempfile.TemporaryDirectory(prefix="toka-cede-callable-") as temp:
        temp_path = pathlib.Path(temp)
        for fixture in runtime_fixtures:
            source = FIXTURES / fixture
            default = run([str(tokac), "--check-only", str(source)])
            require(default.returncode == 0 and "E04570" not in default.stderr,
                    f"default route did not type-check: {fixture}")

            enabled = run([str(tokac), FLAG, "--check-only", str(source)])
            require(enabled.returncode == 0 and "E04570" not in enabled.stderr,
                    f"compatibility flag changed route: {fixture}")

            executable = temp_path / fixture.removesuffix(".tk")
            compiled = run([str(tokac), str(source), "-o",
                            str(executable)])
            require(compiled.returncode == 0,
                    f"route failed CodeGen/link: {fixture}")
            executed = run([str(executable)])
            require(executed.returncode == 0,
                    f"route failed at runtime: {fixture}")

        explicit_source = FIXTURES / "callable_indirect_explicit_runtime.tk"
        explicit_executable = temp_path / "explicit"
        explicit_compiled = run([str(tokac), str(explicit_source), "-o",
                                 str(explicit_executable)])
        require(explicit_compiled.returncode == 0,
                "explicit callable/indirect forms failed CodeGen/link")
        explicit_executed = run([str(explicit_executable)])
        require(explicit_executed.returncode == 0,
                "explicit callable/indirect forms failed at runtime")

        copy_source = FIXTURES / "callable_indirect_copy_runtime.tk"
        copy_executable = temp_path / "copy"
        copy_compiled = run([str(tokac), str(copy_source), "-o",
                             str(copy_executable)])
        require(copy_compiled.returncode == 0,
                "Copy callable/indirect forms failed CodeGen/link")
        copy_executed = run([str(copy_executable)])
        require(copy_executed.returncode == 0,
                "bare Copy place was not preserved across a cede formal")

        unique_source = FIXTURES / "callable_unique_runtime.tk"
        unique_executable = temp_path / "unique"
        unique_compiled = run([str(tokac), str(unique_source), "-o",
                               str(unique_executable)])
        require(unique_compiled.returncode == 0,
                "unique callable form failed CodeGen/link")
        unique_executed = run([str(unique_executable)])
        require(unique_executed.returncode == 0,
                "unique callable form failed at runtime")

    for fixture in ("callable_use_after_implicit.tk",
                    "indirect_fn_use_after_implicit.tk",
                    "indirect_dyn_use_after_implicit.tk"):
        moved = run([str(tokac), "--check-only",
                     str(FIXTURES / fixture)])
        require(moved.returncode != 0 and "E0438" in moved.stderr and
                "E04570" not in moved.stderr,
                f"implicit route did not invalidate its source: {fixture}")

    with tempfile.TemporaryDirectory(prefix="toka-cede-indirect-multi-") as temp:
        for fixture in ("indirect_multi_arg_out_of_slice.tk",
                        "indirect_dyn_multi_runtime.tk"):
            source = FIXTURES / fixture
            checked = run([str(tokac), "--check-only", str(source)])
            require(checked.returncode == 0,
                    f"multi-argument indirect call remained out of slice: {fixture}")
            executable = pathlib.Path(temp) / fixture.removesuffix(".tk")
            compiled = run([str(tokac), str(source), "-o", str(executable)])
            require(compiled.returncode == 0,
                    f"multi-argument indirect call failed CodeGen: {fixture}")
            require(run([str(executable)]).returncode == 0,
                    f"multi-argument indirect runtime failed: {fixture}")

    for fixture in ("indirect_mixed_type_failure_atomic.tk",
                    "indirect_dyn_mixed_type_failure_atomic.tk"):
        rejected = run([str(tokac), "--check-only", str(FIXTURES / fixture)])
        require(rejected.returncode != 0 and "E0438" not in rejected.stderr,
                f"mixed indirect rejection partially moved source: {fixture}")

    print("Signature-driven callable/indirect cede tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven callable/indirect cede tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
