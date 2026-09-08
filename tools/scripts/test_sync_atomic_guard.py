#!/usr/bin/env python3
"""Scoped sys/sync nullable migration; no permission or allocation API changes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/unsafe_raw_construction"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), *map(str, flags), str(FIXTURES / source)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)

    normal = run("sync_atomic_guard.tk", "--check-only")
    shadow = run("sync_atomic_guard.tk", "--check-only", "--non-call-transfer-shadow=json")
    require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
            normal.stderr + shadow.stderr)
    records = [r for r in json.loads(shadow.stdout)["records"]
               if r["location"]["file"].endswith("sys/sync.tk") and r.get("raw_write_authority")]
    require(records and all(r["raw_write_authority"] == "UnsafeCallerPrecondition" and
                            r["plan"]["drop"] == "NoLiability" for r in records),
            "migration acquired compiler-proven ownership or lost raw authority origin")
    with tempfile.TemporaryDirectory(prefix="toka-sync-atomic-guard-") as directory:
        work = Path(directory)
        executable = work / "atomic"
        built = run("sync_atomic_guard.tk", "-o", executable)
        require(built.returncode == 0, built.stderr)
        require(subprocess.run([str(executable)], timeout=10).returncode == 0,
                "atomic init/load/add/store/destroy failed")

        # Isolate the actual generated helper and inject allocation failure at
        # its single malloc instruction. Never interpose process-wide malloc,
        # edit source/compiler, or manufacture an invalid runtime pointer.
        ir = work / "failure.ll"
        emitted = run("sync_atomic_alloc_failure.tk", "--emit-llvm", "-o", ir)
        require(emitted.returncode == 0, emitted.stderr)
        text = ir.read_text()
        matches = list(re.finditer(r'^define [^\n]*\bi(32|64) @([^ (]*sys_atomic_init_i32)'
                                  r'\(i32 %val\) \{\n.*?^\}', text, re.M | re.S))
        require(len(matches) == 1, "expected one concrete emitted atomic initializer")
        function = matches[0]
        body = function.group(0)
        require("icmp eq ptr" in body and "ret i" in body and "store i32" in body,
                "generated nullable branch/store is missing")
        body, count = re.subn(r'call ptr @malloc\(i(32|64) 4\)',
                             lambda m: "inttoptr i" + m.group(1) + " 0 to ptr", body)
        require(count == 1, "allocation failure must replace exactly one allocator call")
        target = "\n".join(re.findall(r'^target (?:datalayout|triple) = .*$', text, re.M))
        isolated = work / "isolated-null.ll"
        isolated.write_text(target + "\n" + body + "\n")
        caller = work / "caller.c"
        caller.write_text("#include <stdint.h>\nextern intptr_t " + function.group(2) +
                          "(int32_t);\nint main(void) { return " + function.group(2) +
                          "(7) == 0 ? 0 : 1; }\n")
        null_executable = work / "null-allocation"
        linked = subprocess.run([os.environ.get("CC", "clang"), str(isolated), str(caller),
                                 "-o", str(null_executable)], capture_output=True, text=True, timeout=30)
        require(linked.returncode == 0, linked.stderr)
        require(subprocess.run([str(null_executable)], timeout=10).returncode == 0,
                "actual generated initializer did not return zero on allocation failure")

        for source, diagnostic in (("nullable_reject.tk", "KnownNullableSourceRequiresGuard"),
                                   ("readonly_copy.tk", "KnownReadOnlyOrFrozenSource"),
                                   ("pal_conflict.tk", "KnownBorrowConflict")):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (source + suffix)
                rejected = run(source, mode, "-o", output)
                require(rejected.returncode == 1 and diagnostic in rejected.stderr and not output.exists(),
                        "existing raw guard was weakened: " + rejected.stderr)
    print("sync atomic guard: normal/shadow evidence + atomic runtime + isolated generated-IR NULL allocation + 6 rejection no-artifact checks; no skips")


if __name__ == "__main__":
    main()
