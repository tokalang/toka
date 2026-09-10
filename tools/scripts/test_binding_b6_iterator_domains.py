#!/usr/bin/env python3
"""Raw interface domains, managed borrowing and shared-transfer responsibility."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/semantics/binding_b6_managed"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    with tempfile.TemporaryDirectory(prefix="toka-b6-domains-") as directory:
        work = Path(directory)
        clang = shutil.which("clang")
        assert clang, "clang required for live pointer-chain test driver"
        driver = work / "raw.o"
        subprocess.run([clang, "-c", str(CASES / "raw_accessors_driver.c"), "-o", str(driver)], check=True)
        for source in (ROOT / "tests/semantics/binding_b5_hashmap/handles.tk",
                       CASES / "iterators.tk", CASES / "raw_accessors.tk",
                       ROOT / "tests/pass/g07_hashmap_resize_test.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            json.loads(shadow.stdout)
            binary = work / source.stem
            linked = [driver] if source.name == "raw_accessors.tk" else []
            built = run(source, *linked, "-o", binary)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, (source.name, result.returncode, result.stderr)
            print("PASS runtime/parity: " + source.name, flush=True)

        for role in ("key", "value"):
            for handle in ("^", "~"):
                types = handle + "Cell, i32" if role == "key" else "i32, " + handle + "Cell"
                for api in ("next", "accessor"):
                    source = work / (role + ("-unique" if handle == "^" else "-shared") + "-" + api + ".tk")
                    prelude = "import std/hashmap::{HashMapIterator, Entry}\nshape Cell(id:i32)\n"
                    body = ("fn probe(cursor#: HashMapIterator<" + types + ">) { cursor#.next() }\n"
                            if api == "next" else
                            "fn probe(entry: Entry<" + types + ">) { entry." + ("key" if role == "key" else "val") + "() }\n")
                    source.write_text(prelude + body + "fn main()->i32 { return 0 }\n")
                    normal = run(source, "--check-only")
                    shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
                    assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
                    assert not re.search(r"error\[E01", normal.stderr), normal.stderr
                    expected = "E0621" if api == "next" else "E0492"
                    assert expected in normal.stderr, (api, normal.stderr)
                    if api == "next":
                        assert "raw_extendable" in normal.stderr, normal.stderr
                        assert ("generic parameter 'K" if role == "key" else "generic parameter 'V") in normal.stderr, normal.stderr
                    for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                        output = source.with_suffix(suffix)
                        failed = run(source, flag, "-o", output)
                        assert failed.returncode == 1 and not output.exists(), failed.stderr
                    if api == "next":
                        control = work / (source.stem + "-borrow.tk")
                        control.write_text(source.read_text().replace("cursor#.next()", "cursor#.next_ref()"))
                        accepted = run(control, "--check-only")
                        assert accepted.returncode == 0, accepted.stderr
                    print("PASS managed raw rejection: " + source.stem, flush=True)
    print("4 runtime/parity cases, 8 raw-domain rejections, 4 borrowed-domain controls.")
    print("raw_next_pending.tk remains a failed positive due to EntryRef's unbounded borrow domain.")
    print("shared_aggregate.tk remains a failed positive: bare morphic shared copy retains twice.")
    print("shared_transfer.tk remains a failed positive for wrapped aggregate transfer.")


if __name__ == "__main__":
    main()
