#!/usr/bin/env python3
"""A directory selected as module input must produce a diagnostic, not abort."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = root / args.build_dir / "bin" / ("tokac.exe" if os.name == "nt" else "tokac")
    with tempfile.TemporaryDirectory(prefix="toka-module-input-io-") as temp:
        work = Path(temp)
        directory = work / "source.tk"
        directory.mkdir()
        output = work / "source.o"
        result = subprocess.run([str(compiler), "-c", str(directory), "-o", str(output)],
                                cwd=root, capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and "E0901" in result.stderr, result.stdout + result.stderr
        assert not output.exists(), "directory input produced an object"
    print("Module directory input diagnostic and no-object control PASSED")


if __name__ == "__main__":
    main()
