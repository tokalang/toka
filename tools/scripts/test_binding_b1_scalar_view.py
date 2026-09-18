#!/usr/bin/env python3
"""Approved B1 scalar classification and explicit owner/view lifetime matrix."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/binding_b1"


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
    with tempfile.TemporaryDirectory(prefix="toka-b1-scalar-view-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread", "-c",
                        str(ROOT / "lib/sys/toka_rt.c"), "-o", str(runtime)], check=True)
        def compile(source, *flags):
            cmd = [str(compiler), "--workspace-node", "b1-scalar-view", "--workspace-root",
                   str(ROOT if source.is_relative_to(ROOT) else work), str(source), *map(str, flags)]
            proc = subprocess.Popen(cmd, env=env, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            pid = proc.pid
            stdout, stderr = proc.communicate(timeout=60)
            return ProcessResult(cmd, pid, proc.returncode, stdout, stderr)

        for source in (ROOT / "tests/pass/g03_bitwise.tk", ROOT / "tests/pass/g03_chain_static.tk",
                       FIXTURES / "scalar_and_shared.tk", FIXTURES / "owner_view.tk"):
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == 0, f"check-only failed for {source.name}", normal)
            require(shadow.returncode == 0, f"shadow check-only failed for {source.name}", shadow)
            require(normal.stderr == shadow.stderr, f"normal vs shadow stderr mismatch for {source.name}", normal, shadow)
            if source.name == "scalar_and_shared.tk":
                lines = source.read_text().splitlines()
                scalar_lines = {i + 1 for i, line in enumerate(lines) if "= ~ " in line}
                records = [r for r in json.loads(shadow.stdout)["records"] if r["location"]["file"].endswith(source.name)
                           and r["boundary"] == "initialization" and r["location"]["line"] in scalar_lines]
                assert len(records) == 2, records
                for record in records:
                    p = record["plan"]
                    assert (p["outcome"], p["source_view"], p["value_production"], p["source"], p["drop"]) == (
                        "Admitted", "DirectValue", "CopyValue", "NoSourcePlace", "NoLiability"), p
                    assert not p["exact_path"] and not p["liability_identity"], p
            binary = work / source.stem
            built = compile(source, runtime, "-o", binary)
            require(built.returncode == 0, f"binary compilation failed for {source.name}", built)
            proc_ran = subprocess.Popen([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            ran_out, ran_err = proc_ran.communicate(timeout=10)
            ran_res = ProcessResult([str(binary)], proc_ran.pid, proc_ran.returncode, ran_out, ran_err)
            require(ran_res.returncode == 0, f"execution failed for {source.name}", ran_res)
            if source.name == "owner_view.tk":
                ir = work / "owner.ll"
                emitted = compile(source, "--emit-llvm", "-o", ir)
                require(emitted.returncode == 0, f"LLVM IR emission failed for {source.name}", emitted)
                body = re.search(r"^define[^\n]*@retained_view\([^\n]*\n.*?^}", ir.read_text(), re.M | re.S)
                assert body, "missing retained_view IR"
                drops = list(re.finditer(r"call void @Encap_string_drop\(ptr %owner\)", body[0]))
                assert len(drops) == 1 and body[0].index("@str_len(") < drops[0].start(), body[0]
            print("PASS runtime/parity: " + source.name, flush=True)
        negatives = {
            "cede-bitwise": ("fn main() -> i32 {\nauto a = 12\nauto b = cede (~ a)\nreturn a\n}\n", "E04661"),
            "moved-shared": ("shape Cell(value: i32)\nfn main() -> i32 {\nauto ~a = new Cell(value = 1)\n"
                "auto ~b = cede ~a\nreturn a.value\n}\n", "E0438"),
            "local-reference-escape": ("fn bad() -> &str {\nauto owner = string::from(\"local\")\nauto view = owner.as_view()\nreturn &view\n}\n"
                "fn main() -> i32 { return 0 }\n", "E0455"),
            "temporary-view": ("fn main() -> i32 {\nauto view = string::from(\"temporary\").as_view()\n"
                "return view.byte_at(0) as i32\n}\n", "E04661"),
        }
        for name, (body, diagnostic) in negatives.items():
            source = work / (name + ".tk")
            source.write_text(body)
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == 1, f"negative check-only expected exit 1 for {name}", normal)
            require(shadow.returncode == 1, f"negative shadow check-only expected exit 1 for {name}", shadow)
            require(normal.stderr == shadow.stderr, f"negative normal vs shadow stderr mismatch for {name}", normal, shadow)
            require(diagnostic in normal.stderr, f"diagnostic {diagnostic} missing in {name}", normal)
            for flag, ext in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + ext)
                result = compile(source, flag, "-o", output)
                require(result.returncode == 1, f"{name} {flag} expected exit 1", result)
                require(not output.exists(), f"{name} {flag} unexpectedly created {output}", result)
            print("PASS rejection/parity/no-artifact: " + name, flush=True)
        print("B1 scalar/view: 4 runtime positives; scalar evidence and owner cleanup IR; 4 negative pairs, 8 no-artifact checks")


if __name__ == "__main__":
    main()
