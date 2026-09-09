#!/usr/bin/env python3
"""Qualify explicit raw P construction, not ownership or initialized extent."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/unsafe_raw_construction"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags):
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)], cwd=ROOT,
                              env=env, capture_output=True, text=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-unsafe-raw-") as directory:
        for source in ("allocated_write.tk", "nullable_guard.tk", "discard.tk", "fallible_allocation.tk", "checked_generic.tk", "no_drop_authority.tk"):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            if source != "discard.tk":
                records = [r for r in json.loads(shadow.stdout)["records"]
                           if r["location"]["file"].endswith(source) and r.get("raw_write_authority")]
                require(records and all(r["raw_write_authority"] == "UnsafeCallerPrecondition" and
                        r["plan"]["drop"] == "NoLiability" and r["plan"]["outcome"] == "Admitted"
                        for r in records), source + ": raw P became a static proof or Drop authority")
            output = Path(directory) / source.removesuffix(".tk")
            built = run(source, "-o", str(output))
            require(built.returncode == 0 and output.exists(), source + ": " + built.stderr)
            executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
            require(executed.returncode == 0, source + ": runtime " + str(executed.returncode) + executed.stderr)
        negatives = {
            "no_unsafe.tk": "UnsafeContextRequired",
            "no_target_write.tk": "AccessCapabilityMismatch",
            "readonly_copy.tk": "KnownReadOnlyOrFrozenSource",
            "readonly_call.tk": "KnownReadOnlyOrFrozenSource",
            "readonly_record.tk": "KnownReadOnlyOrFrozenSource",
            "readonly_array.tk": "KnownReadOnlyOrFrozenSource",
            "readonly_join.tk": "KnownReadOnlyOrFrozenSource",
            "readonly_capture.tk": "KnownReadOnlyOrFrozenSource",
            "frozen_field.tk": "KnownReadOnlyOrFrozenSource",
            "pal_conflict.tk": "KnownBorrowConflict",
            "nullable_reject.tk": "KnownNullableSourceRequiresGuard",
            "null_nonzero.tk": "E0483",
            "ordinary_pointer_upgrade.tk": "ExplicitAddrConstructionRequired",
            "numeric_is_not_addr.tk": "ExplicitAddrConstructionRequired",
        }
        for source, expected in negatives.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and
                    expected in normal.stderr, source + ": " + normal.stderr + shadow.stderr)
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = Path(directory) / (source + suffix)
                rejected = run(source, flag, "-o", str(output))
                require(rejected.returncode == 1 and not output.exists(), source + ": rejected artifact or crash")
        faults = ("missing", "source", "target", "rejected", "incomplete", "authority", "nullable", "rejection")
        for source in ("allocated_write.tk", "discard.tk"):
            for fault in faults:
                for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = Path(directory) / (source + fault + suffix)
                    rejected = run(source, "--unsafe-raw-construction-fault=" + fault, flag, "-o", str(output))
                    require(rejected.returncode == 1 and "E0701" in rejected.stderr and not output.exists(),
                            source + ": fault " + fault + " did not fail closed\n" + rejected.stderr)
    print("unsafe raw construction: 6 runtime/parity, 14 rejection/parity, 28 negative no-artifact, 32 fault no-artifact checks; no skips")


if __name__ == "__main__":
    main()
