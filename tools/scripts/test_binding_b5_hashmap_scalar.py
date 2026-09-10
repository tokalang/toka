#!/usr/bin/env python3
"""Partial HashMap migration gate; resource/JSON positives remain outstanding."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    source = ROOT / "tests/semantics/binding_b5_hashmap/scalar.tk"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def compile(*flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    normal = compile("--check-only")
    shadow = compile("--check-only", "--non-call-transfer-shadow=json")
    assert normal.returncode == shadow.returncode == 0, normal.stderr + shadow.stderr
    assert normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
    json.loads(shadow.stdout)
    with tempfile.TemporaryDirectory(prefix="toka-b5-hashmap-") as directory:
        binary = Path(directory) / "scalar"
        built = compile("-o", binary)
        assert built.returncode == 0, built.stderr
        executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        assert executed.returncode == 0, (executed.returncode, executed.stderr)
        ir = Path(directory) / "scalar.ll"
        emitted = compile("--emit-llvm", "-o", ir)
        assert emitted.returncode == 0, emitted.stderr
        bodies = re.findall(r"^define .*?^}", ir.read_text(), re.M | re.S)
        helpers = [body for body in bodies if "ptr %marker)" in body and "raw.take.value" in body]
        assert len(helpers) == 1, len(helpers)
        body = helpers[0]
        retirement = body.index("store i8 2,")
        take = body.index("%raw.take.value = load i32")
        assert body.index("icmp eq ptr") < retirement < take, body
        assert not re.search(r"\b(call|invoke|callbr)\b|llvm\.coro", body[retirement:take]), body
        assert "getelementptr i32" in body[retirement:take], body
        assert "ret i32 %raw.take.value" in body, body
    print("PASS scalar: 2000 inserts/multiple resizes, remove hit/miss/repeat,")
    print("tombstone probe/reuse, clear/repeated clear/reuse, normal-shadow parity;")
    print("IR verifies retirement -> typed take with no call/suspension.")
    print("NOT QUALIFIED: resource/unique/shared exact-once or JSON integration.")


if __name__ == "__main__":
    main()
