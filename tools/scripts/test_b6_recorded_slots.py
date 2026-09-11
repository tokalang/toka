#!/usr/bin/env python3
"""Recorded-slot dependency proofs; no initialized-extent or JSON-completion claim."""
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
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    original = (ROOT / "tests/semantics/json_leaf_factories/recorded_slot_take.tk").read_text()
    with tempfile.TemporaryDirectory(prefix="toka-recorded-slots-") as directory:
        work = Path(directory)

        def compile(source, *flags):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  cwd=ROOT, env=env, text=True, capture_output=True, timeout=60)

        def check(name, text, expected):
            source = work / (name + ".tk")
            source.write_text(text)
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == expected, (name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, (name, normal.stderr, shadow.stderr)
            json.loads(shadow.stdout)
            if expected:
                assert "E04662" in normal.stderr, (name, normal.stderr)
                for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (name + suffix)
                    result = compile(source, mode, "-o", output)
                    assert result.returncode == 1 and not output.exists(), (name, result.stderr)
            else:
                output = work / name
                result = compile(source, "-o", output)
                assert result.returncode == 0, (name, result.stderr)
                ran = subprocess.run([str(output)], timeout=15)
                assert ran.returncode == 0, (name, ran.returncode)
            print("PASS " + name, flush=True)
            return source

        good = check("written", original, 0)
        for flag in ("true", "false"):
            check("both_branches_" + flag,
                  original.replace("fn main() -> i32 {", "fn scenario(flag: bool) -> i32 {")
                  .replace("        storage[0] = cede item",
                           "        if flag { storage[0] = cede item } else { storage[0] = cede item }")
                  + "\nfn main() -> i32 { return scenario(" + flag + ") }\n", 0)
        check("transfer_twice", original.replace("        if taken.id", "        storage[0] = cede taken\n        auto final_value = raw_take storage[0]\n        if final_value.id"), 0)
        check("vector_value", (ROOT / "tests/semantics/json_leaf_factories/recorded_vec_take.tk").read_text(), 0)
        check("no_write", original.replace("        storage[0] = cede item\n", ""), 1)
        check("wrong_slot", original.replace("alloc [1]", "alloc [2]").replace("raw_take storage[0]", "raw_take storage[1]"), 1)
        check("retired", original.replace("        if taken.id", "        auto again = raw_take storage[0]\n        if taken.id"), 1)
        check("unknown_call", "extern fn opaque() -> void\n" + original.replace("        auto taken", "        opaque()\n        auto taken"), 1)
        check("released", original.replace("        auto taken", "        free [0] *storage\n        auto taken"), 1)
        check("maybe_written", original.replace("fn main() -> i32 {", "fn scenario(flag: bool) -> i32 {")
              .replace("        storage[0] = cede item", "        if flag { storage[0] = cede item }") + "\nfn main() -> i32 { return 0 }\n", 1)
        check("alias_write", original.replace("        auto taken", "        auto *other_buffer# = *storage\n        other_buffer[0] = Item(*marker = null, id = 8)\n        auto taken"), 1)
        check("loop_may_not_run", original.replace("fn main() -> i32 {", "fn scenario(flag: bool) -> i32 {")
              .replace("        storage[0] = cede item", "        loop flag { storage[0] = cede item; break }") + "\nfn main() -> i32 { return 0 }\n", 1)
        check("match_may_not_write", original.replace("fn main() -> i32 {", "fn scenario(flag: bool) -> i32 {")
              .replace("        storage[0] = cede item", "        match flag { true => { storage[0] = cede item } false => {} }") + "\nfn main() -> i32 { return 0 }\n", 1)
        check("borrowed_field", original.replace("id: i32)", "id: i32, view: str)")
              .replace("id = 7)", 'id = 7, view = "borrowed")'), 1)
        check("non_null_field", original.replace("    auto item", "    auto integer = 9\n    auto item")
              .replace("*marker = null", "*marker = &integer as nul *i32"), 1)
        check("enum_single_raw_payload", """
shape RawHolder(nul *pointer: i32)
shape Outcome(Empty | Value(RawHolder))
fn extract(*storage: [Outcome]) -> Outcome {
    unsafe { return raw_take storage[0] }
}
fn main() -> i32 { return 0 }
""", 1)
        for fault in ("missing", "rejected", "incomplete", "slot-proof-missing", "slot-proof-mismatch"):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (fault + suffix)
                result = compile(good, "--raw-take-fault=" + fault, mode, "-o", output)
                assert result.returncode == 1 and "E0701" in result.stderr and not output.exists(), (fault, result.stderr)
        print("PASS fault-injected proof rejection without artifacts", flush=True)


if __name__ == "__main__":
    main()
