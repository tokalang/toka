#!/usr/bin/env python3
"""HashMap clone contract and proven payload lifecycle; not JSON qualification."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/semantics/binding_b5_hashmap"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def compile(source, *flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    with tempfile.TemporaryDirectory(prefix="toka-b5-clone-") as directory:
        work = Path(directory)
        for name in ("lifecycle.tk", "clone.tk", "strings.tk", "owned_handles.tk", "replacement.tk"):
            source = CASES / name
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0, normal.stderr + shadow.stderr
            assert normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            json.loads(shadow.stdout)
            binary = work / source.stem
            built = compile(source, "-o", binary)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, (name, result.returncode, result.stderr)
            print("PASS runtime/parity: " + name, flush=True)
        for name in ("clone_rejected.tk", "key_clone_rejected.tk"):
            source = CASES / name
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            assert re.findall(r"error\[(E\d+)\]", normal.stderr) == ["E0417"], normal.stderr
            assert "has no member named 'clone'" in normal.stderr, normal.stderr
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (source.stem + suffix)
                failed = compile(source, flag, "-o", output)
                assert failed.returncode == 1 and not output.exists(), failed.stderr
            # Removing only the forbidden clone must leave a working container,
            # including insertion and drop with exactly the same element type.
            control = work / name
            control.write_text(source.read_text().replace("    auto copied = source.clone()\n", ""))
            binary = work / (source.stem + "-ordinary")
            built = compile(control, "-o", binary)
            assert built.returncode == 0, built.stderr
            assert subprocess.run([str(binary)], timeout=15).returncode == 0
            print("PASS NonDup clone-only rejection/control: " + name, flush=True)
    print("5 runtime/parity cases; 2 clone rejections with object/IR absence and ordinary-use controls.")
    print("Direct managed element morphology and recursive JSON remain separate failed positives.")


if __name__ == "__main__":
    main()
