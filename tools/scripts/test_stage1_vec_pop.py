#!/usr/bin/env python3
"""Vec::pop responsibility-handoff gate. Known runtime failures remain failures."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_vec_pop"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    results = []

    def record(name, passed, **details):
        results.append(dict(name=name, passed=passed, **details))

    def run(source, *flags):
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)],
                              cwd=ROOT, env=env, text=True, capture_output=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-vec-pop-gate-") as directory:
        for source in ("copy_pop.tk", "owned_pop.tk", "plain_noncopy_pop.tk", "handle_pop.tk", "owning_string_pop.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            record(source + "/parity", normal.returncode == shadow.returncode == 0 and
                   normal.stderr == shadow.stderr, normal_rc=normal.returncode,
                   shadow_rc=shadow.returncode, stderr=normal.stderr)
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            if built.returncode != 0 or not output.is_file():
                record(source + "/runtime", False, compile_rc=built.returncode, stderr=built.stderr)
            else:
                executed = subprocess.run([str(output)], cwd=directory, text=True,
                                          capture_output=True, timeout=15)
                record(source + "/runtime", executed.returncode == 0,
                       runtime_rc=executed.returncode, expected_rc=0, stderr=executed.stderr)

        for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = Path(directory) / ("owning_string" + suffix)
            generated = run("owning_string_pop.tk", mode, "-o", str(output))
            record("owning_string_pop.tk" + suffix + "/artifact", generated.returncode == 0 and output.is_file(),
                   returncode=generated.returncode)

        for source in ("unproven_borrowed.tk",):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            correct = (normal.returncode == shadow.returncode == 1 and
                       normal.stderr == shadow.stderr and "E04662" in normal.stderr and
                       "ElementDependenciesUnproven" in normal.stderr)
            record(source + "/rejection", correct, stderr=normal.stderr)
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = Path(directory) / (source + suffix)
                rejected = run(source, mode, "-o", str(output))
                record(source + suffix + "/no-artifact", rejected.returncode == 1 and not output.exists(),
                       returncode=rejected.returncode)

        ir = Path(directory) / "owned.ll"
        compiled = run("owned_pop.tk", "--emit-llvm", "-o", str(ir))
        valid_interval = False
        if compiled.returncode == 0 and ir.is_file():
            functions = re.findall(r"^define .*?^}", ir.read_text(), re.M | re.S)
            bodies = [f for f in functions if "raw.take.value = load" in f]
            valid_interval = len(bodies) == 1
            for body in bodies:
                lines = body.splitlines()
                take = next(i for i, line in enumerate(lines) if "raw.take.value = load" in line)
                base = next(i for i, line in enumerate(lines) if "raw.take.base = load" in line)
                length = max(i for i in range(base) if re.search(r"store i64 .*ptr %ptr.peel_soul", lines[i]))
                tail = max(i for i in range(length) if re.search(r"store i64 .*ptr %tail", lines[i]))
                prefix = "\n".join(lines[:tail])
                interval = "\n".join(lines[length + 1:take + 1])
                valid_interval &= (tail < length < base < take and
                                   bool(re.search(r"icmp eq i64 .* 0", prefix)) and
                                   bool(re.search(r"icmp eq ptr .* null", prefix)) and
                                   not re.search(r"\b(call|invoke|callbr)\b|llvm\.coro", interval))
        record("check/prepare -> length store -> non-unwinding take", valid_interval)

        # These controls deliberately require success. Their current failure
        # cannot be blessed as an unsafe precondition violation or hidden skip.
        for source in ("option_handle_cleanup_baseline.tk", "option_shared_cleanup_baseline.tk"):
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            runtime_rc = None
            if built.returncode == 0 and output.is_file():
                runtime_rc = subprocess.run([str(output)], cwd=directory, timeout=15).returncode
            record(source + "/independent-control", built.returncode == 0 and runtime_rc == 0,
                   compile_rc=built.returncode, runtime_rc=runtime_rc, expected_rc=0,
                   stderr=built.stderr)

    passed = sum(r["passed"] for r in results)
    print(json.dumps(dict(passed=passed, total=len(results), skipped=0, results=results), indent=2))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
