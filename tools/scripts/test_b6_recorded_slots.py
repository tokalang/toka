#!/usr/bin/env python3
"""Recorded-slot dependency proofs; no initialized-extent or JSON-completion claim."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


class ProcessResult:
    def __init__(self, cmd, pid, returncode, stdout, stderr):
        self.cmd = cmd
        self.pid = pid
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.signal = -returncode if returncode < 0 else None

    def describe(self):
        sig_str = f", signal={self.signal}" if self.signal is not None else ""
        return (f"Command: {' '.join(self.cmd)}\n"
                f"PID: {self.pid}, ReturnCode: {self.returncode}{sig_str}\n"
                f"STDERR:\n{self.stderr}\nSTDOUT:\n{self.stdout}")


def require(cond, msg, *results):
    if not cond:
        details = "\n---\n".join(r.describe() for r in results if hasattr(r, "describe"))
        raise AssertionError(f"{msg}\n{details}" if details else msg)


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
            cmd = [str(compiler), str(source), *map(str, flags)]
            proc = subprocess.Popen(cmd, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            pid = proc.pid
            stdout, stderr = proc.communicate(timeout=60)
            return ProcessResult(cmd, pid, proc.returncode, stdout, stderr)

        def check(name, text, expected):
            source = work / (name + ".tk")
            source.write_text(text)
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == expected, f"{name}: normal returncode {normal.returncode} != expected {expected}", normal)
            require(shadow.returncode == expected, f"{name}: shadow returncode {shadow.returncode} != expected {expected}", shadow)
            require(normal.stderr == shadow.stderr, f"{name}: normal vs shadow stderr mismatch", normal, shadow)
            json.loads(shadow.stdout)
            if expected:
                require("E04662" in normal.stderr, f"{name}: missing E04662 in stderr", normal)
                for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (name + suffix)
                    result = compile(source, mode, "-o", output)
                    require(result.returncode == 1, f"{name}: {mode} expected exit 1", result)
                    require(not output.exists(), f"{name}: {mode} unexpectedly created {output}", result)
            else:
                output = work / name
                result = compile(source, "-o", output)
                require(result.returncode == 0, f"{name}: compilation failed", result)
                proc_ran = subprocess.Popen([str(output)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                ran_out, ran_err = proc_ran.communicate(timeout=15)
                ran_res = ProcessResult([str(output)], proc_ran.pid, proc_ran.returncode, ran_out, ran_err)
                require(ran_res.returncode == 0, f"{name}: execution failed", ran_res)
            print("PASS " + name, flush=True)
            return source

        good = check("written", original, 0)
        indexed = original.replace("    auto item", "    auto offset = 0:i32\n    auto item")\
                          .replace("storage[0]", "storage[offset]")
        check("literal_index_binding", indexed, 0)
        check("literal_outside_extent", original.replace("storage[0]", "storage[1]"), 1)
        check("mutable_index", indexed.replace("offset = 0", "offset# = 0"), 1)
        check("different_index_binding", indexed.replace("    auto item", "    auto other = 0:i32\n    auto item")
              .replace("raw_take storage[offset]", "raw_take storage[other]"), 1)
        runtime = indexed.replace("fn main() -> i32 {", "fn scenario(seed: i32) -> i32 {")\
                         .replace("offset = 0:i32", "offset = seed")
        dynamic = check("runtime_index", runtime + "\nfn main() -> i32 { return scenario(0) }\n", 0)
        check("unsafe_index_wrapper", runtime.replace("storage[offset]", "storage[unsafe offset]")
              + "\nfn main() -> i32 { return scenario(0) }\n", 0)
        check("narrowing_index", runtime.replace("raw_take storage[offset]", "raw_take storage[(offset as i8)]")
              + "\nfn main() -> i32 { return 0 }\n", 1)
        check("converted_write_index", runtime.replace("storage[offset] =", "storage[(offset as i8)] =")
              + "\nfn main() -> i32 { return 0 }\n", 1)
        check("same_root_other_value", runtime.replace("    auto item", "    auto other = 1:i32\n    auto item")
              .replace("raw_take storage[offset]", "raw_take storage[other]")
              + "\nfn main() -> i32 { return 0 }\n", 1)
        check("shadowed_index", indexed.replace("        auto taken", "        {\n        auto offset = 0:i32\n        auto taken")
              .replace("        free [0] *storage", "        }\n        free [0] *storage"), 1)
        check("unknown_extent", indexed.replace("    auto *storage# = unsafe alloc [1] Item",
              "    auto extent = 1:i32\n    auto *storage# = unsafe alloc [extent] Item"), 0)
        joined = None
        for flag in ("true", "false"):
            joined = check("both_branches_" + flag,
                  original.replace("fn main() -> i32 {", "fn scenario(flag: bool) -> i32 {")
                  .replace("        storage[0] = cede item",
                           "        if flag { storage[0] = cede item } else { storage[0] = cede item }")
                  + "\nfn main() -> i32 { return scenario(" + flag + ") }\n", 0)
        for flag in ("true", "false"):
            joined = check("dynamic_branches_" + flag,
                  runtime.replace("scenario(seed: i32)", "scenario(seed: i32, flag: bool)")
                  .replace("        storage[offset] = cede item",
                           "        if flag { storage[offset] = cede item } else { storage[offset] = cede item }")
                  + "\nfn main() -> i32 { return scenario(0, " + flag + ") }\n", 0)
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
        rejected = work / "failed_write_restores_receipt.tk"
        rejected.write_text(indexed.replace("        auto taken", "        storage[offset] = true\n        auto taken"))
        normal = compile(rejected, "--check-only")
        shadow = compile(rejected, "--check-only", "--non-call-transfer-shadow=json")
        require(normal.returncode == 1, "failed_write_restores_receipt normal expected exit 1", normal)
        require(shadow.returncode == 1, "failed_write_restores_receipt shadow expected exit 1", shadow)
        require(normal.stderr == shadow.stderr, "failed_write_restores_receipt normal vs shadow stderr mismatch", normal, shadow)
        require("E04662" not in normal.stderr, "failed_write_restores_receipt should not contain E04662", normal)
        print("PASS failed_write_restores_receipt", flush=True)
        for fault in ("missing", "rejected", "incomplete", "slot-proof-missing", "slot-proof-mismatch"):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (fault + suffix)
                result = compile(good, "--raw-take-fault=" + fault, mode, "-o", output)
                require(result.returncode == 1, f"fault {fault} {mode} expected exit 1", result)
                require("E0701" in result.stderr, f"fault {fault} {mode} missing E0701", result)
                require(not output.exists(), f"fault {fault} {mode} unexpectedly created {output}", result)
        for source in (dynamic, joined):
            for fault in ("slot-index", "slot-allocation", "slot-leaf"):
                for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (source.stem + fault + suffix)
                    result = compile(source, "--raw-take-fault=" + fault, mode, "-o", output)
                    require(result.returncode == 1, f"fault {fault} {mode} for {source.name} expected exit 1", result)
                    require("E0701" in result.stderr, f"fault {fault} {mode} for {source.name} missing E0701", result)
                    require(not output.exists(), f"fault {fault} {mode} for {source.name} unexpectedly created {output}", result)
        print("PASS fault-injected proof rejection without artifacts", flush=True)


if __name__ == "__main__":
    main()
