#!/usr/bin/env python3
"""Factory construction regression subset; NOT the original JSON recovery gate."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/json_leaf_factories"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    environment = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-json-factory-values-") as directory:
        work = Path(directory)

        def compile(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  cwd=ROOT, env=environment, text=True,
                                  capture_output=True, timeout=90)

        def qualify(source, expected, diagnostic="E0455"):
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == expected, (source, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, (source, normal.stderr, shadow.stderr)
            json.loads(shadow.stdout)
            output = work / source.stem
            built = compile(source, "-o", output)
            if expected:
                assert "error[" + diagnostic + "]" in built.stderr and built.returncode == expected, built.stderr
                assert not output.exists(), source
            else:
                assert built.returncode == 0, built.stderr
                ran = subprocess.run([str(output)], text=True, capture_output=True, timeout=15)
                assert ran.returncode == 0, (source, ran.returncode, ran.stdout, ran.stderr)
            print("PASS " + source.name, flush=True)

        qualify(FIXTURES / "generic_factory_values.tk", 0)
        cleanup = FIXTURES / "generic_factory_cleanup.tk"
        qualify(cleanup, 0)
        # Same concrete producer before/after its consumer; includes repeated
        # generic cache hits and child cleanup on an enclosing delimiter error.
        contents = cleanup.read_text()
        start = contents.index("impl Token@JsonFactory {")
        end = contents.index("\nfn main()", start)
        reordered = work / "factory_declared_after_caller.tk"
        reordered.write_text(contents[:start] + contents[end:] + "\n" + contents[start:end])
        qualify(reordered, 0)
        qualify(FIXTURES / "generic_factory_borrow_escape.tk", 1)
        qualify(FIXTURES / "generic_factory_invalid_body.tk", 1, "E0408")
    print("Factory construction subset passed; container/JSON recovery remains a separate gate.")


if __name__ == "__main__":
    main()
