#!/usr/bin/env python3
"""Directed raw_take frontend, executable cleanup and fail-closed lowering gates."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/raw_take"


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
        return subprocess.run([str(compiler), *flags, str(FIXTURES / source)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)

    rejected = {
        "reject_without_unsafe.tk": "UnsafeContextRequired",
        "reject_managed.tk": "RawStorageRequired",
        "reject_not_index.tk": "RawStorageIndexRequired",
        "reject_handle_selector.tk": "RawStorageIndexRequired",
        "reject_nullable.tk": "NonNullStorageRequired",
        "reject_reference_element.tk": "ElementDependenciesUnproven",
        "reject_old_cede.tk": "E04661",
        "reject_unqualified_shared_generic_parameter.tk": "UnqualifiedSharedGenericParameter",
        "reject_shared_amplification.tk": "AccessCapabilityMismatch",
        "reject_managed_raw_alias.tk": "RawStorageOriginUnprovenOrManaged",
        "reject_active_borrow.tk": "ActiveBorrowConflict",
    }
    with tempfile.TemporaryDirectory(prefix="toka-raw-take-frontend-") as directory:
        for source, reason in rejected.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == 1 and reason in normal.stderr,
                    source + ": " + normal.stderr)
            require((normal.returncode, normal.stderr) == (shadow.returncode, shadow.stderr),
                    source + ": normal/shadow parity")
            for flags, extension in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                output = Path(directory) / (source + extension)
                result = run(source, *flags, "-o", str(output))
                require(result.returncode == 1 and not output.exists(),
                        source + ": rejected source emitted artifact or crashed")

        for source, production, liability in (("accepted_copy.tk", "CopyValue", False),
                                               ("accepted_owned.tk", "MoveOwned", True)):
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            records = [r for r in json.loads(shadow.stdout)["records"]
                       if r["location"]["file"].endswith(source) and "raw_take" in r]
            require(len(records) == 1, source + ": expected one primitive-result edge")
            record = records[0]
            evidence = record["raw_take"]
            require(evidence["initialization_basis"] == "UnsafeCallerPrecondition" and
                    evidence["slot_retirement"] == "UnsafeCallerPostcondition" and
                    evidence["remainder"] == "CallerMaintained" and
                    evidence["value_production"] == production and
                    evidence["carries_drop_liability"] == liability,
                    source + ": wrong unsafe/production/liability facts")
            require(evidence["source_slot"] and evidence["edge"] and
                    "/private/" not in evidence["edge"] and
                    record["source_category"] == "NoSourcePlace" and
                    record["plan"]["exact_path"] == "" and
                    record["plan"]["outcome"] == "Admitted",
                    source + ": raw source and independent result are conflated")
            for flags, extension in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                output = Path(directory) / (source + extension)
                result = run(source, *flags, "-o", str(output))
                require(result.returncode == 0 and output.is_file(), source + ": " + result.stderr)

        runtime_sources = ("accepted_copy.tk", "accepted_owned.tk", "cleanup_paths.tk",
                           "handle_storage_helpers.tk", "index_once_stride.tk", "nullable_guard.tk")
        for source in runtime_sources:
            output = Path(directory) / source.removesuffix(".tk")
            result = run(source, "-o", str(output))
            require(result.returncode == 0 and output.is_file(), source + ": " + result.stderr)
            executed = subprocess.run([str(output)], cwd=directory, capture_output=True,
                                      text=True, timeout=15)
            require(executed.returncode == 0,
                    source + ": runtime returned " + str(executed.returncode) + executed.stderr)

        ir = Path(directory) / "stride.ll"
        proof = run("index_once_stride.tk", "--check-only", "--non-call-transfer-shadow=json")
        require(proof.returncode == 0, proof.stderr)
        plain = [r["raw_take"] for r in json.loads(proof.stdout)["records"] if "raw_take" in r]
        require(len(plain) == 1 and plain[0]["value_production"] == "MoveOwned" and
                not plain[0]["carries_drop_liability"] and "[?]" in plain[0]["source_slot"],
                "NonCopy/no-Drop raw element or dynamic source identity lost")
        result = run("index_once_stride.tk", "--emit-llvm", "-o", str(ir))
        require(result.returncode == 0, result.stderr)
        lines = ir.read_text().splitlines()
        base = next(i for i, line in enumerate(lines) if '"raw.take.base" = load' in line or
                    "raw.take.base = load" in line)
        call = next(i for i in range(base + 1, len(lines)) if re.search(r"call .*@.*next_index", lines[i]))
        gep = next(i for i in range(call + 1, len(lines)) if "raw.take.slot" in lines[i] and
                   "getelementptr" in lines[i])
        require(base < call < gep and "getelementptr i8," not in lines[gep],
                "raw_take base/index order or strict stride violated")

        faults = ("missing", "rejected", "mismatch", "incomplete", "production-none",
                  "production-identity", "production-borrow", "production-temporary", "production-unknown",
                  "copy-proof", "drop", "storage-type", "index-type")
        for source in ("accepted_copy.tk", "accepted_owned.tk"):
            for fault in faults:
                for flags, extension in ((["-c"], ".o"), (["--emit-llvm"], ".ll")):
                    output = Path(directory) / (source + fault + extension)
                    result = run(source, *flags, "--raw-take-fault=" + fault, "-o", str(output))
                    require(result.returncode == 1 and "E0701" in result.stderr and not output.exists(),
                            source + "/" + fault + ": faulty plan emitted artifact or crashed: " + result.stderr)
    print("raw_take: 11 rejections, 6 runtime cases, strict stride/order, 52 fault/no-artifact checks")


if __name__ == "__main__":
    main()
