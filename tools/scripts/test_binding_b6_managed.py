#!/usr/bin/env python3
"""Inferred morphic target identity; HashMap iterator domain remains pending."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/semantics/binding_b6_managed/factory.tk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)

    normal = run(FIXTURE, "--check-only")
    shadow = run(FIXTURE, "--check-only", "--non-call-transfer-shadow=json")
    assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
    records = [r for r in json.loads(shadow.stdout)["records"] if
               r["location"]["file"].endswith(FIXTURE.name) and r["boundary"] == "initialization"]
    plans = [r["plan"] for r in records if r["plan"]["actual_type"] in ("^Cell", "~Cell")]
    assert len(plans) >= 7, plans
    for plan in plans:
        assert plan["outcome"] == "Admitted" and plan["actual_type"] == plan["formal_type"], plan
        assert plan["source_view"] in ("UniqueHandle", "SharedHandle"), plan
        assert plan["copy_proof"] == "ProvenNonCopy", plan
        if plan["source"] == "KeepLive":
            assert plan["source_view"] == "SharedHandle" and plan["drop"] == "SharedLiabilityIncremented", plan
        else:
            assert plan["drop"] == "DestinationAssumesLiability", plan

    with tempfile.TemporaryDirectory(prefix="toka-b6-managed-") as directory:
        work = Path(directory)
        binary = work / "factory"
        built = run(FIXTURE, "-o", binary)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(binary)], timeout=15).returncode == 0
        ir = work / "factory.ll"
        emitted = run(FIXTURE, "--emit-llvm", "-o", ir)
        assert emitted.returncode == 0, emitted.stderr
        main_ir = re.search(r'^define i32 @main\([^\n]*\) \{(.*?)^}', ir.read_text(), re.M | re.S).group(1)
        transfers = re.findall(r'(%move\.val\d*) = load ptr, ptr (%value\d*),[^\n]*\n'
                               r'\s*store ptr null, ptr \2,[^\n]*\n\s*store ptr \1, ptr %moved', main_ir)
        assert len(transfers) == 2, main_ir[:6000]
        prelude = FIXTURE.read_text().split("fn main()", 1)[0]
        cases = {
            "moved": "auto ^value = make_unique(); auto ^moved = cede ^value; return value.id",
            "borrow": "auto ^value = make_unique(); auto &^view = &^value; auto ^moved = cede ^value; return view.id",
            "write": "auto ~value = make_shared(); value.id = 9; return 0",
            "copy": "auto ^value = make_unique(); duplicate<^Cell>(^value); return value.id",
        }
        for name, body in cases.items():
            source = work / (name + ".tk")
            helper = "fn duplicate<T>(value:T)->i32 { auto copied=value; return 0 }\n" if name == "copy" else ""
            source.write_text(prelude + helper + "fn main() -> i32 { " + body + " }\n")
            rejected = run(source, "--check-only")
            observed = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert rejected.returncode == observed.returncode == 1 and rejected.stderr == observed.stderr, (name, rejected.stderr, observed.stderr)
            assert not re.search(r"error\[E01", rejected.stderr), rejected.stderr
            expected = {"moved": "E0438", "borrow": "E0440", "write": "E04573", "copy": "E04661"}[name]
            assert expected in rejected.stderr, (name, rejected.stderr)
            if name == "copy":
                assert "MissingCedeForNamedSource" in rejected.stderr, rejected.stderr
            if name in ("borrow", "copy"):
                assert "E0438" not in rejected.stderr and "E0410" not in rejected.stderr, rejected.stderr
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                result = run(source, flag, "-o", output)
                assert result.returncode == 1 and not output.exists(), result.stderr
    print("PASS unique/shared target identity, source invalidation, shared survivor, exact-once;")
    print("use-after-move/PAL/write-ceiling rejection, parity and no-artifact controls.")
    print("NOT a complete handles.tk or HashMap iterator qualification.")


if __name__ == "__main__":
    main()
