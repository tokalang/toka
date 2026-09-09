#!/usr/bin/env python3
"""Typed enum payload cleanup; no ABI or raw_take changes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/enum_payload_cleanup"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)], cwd=ROOT,
                              env=env, text=True, capture_output=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-enum-cleanup-gate-") as directory:
        for source in ("lifecycle.tk", "nested_multi_custom.tk", "raw_reference.tk", "uninitialized.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": normal/shadow failed\n" + normal.stderr + shadow.stderr)
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            require(built.returncode == 0 and output.is_file(), source + ": " + built.stderr)
            executed = subprocess.run([str(output)], cwd=directory, text=True,
                                      capture_output=True, timeout=15)
            require(executed.returncode == 0,
                    source + ": lifecycle failed, rc=" + str(executed.returncode) + executed.stderr)

        # This declaration is legal source, but the current declaration path
        # loses its root hats before CodeGen. Do not bless a guessed layout.
        source = "incomplete_concrete_morphology.tk"
        checked = run(source, "--check-only")
        require(checked.returncode == 0, checked.stderr)
        for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = Path(directory) / ("incomplete" + suffix)
            rejected = run(source, mode, "-o", str(output))
            require(rejected.returncode == 1 and "E0701" in rejected.stderr and
                    "enum cleanup requires complete resolved payload types" in rejected.stderr and
                    not output.exists(), "incomplete payload metadata emitted an artifact or crashed")
    print("enum cleanup: 4 runtime/parity cases, 2 incomplete-metadata no-artifact checks; zero skips")


if __name__ == "__main__":
    main()
