#!/usr/bin/env python3

"""Qualify the first Stage-1 return/source lowering slice."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_return_source"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    with tempfile.TemporaryDirectory(prefix="toka-stage1-return-source-") as temp:
        for source in (
                "dyn_fn_constructed_return.tk",
                "dyn_fn_constructed_return_exact_once.tk",
                "dyn_fn_local_return_control.tk"):
            artifact = Path(temp) / source.removesuffix(".tk")
            built = subprocess.run(
                [str(tokac), str(FIXTURES / source), "-o", str(artifact)],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=30)
            require(built.returncode == 0 and artifact.is_file(), built.stderr)
            ran = subprocess.run(
                [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, timeout=10)
            require(ran.returncode == 0,
                    source + " failed its return carrier/lifetime contract: " +
                    ran.stderr)

    print("stage1 return source: pass")


if __name__ == "__main__":
    main()
