#!/usr/bin/env python3
"""Exercise the five owner-buffer migrations without granting raw permissions."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/semantics/unsafe_raw_construction/string_buffer_migration.tk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def compile(*flags):
        return subprocess.run([str(compiler), *flags, str(SOURCE)], cwd=ROOT,
                              env=env, capture_output=True, text=True, timeout=45)

    normal = compile("--check-only")
    shadow = compile("--check-only", "--non-call-transfer-shadow=json")
    if normal.returncode != 0 or shadow.returncode != 0 or normal.stderr != shadow.stderr:
        raise RuntimeError(normal.stderr + shadow.stderr)
    with tempfile.TemporaryDirectory(prefix="toka-string-buffer-") as directory:
        output = Path(directory) / "string_buffer"
        built = compile("-o", str(output))
        if built.returncode != 0 or not output.is_file():
            raise RuntimeError(built.stderr)
        executed = subprocess.run([str(output)], cwd=directory, env=env,
                                  capture_output=True, text=True, timeout=15)
        if executed.returncode != 0:
            raise RuntimeError(f"runtime {executed.returncode}: {executed.stderr}")
    print("string owner buffers: normal/shadow parity and runtime passed; nullable/allocated empty, ASCII boundaries, non-ASCII bytes, input independence, in-place address/capacity, repeated cleanup scopes; no skips")


if __name__ == "__main__":
    main()
