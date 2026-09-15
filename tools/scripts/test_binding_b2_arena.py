#!/usr/bin/env python3
"""Arena's existing raw storage contract: permissions and exact cleanup."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/semantics/binding_b2_arena"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    compiler = Path(parser.parse_args().build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    def compile(source, *flags):
        return subprocess.run([str(compiler), str(source), *map(str, flags)], cwd=ROOT,
            env=env, capture_output=True, text=True, timeout=60)
    with tempfile.TemporaryDirectory(prefix="toka-b2-arena-gate-") as directory:
        work = Path(directory)
        for source in (ROOT / "tests/semantics/stage1_return_matrix/arena_raw_handle_return.tk",
                       ROOT / "tests/pass/g07_arena_test.tk", CASES / "nullable_flow.tk"):
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
            binary = work / source.stem
            built = compile(source, "-o", binary)
            assert built.returncode == 0, built.stderr
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            assert ran.returncode == 0, (source.name, ran.returncode, ran.stdout, ran.stderr)
            print("PASS original runtime/parity: " + source.name, flush=True)

        source = CASES / "lifecycle.tk"
        normal = compile(source, "--check-only")
        shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
        assert normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr, normal.stderr + shadow.stderr
        records = [r for r in json.loads(shadow.stdout)["records"]
            if r["location"]["file"].endswith("std/arena.tk") and r.get("raw_write_authority")]
        assert records and all(r["raw_write_authority"] == "UnsafeCallerPrecondition" and
                              r["plan"]["drop"] == "NoLiability" for r in records), records
        ir = work / "arena.ll"
        emitted = compile(source, "--emit-llvm", "-o", ir)
        assert emitted.returncode == 0, emitted.stderr
        text = ir.read_text()
        counts = {}
        def instrument(match):
            body = match[0]
            name = match[1]
            body, mallocs = re.subn(r"@malloc\(", "@b2_arena_malloc(", body)
            body, frees = re.subn(r"@free\(", "@b2_arena_free(", body)
            counts[name] = (mallocs, frees)
            return body
        instrumented = re.sub(r"^define[^\n]*@(Arena_alloc_bytes|Arena_release)\([^\n]*\{\n.*?^}",
                              instrument, text, flags=re.M | re.S)
        assert counts == {"Arena_alloc_bytes": (2, 1), "Arena_release": (0, 2)}, counts
        instrumented += "\ndeclare ptr @b2_arena_malloc(i64)\ndeclare void @b2_arena_free(ptr)\n"
        scoped = work / "arena-counted.ll"
        scoped.write_text(instrumented)
        binary = work / "arena-counted"
        linked = subprocess.run([os.environ.get("CC", "clang"), str(scoped),
            str(ROOT / "tests/runtime/arena_allocation_hooks.c"), str(ROOT / "lib/sys/toka_rt.c"),
            "-pthread", "-lm", "-o", str(binary)], capture_output=True, text=True, timeout=60)
        assert linked.returncode == 0, linked.stderr
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        assert ran.returncode == 0, (ran.returncode, ran.stdout, ran.stderr)
        print("PASS scoped Arena allocation/release: zero, multi-chunk, reset, repeated release/drop, both allocation failures and retry", flush=True)

        negatives = [(CASES / "readonly_arena.tk", "E0443"),
                     (CASES / "readonly_field.tk", "AccessCapabilityMismatch"),
                     (ROOT / "tests/semantics/unsafe_raw_construction/readonly_call.tk", "KnownReadOnlyOrFrozenSource"),
                     (ROOT / "tests/semantics/unsafe_raw_construction/nullable_reject.tk", "KnownNullableSourceRequiresGuard"),
                     (ROOT / "tests/semantics/unsafe_raw_construction/pal_conflict.tk", "KnownBorrowConflict")]
        for source, diagnostic in negatives:
            normal = compile(source, "--check-only")
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json")
            assert normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and diagnostic in normal.stderr, normal.stderr + shadow.stderr
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (source.stem + suffix)
                rejected = compile(source, flag, "-o", output)
                assert rejected.returncode == 1 and diagnostic in rejected.stderr and not output.exists(), rejected.stderr
        print("Arena: 4 source parity cases, 3 runtime cases, scoped allocation/failure matrix, 5 rejection pairs and 10 no-artifact checks; no skips")


if __name__ == "__main__":
    main()
