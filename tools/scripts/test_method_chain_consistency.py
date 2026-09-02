#!/usr/bin/env python3
"""Keep method-chain semantic checking and CodeGen in agreement."""

import argparse
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/method_chain_consistency"


def run(command):
    return subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def require(condition, message, result=None):
    if condition:
        return
    detail = ""
    if result is not None:
        detail = "\nstdout:\n%s\nstderr:\n%s" % (result.stdout, result.stderr)
    raise RuntimeError(message + detail)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.is_file(), "missing compiler: " + str(tokac))

    valid = FIXTURES / "valid_nested_unwrap.tk"
    valid_check = run([str(tokac), "--check-only", str(valid)])
    require(valid_check.returncode == 0,
            "valid nested method chain failed semantic checking", valid_check)
    with tempfile.TemporaryDirectory(prefix="toka-valid-method-chain-") as temp:
        executable = pathlib.Path(temp) / "valid-chain"
        valid_compile = run([str(tokac), str(valid), "-o", str(executable)])
        require(valid_compile.returncode == 0,
                "valid nested method chain failed CodeGen/link", valid_compile)
        valid_run = run([str(executable)])
        require(valid_run.returncode == 0,
                "valid nested method chain failed at runtime", valid_run)

    invalid = FIXTURES / "invalid_nonnullable_unwrap.tk"
    with tempfile.TemporaryDirectory(prefix="toka-invalid-method-chain-") as temp:
        commands = (
            ("check-only", [str(tokac), "--check-only", str(invalid)]),
            ("normal compile",
             [str(tokac), str(invalid), "-o", str(pathlib.Path(temp) / "invalid-chain")]),
        )
        for command_name, command in commands:
            rejected = run(command)
            output = rejected.stdout + rejected.stderr
            require(rejected.returncode != 0 and "E0417" in output,
                    command_name + " did not reject unwrap on a non-nullable receiver",
                    rejected)
            require("E0755" not in output and "Internal Error" not in output,
                    command_name + " leaked an invalid method chain into CodeGen",
                    rejected)

    print("Method-chain semantic/CodeGen consistency tests PASSED")


if __name__ == "__main__":
    main()
