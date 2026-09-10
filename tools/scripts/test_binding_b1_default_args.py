#!/usr/bin/env python3
"""B1 generated SourceLoc metadata regression; not whole B1 acceptance."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    source = ROOT / "tests/pass/g03_default_args.tk"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    def run(*flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)], cwd=ROOT,
                              env=env, capture_output=True, text=True, timeout=60)
    normal = run("--check-only")
    shadow = run("--check-only", "--non-call-transfer-shadow=json")
    assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
    records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name)
               and r["location"]["line"] == 26 and r["boundary"] == "initialization"]
    assert len(records) == 1 and records[0]["plan"]["outcome"] == "Admitted", records
    with tempfile.TemporaryDirectory(prefix="toka-b1-default-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread", "-c",
                        str(ROOT / "lib/sys/toka_rt.c"), "-o", str(runtime)], check=True)
        binary = work / "default-args"
        built = run(runtime, "-o", binary)
        assert built.returncode == 0, built.stderr
        executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        assert executed.returncode == 0, (executed.returncode, executed.stderr)
        escaped = work / "escaped-location.tk"
        escaped.write_text("import core/types::SourceLoc\n"
            "fn get_loc(loc: SourceLoc = __LOC__) -> SourceLoc <- loc { auto result = SourceLoc(file = loc.file.clone(), line = loc.line); return cede result }\n"
            "fn escaped() -> &SourceLoc {\nauto loc = get_loc(..)\nreturn &loc\n}\n"
            "fn main() -> i32 { return 0 }\n")
        for flag, ext in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("escaped" + ext)
            result = subprocess.run([str(compiler), "--workspace-node", "b1-default-test", "--workspace-root", str(work),
                                     str(escaped), flag, "-o", str(output)], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 1 and "E0455" in result.stderr and not output.exists(), result.stderr
    print("B1 default arguments: original runtime/parity/binding receipt PASS; local descriptor escape rejected without object/IR")


if __name__ == "__main__":
    main()
