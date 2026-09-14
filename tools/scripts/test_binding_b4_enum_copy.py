#!/usr/bin/env python3
"""Copy proof follows stored enum payloads, including generic full morphology."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/semantics/binding_b4_enum_copy"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-b4-enum-") as directory:
        work = Path(directory)
        def run(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)], cwd=ROOT,
                env=env, capture_output=True, text=True, timeout=60)
        for source in (CASES / "values.tk", CASES / "callable_modes.tk", CASES / "path_temporary.tk",
                       ROOT / "tests/pass/g03_path.tk",
                       ROOT / "tests/semantics/enum_payload_cleanup/lifecycle.tk",
                       ROOT / "tests/conformance/std/vec_nested_generic_copy_witness.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name)
                       and r["boundary"] == "initialization"]
            if source.name in ("values.tk", "callable_modes.tk"):
                lines = source.read_text().splitlines()
                for record in records:
                    plan = record["plan"]
                    text = lines[record["location"]["line"]-1]
                    assert plan["outcome"] == "Admitted", plan
                    if source.name == "values.tk" and any("auto " + name + " =" in text for name in
                        ("scalar", "copied", "pure", "pure_copy", "phantom", "phantom_copy")):
                        assert plan["copy_proof"] == "ProvenCopy" and plan["value_production"] == "CopyValue" and plan["drop"] == "NoLiability", plan
                    else:
                        assert plan["copy_proof"] == "ProvenNonCopy", plan
                        if "= cede " in text:
                            assert plan["value_production"] == "MoveOwned" and plan["source"].startswith("Invalidate"), plan
                        else:
                            assert plan["value_production"] == "ConsumeTemporary" and plan["source"] == "NoSourcePlace", plan
                        if source.name == "values.tk" and not any("auto " + n + " =" in text for n in ("plain", "plain_moved")):
                            assert plan["drop"] == "DestinationAssumesLiability" and plan["liability_identity"], plan
                        else:
                            assert plan["drop"] == "NoLiability", plan
            binary = work / source.stem
            built = run(source, "-o", binary)
            assert built.returncode == 0, built.stderr
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, (source.name, result.returncode, result.stderr)
            print("PASS runtime/parity/proof: " + source.name, flush=True)

        prelude = "shape Token(value:i32)\nimpl Token@Encap {\nfn drop(self#) {}\n}\nshape Packet<T>(Empty | Value(T))\n"
        cases = {
            "resource_copy": prelude + "fn main()->i32 {auto value=Packet<Token>::Value(Token(value=1)); auto copied=value; return 0}\n",
            "nested_copy": prelude + "fn main()->i32 {auto value=Packet<Option<Token>>::Value(Option<Token>::Some(Token(value=1))); auto copied=value; return 0}\n",
            "consuming_copy": "shape Packet<T>(Empty | Value(T))\nfn main()->i32 {auto once={=>7}:cede fn()->i32; auto value=Packet<cede fn()->i32>::Value(cede once); auto copied=value; return 0}\n",
            "direct_consuming_copy": "shape Packet<T>(Empty | Value(cede fn()->i32))\nfn main()->i32 {auto once={=>7}:cede fn()->i32; auto value=Packet<i32>::Value(cede once); auto copied=value; return 0}\n",
            "moved_source": prelude + "fn read(value:Packet<Token>)->i32{return 0}\nfn main()->i32 {auto value=Packet<Token>::Value(Token(value=1)); auto moved=cede value; return read(value)}\n",
            "no_drop_noncopy": "shape Item(value:i32)\nimpl Item@Encap {pub value}\nshape Packet<T>(Empty | Value(T))\nfn main()->i32 {auto value=Packet<Item>::Value(Item(value=1)); auto copied=value; return 0}\n",
            "borrow_conflict": prelude + "fn read(value:Packet<Token>)->i32{return 0}\nfn main()->i32 {auto value=Packet<Token>::Value(Token(value=1)); auto &view=&value; auto moved=cede value; return read(view)}\n",
            "borrowed_parameter": prelude + "fn blocked(value:Packet<Token>) {auto moved=cede value}\nfn main()->i32{return 0}\n",
            "temporary_cede": "fn main()->i32 {auto value=cede string::from(\"temporary\"); return 0}\n",
        }
        for name, body in cases.items():
            source = work / (name + ".tk"); source.write_text(body)
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            diagnostic = "E0438" if name == "moved_source" else "E04661"
            assert not re.search(r"error\[E01", normal.stderr), (name, normal.stderr)
            if name not in ("borrow_conflict", "borrowed_parameter", "temporary_cede"):
                assert diagnostic in normal.stderr, (name, normal.stderr)
                if name != "moved_source": assert "MissingCedeForNamedSource" in normal.stderr, normal.stderr
            elif name == "temporary_cede":
                assert "E04661" in normal.stderr and "ExplicitCedeRequiresSource" in normal.stderr, normal.stderr
            else:
                assert ("E0440" if name == "borrow_conflict" else "E0473") in normal.stderr, normal.stderr
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                failed = run(source, flag, "-o", output)
                assert failed.returncode == 1 and not output.exists(), failed.stderr
            print("PASS rejection/parity/no-artifact: " + name + " " +
                  ",".join(sorted(set(re.findall(r"error\[(E\d+)\]", normal.stderr)))), flush=True)

        source = CASES / "borrowed_holder_escape.tk"
        normal = run(source, "--check-only")
        shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
        assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and "E04661" in normal.stderr, normal.stderr + shadow.stderr
        records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name) and r["boundary"] == "initialization"]
        source_lines = source.read_text().splitlines()
        once_line = next(i+1 for i,s in enumerate(source_lines) if "auto once =" in s)
        holder_line = next(i+1 for i,s in enumerate(source_lines) if "auto holder =" in s)
        once = [r for r in records if r["location"]["line"] == once_line]
        holder = [r for r in records if r["location"]["line"] == holder_line]
        assert len(once) == len(holder) == 1 and once[0]["plan"]["outcome"] == "Admitted" and once[0]["plan"]["dependency_roots"], records
        assert holder[0]["plan"]["outcome"] == "Rejected", records
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("escaping-holder" + suffix)
            failed = run(source, flag, "-o", output)
            assert failed.returncode == 1 and not output.exists(), failed.stderr
        print("PASS dependent callable environment is not promoted to independent holder")

        for bound, accepted in (("T: @Copy", True), ("T", False)):
            source = work / ("copy-domain-valid.tk" if accepted else "copy-domain-invalid.tk")
            source.write_text("shape Packet<T>(Empty | Value(T))\nimpl<T> Packet<T>@Encap {}\n"
                              "impl<" + bound + "> Packet<T>@Copy {}\nfn main()->i32 {return 0}\n")
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == (0 if accepted else 1) and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            if not accepted:
                assert "E0406" in normal.stderr, normal.stderr
                output = work / "unproven-domain.o"
                failed = run(source, "-c", "-o", output)
                assert failed.returncode == 1 and not output.exists(), failed.stderr
        print("PASS generic @Copy domain requires actual payload proof")

        provider = work / "provider.tk"
        provider.write_text("pub shape Packet<T>(Empty | Value(T))\npub shape Phantom<T>(First | Second)\n")
        emitted = run(provider, "-c", "--emit-interface", "-o", work / "provider.o")
        assert emitted.returncode == 0, emitted.stderr
        tki = work / "provider.tki"
        interface = tki.read_text()
        assert "copy_recipe: Packet = all(T:@Copy)" in interface, interface
        assert "copy_recipe: Phantom = always" in interface, interface
        provider.rename(work / "provider.source")
        consumer = work / "consumer.tk"
        consumer.write_text("import provider::{Packet}\nfn main()->i32 {auto value=Packet<i32>::Value(7); auto copied=value; return 0}\n")
        copied = run(consumer, "-I", work, "-o", work / "imported")
        assert copied.returncode == 0, copied.stderr
        assert subprocess.run([str(work / "imported")], timeout=10).returncode == 0
        consumer.write_text("import provider::{Packet}\nfn main()->i32 {auto value=Packet<string>::Value(string::from(\"owned\")); auto copied=value; return 0}\n")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("imported-copy" + suffix)
            failed = run(consumer, "-I", work, flag, "-o", output)
            assert failed.returncode == 1 and not output.exists(), failed.stderr
        # An old/false unconditional recipe must not override the real fields.
        tki.write_text(interface.replace("copy_recipe: Packet = all(T:@Copy)",
                                         "copy_recipe: Packet = always"))
        stale = run(consumer, "-I", work, "-c", "-o", work / "stale.o")
        assert stale.returncode == 1 and not (work / "stale.o").exists(), stale.stderr
        print("PASS source-hidden generic enum: actual payload controls Copy; no interface schema change")
    print("Enum Copy graph: six runtime cases, ten negative pairs, Copy-domain and source-hidden controls; no skipped cases")


if __name__ == "__main__":
    main()
