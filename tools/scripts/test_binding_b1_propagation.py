#!/usr/bin/env python3
"""Binding-only result! origins, replacement, cleanup and rejection regression."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/binding_b1"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-b1-propagation-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread", "-c",
                        str(ROOT / "lib/sys/toka_rt.c"), "-o", str(runtime)], check=True)
        def compile(source, *flags):
            return subprocess.run([str(compiler), "--workspace-node", "b1-propagation", "--workspace-root",
                str(ROOT if source.is_relative_to(ROOT) else work), str(source), *map(str, flags)],
                env=env, cwd=ROOT, capture_output=True, text=True, timeout=60)
        for name in ("propagated_view", "propagated_mapping", "propagated_cleanup"):
            source = FIXTURES / (name + ".tk")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            lines = {i + 1 for i, line in enumerate(source.read_text().splitlines()) if " = " in line and line.rstrip().endswith("!")}
            records = [r for r in json.loads(shadow.stdout)["records"]
                if r["location"]["file"].endswith(source.name) and r["location"]["line"] in lines
                and r["boundary"] in ("initialization", "assignment")]
            assert len(records) == len(lines), (lines, records)
            for record in records:
                plan = record["plan"]
                assert plan["outcome"] == "Admitted" and plan["source"] == "NoSourcePlace", plan
                if name == "propagated_cleanup":
                    assert plan["value_production"] == "ConsumeTemporary" and plan["liability_identity"], plan
                    assert plan["drop"] == "DestinationAssumesLiability", plan
                else:
                    assert plan["value_production"] == "CopyIdentity" and plan["drop"] == "NoLiability", plan
                    expected = "b" if name == "propagated_mapping" and record["boundary"] == "assignment" else "input"
                    roots = plan["dependency_roots"]
                    assert len(roots) == 1 and roots[0].endswith("binding:" + expected + ";"), plan
                    assert plan["referent_path"] == roots[0], plan
            executable = work / name
            built = compile(source, runtime, "-o", executable)
            assert built.returncode == 0, built.stderr
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            assert ran.returncode == 0, (name, ran.returncode, ran.stderr)
            print("PASS runtime/plan/parity: " + name, flush=True)

        suffix = "fn suffix(input: str) -> Result<str, str> <- input { return Result<str, str>::Ok(input) }\n"
        token = "shape Token(id: i32)\nimpl Token@Encap {\npub id\nfn drop(self#) {}\n}\n"
        negatives = {
            "local-view": suffix + "fn bad(input: str) -> Result<i32, str> <- input {\n"
                "auto remaining# = input\n{ auto owner = string::from(\"local\"); remaining = suffix(owner.as_str())! }\n"
                "return Result<i32, str>::Ok(remaining.len() as i32)\n}\n",
            "wrong-dependency": suffix + "fn bad(a: str, b: str) -> Result<str, str> <- a {\n"
                "auto remaining# = a\nremaining = suffix(b)!\nreturn Result<str, str>::Ok(remaining)\n}\n",
            "temporary-view": suffix + "fn bad() -> Result<i32, str> {\n"
                "auto remaining = suffix(string::from(\"temporary\").as_str())!\n"
                "return Result<i32, str>::Ok(remaining.len() as i32)\n}\n",
            "rollback": token + "fn read(value: Token) -> i32 { return value.id }\n"
                "fn take(cede value: Token) -> Result<str, str> { auto consumed = cede value; return Result<str, str>::Ok(\"static\") }\n"
                "fn bad() -> Result<i32, str> {\nauto source = Token(id = 7)\nauto target# = 0:i32\n"
                "target = take(cede source)!\nreturn Result<i32, str>::Ok(read(source) + target)\n}\n",
        }
        for name, body in negatives.items():
            source = work / (name + ".tk")
            source.write_text(body + "fn main() -> i32 { return 0 }\n")
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            assert "error[E" in normal.stderr and "E000" not in normal.stderr, (name, normal.stderr)
            expected = {"local-view": "E0456", "wrong-dependency": "E0454", "temporary-view": "E04661", "rollback": "E0408"}
            assert expected[name] in normal.stderr, (name, normal.stderr)
            assert not re.search(r"error\[E01", normal.stderr), (name, normal.stderr)
            if name == "rollback":
                assert "E0438" not in normal.stderr and "E0410" not in normal.stderr, normal.stderr
            for flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + extension)
                result = compile(source, flag, "-o", output)
                assert result.returncode == 1 and not output.exists(), (name, result.stderr)
            print("PASS rejection/parity/no-artifact: " + name + " " +
                  ",".join(sorted(set(re.findall(r"error\[(E\d+)\]", normal.stderr)))), flush=True)
    print("B1 propagation: 3 runtime/evidence pairs; 4 negative pairs; 8 no-artifact checks")


if __name__ == "__main__":
    main()
