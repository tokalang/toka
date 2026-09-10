#!/usr/bin/env python3
"""Validated static return facts; no type/empty-dependency lifetime shortcuts."""
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
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-b3-static-") as directory:
        work = Path(directory)
        def run(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)], cwd=ROOT,
                env=env, capture_output=True, text=True, timeout=60)
        for source in (ROOT / "tests/semantics/stage1_return_matrix/static_storage_chain.tk",
                       ROOT / "tests/semantics/binding_b3_static_return/factories.tk",
                       ROOT / "tests/semantics/binding_b3_static_return/resource_and_dynamic.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name)]
            lines = source.read_text().splitlines()
            names = {"static_storage_chain.tk": ("result",), "resource_and_dynamic.tk": ("static_result",)}.get(
                source.name, ("first", "second", "chained", "selected", "number", "boolean"))
            for name in names:
                line = next(i+1 for i,s in enumerate(lines) if "auto " + name + " =" in s)
                matches = [r for r in records if r["boundary"] == "initialization" and r["location"]["line"] == line]
                assert len(matches) == 1, matches
                record = matches[0]; plan = record["plan"]
                assert plan["outcome"] == "Admitted" and plan["value_production"] == "CopyIdentity" and plan["drop"] == "NoLiability", plan
                assert record.get("static_storage_origins") and not plan["dependency_roots"] and not plan["referent_path"] and not plan["liability_identity"], record
                if name == "selected": assert len(set(record["static_storage_origins"])) == 2, record
            if source.name == "factories.tk":
                for name, count in (("text", 1), ("wrapper", 1), ("choose", 2)):
                    returned = [r for r in records if r["boundary"] == "return" and ";function:"+name+";" in r["group_identity"]]
                    assert len(returned) == count, (name, returned)
            if source.name == "resource_and_dynamic.tk":
                line = next(i+1 for i,s in enumerate(lines) if "auto dynamic_result =" in s)
                matches = [r for r in records if r["boundary"] == "initialization" and r["location"]["line"] == line]
                assert len(matches) == 1 and not matches[0].get("static_storage_origins"), matches
                assert matches[0]["plan"]["dependency_roots"] and all("binding:owner;" in p for p in matches[0]["plan"]["dependency_roots"]), matches
            binary = work / source.stem
            built = run(source, "-o", binary)
            assert built.returncode == 0, built.stderr
            executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            assert executed.returncode == 0, (source.name, executed.returncode, executed.stderr)
            print("PASS runtime/parity/static evidence: " + source.name, flush=True)

        negatives = {
            "mixed": 'fn choose(flag: bool) -> str {\nif flag { return "static" }\nauto owner = string::from("local")\nreturn owner.as_view()\n}\nfn main() -> i32 { auto value = choose(false); return value.len() as i32 }\n',
            "nested_closure": 'fn text() -> str {\nauto callback = { => "not the outer return" }:fn() -> str\nauto owner = string::from("local")\nreturn owner.as_view()\n}\nfn main() -> i32 { auto value = text(); return value.len() as i32 }\n',
            "descriptor": 'fn text() -> str { return "static" }\nfn bad() -> &str { auto view = text(); return &view }\nfn main() -> i32 { return 0 }\n',
            "invalid": 'fn main() -> i32 { auto first = text(); auto second = text(); return 0 }\nfn text() -> str { auto wrong = true:i32; return "static" }\n',
            "fallthrough": 'fn main() -> i32 { auto view = text(false); return 0 }\nfn text(flag: bool) -> str { if flag { return "static" } }\n',
            "recursion": 'fn main() -> i32 { auto view = text(); return 0 }\nfn text() -> str { return text() }\n',
            "mutual": 'fn main() -> i32 { auto view = first(); return 0 }\nfn first() -> str { return second() }\nfn second() -> str { return first() }\n',
            "dynamic_escape": 'fn choose(value: str, flag: bool) -> str <- value {\nif flag { return "static" }\nreturn value\n}\nfn bad() -> str { auto owner = string::from("local"); return choose(owner.as_str(), false) }\nfn main() -> i32 { return 0 }\n',
            "generic_invalid_cache": 'fn main() -> i32 { auto first = text(true); auto second = text(true); return 0 }\nfn text<T>(value:T) -> str { auto wrong = value:i32; return "static" }\n',
            "rollback": 'shape Token(value:i32)\nimpl Token@Encap {\npub value\nfn drop(self#) {}\n}\nfn read(value:Token)->i32 { return value.value }\nfn main()->i32 {\nauto token=Token(value=7)\nauto result=text(cede token)\nreturn read(token)\n}\nfn text(cede token:Token)->str {auto owned=cede token; auto wrong=true:i32; return "static"}\n',
            "journal": 'fn main()->i32 {auto failed=bad(1); auto recovered=text(); return 0}\nfn bad<T>(_value:T)->i32 {auto view=text(); return true}\nfn text()->str {return "static"}\n',
            "cede_temporary": 'fn text()->str {return "static"}\nfn main()->i32 {auto value=cede text(); return 0}\n',
        }
        for name, body in negatives.items():
            source = work / (name + ".tk"); source.write_text(body)
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            assert "error[E" in normal.stderr and not re.search(r"error\[E01", normal.stderr), (name, normal.stderr)
            expected = {"mixed": "E04661", "nested_closure": "E04661", "descriptor": "E0455",
                        "invalid": "E04606", "fallthrough": "E0431", "recursion": "E04658", "mutual": "E04658",
                        "dynamic_escape": "E0455", "generic_invalid_cache": "E04606", "rollback": "E04606", "journal": "E0408", "cede_temporary": "E04661"}
            assert expected[name] in normal.stderr, (name, normal.stderr)
            if name == "rollback": assert "E0438" not in normal.stderr and "E0410" not in normal.stderr, normal.stderr
            records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name)]
            if name == "journal":
                definitions = [r for r in records if r["boundary"] == "return" and ";function:text;" in r["group_identity"]]
                assert len(definitions) == 1 and definitions[0].get("static_storage_origins"), definitions
            if name not in ("descriptor", "dynamic_escape", "journal"):
                assert not any(r["boundary"] == "initialization" and r["plan"]["outcome"] == "Admitted" and r.get("static_storage_origins")
                               for r in records if ";function:main;" in r["group_identity"]), (name, records)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                rejected = run(source, flag, "-o", output)
                assert rejected.returncode == 1 and not output.exists(), (name, rejected.stderr)
            print("PASS rejection/parity/no-artifact: " + name + " " + ",".join(sorted(set(re.findall(r"error\[(E\d+)\]", normal.stderr)))), flush=True)

        provider = work / "provider.tk"
        provider.write_text('pub fn text() -> str { return "static" }\n')
        consumer = work / "consumer.tk"
        consumer.write_text('import provider::{text}\nfn main() -> i32 { auto value = text(); if !value.equals("static") { return 1 } return 0 }\n')
        visible = run(consumer, "-I", work, "--check-only")
        visible_shadow = run(consumer, "-I", work, "--check-only", "--non-call-transfer-shadow=json")
        assert visible.returncode == visible_shadow.returncode == 0 and visible.stderr == visible_shadow.stderr, visible.stderr + visible_shadow.stderr
        binary = work / "imported"
        built = run(consumer, "-I", work, "-o", binary)
        assert built.returncode == 0, built.stderr
        assert subprocess.run([str(binary)], capture_output=True, timeout=10).returncode == 0
        print("PASS source-visible import: runtime/parity")
        emitted = run(provider, "-c", "--emit-interface", "-o", work / "provider.o")
        assert emitted.returncode == 0, emitted.stderr
        provider.rename(work / "provider.source")
        hidden = run(consumer, "-I", work, "--check-only")
        hidden_shadow = run(consumer, "-I", work, "--check-only", "--non-call-transfer-shadow=json")
        assert hidden.returncode == hidden_shadow.returncode == 1 and hidden.stderr == hidden_shadow.stderr, hidden.stderr + hidden_shadow.stderr
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("hidden" + suffix)
            rejected = run(consumer, "-I", work, flag, "-o", output)
            assert rejected.returncode == 1 and "E04661" in rejected.stderr and not output.exists(), rejected.stderr
        print("PASS source-hidden missing static proof: rejected without object/IR")
    print("Static return binding: complete witnesses only; original plus forward-order/cache/generic/branch runtime controls; no skipped negatives")


if __name__ == "__main__":
    main()
