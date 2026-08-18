#!/usr/bin/env python3

"""Fail-closed evidence gate for the bounded direct-task cancellation profile."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
PROFILE_PATH = ROOT / "spec/restricted_cancellation_profile.v1.json"
REQUIRED_KEYS = {
    "schema", "version", "status", "purpose", "supported_scope", "guarantees",
    "source_evidence", "native_evidence", "ecosystem_evidence",
    "explicitly_deferred", "graduation_requirements",
}
EXPECTED_GUARANTEES = {
    "RCP-G1", "RCP-G2", "RCP-G3", "RCP-G4", "RCP-G5",
    "RCP-G6a-Timer", "RCP-G6a-TCP", "RCP-G7a",
}
DEFERRED_MARKERS = {"TaskScope", "race2", "TLS", "dual-source", "Context deadline"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def file_sha256(path: Path) -> str:
    if not path.is_file():
        return ""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha256(document):
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def compute_worktree_digest(profile, build_path):
    hasher = hashlib.sha256()
    # Hash profile
    hasher.update(canonical_sha256(profile).encode("utf-8"))

    # Hash critical runtime files
    rt_c = (ROOT / "lib/sys/toka_rt.c").resolve()
    if rt_c.is_file():
        hasher.update(rt_c.read_bytes())

    # Hash all source evidence files
    for item in profile.get("source_evidence", []):
        src = (ROOT / item["path"]).resolve()
        if src.is_file():
            hasher.update(src.read_bytes())

    # Hash all native evidence files
    for item in profile.get("native_evidence", []):
        src = (ROOT / item["path"]).resolve()
        if src.is_file():
            hasher.update(src.read_bytes())

    # Hash git diff if available
    try:
        diff_out = subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)
        hasher.update(diff_out)
    except Exception:
        pass

    return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--no-build", action="store_true", help="Skip automatic cmake build")
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--conformance-output", type=Path)
    args = parser.parse_args()

    build_path = ROOT / args.build_dir
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = build_path / "bin" / ("tokac" + suffix)

    # Step 0: Guarantee reproducible fresh build unless explicitly skipped
    if not args.no_build:
        build_res = subprocess.run(["cmake", "--build", str(build_path)], cwd=ROOT,
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(build_res.returncode == 0, "CMake build failed:\n%s%s" % (build_res.stdout, build_res.stderr))

    require(tokac.is_file(), f"tokac is missing at {tokac}; run 'cmake --build {args.build_dir}' first")

    profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
    require(set(profile) == REQUIRED_KEYS, "restricted cancellation profile fields changed")
    require(profile["schema"] == "toka.restricted-cancellation-profile" and profile["version"] == 1,
            "restricted cancellation profile identity changed")
    require(profile["status"] == "candidate", "profile must remain candidate until full qualification graduation")
    guarantee_ids = {item["id"] for item in profile["guarantees"]}
    require(guarantee_ids == EXPECTED_GUARANTEES,
            f"restricted cancellation guarantee IDs changed: got {guarantee_ids} vs expected {EXPECTED_GUARANTEES}")
    deferred = "\n".join(profile["explicitly_deferred"])
    require(all(marker in deferred for marker in DEFERRED_MARKERS),
            "profile no longer explicitly defers a high-risk cancellation feature")

    evidence_records = []

    with tempfile.TemporaryDirectory(prefix="toka-restricted-cancel-") as temp:
        temp_dir = Path(temp)
        for index, evidence in enumerate(profile["source_evidence"]):
            require(set(evidence) == {"path", "guarantee_ids"}, "source evidence record changed")
            require(set(evidence["guarantee_ids"]).issubset(guarantee_ids), "unknown source guarantee")
            source = ROOT / evidence["path"]
            require(source.is_file(), "source evidence is missing: %s" % source)
            executable = temp_dir / ("source-%d" % index + suffix)
            compiled = subprocess.run([str(tokac), str(source), "-o", str(executable)], cwd=ROOT,
                                     text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            require(compiled.returncode == 0, "source evidence failed to compile: %s\n%s%s" %
                    (source, compiled.stdout, compiled.stderr))
            ran = subprocess.run([str(executable)], cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, timeout=args.timeout)
            require(ran.returncode == 0, "source evidence failed: %s\n%s%s" %
                    (source, ran.stdout, ran.stderr))
            res_str = "pass"
            if "UNSUPPORTED_ENVIRONMENT" in ran.stdout:
                res_str = "skipped (unsupported environment: TCP bind prohibited)"
            evidence_records.append({
                "kind": "source",
                "path": evidence["path"],
                "guarantee_ids": evidence["guarantee_ids"],
                "result": res_str,
            })

    for evidence in profile["native_evidence"]:
        require(set(evidence) == {"target", "path", "guarantee_ids"}, "native evidence record changed")
        require(set(evidence["guarantee_ids"]).issubset(guarantee_ids), "unknown native guarantee")
        require((ROOT / evidence["path"]).is_file(), "native evidence is missing: " + evidence["path"])
        result = subprocess.run(["ctest", "--test-dir", str(build_path), "--output-on-failure",
                                 "-R", "^%s$" % evidence["target"]], cwd=ROOT, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(result.returncode == 0, "native evidence failed: %s\n%s%s" %
                (evidence["target"], result.stdout, result.stderr))
        evidence_records.append({
            "kind": "native",
            "target": evidence["target"],
            "path": evidence["path"],
            "guarantee_ids": evidence["guarantee_ids"],
            "result": "pass",
        })

    for evidence in profile["ecosystem_evidence"]:
        require(set(evidence) == {"path", "statement"} and evidence["statement"],
                "ecosystem evidence record changed")
        eco_path = (ROOT / evidence["path"]).resolve()
        require(eco_path.is_file(), "ecosystem evidence file is missing: " + str(eco_path))
        evidence_records.append({
            "kind": "ecosystem",
            "path": evidence["path"],
            "statement": evidence["statement"],
            "result": "not-run (ecosystem reference)",
        })

    base_rev = "unknown"
    try:
        base_rev = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception:
        pass

    is_dirty = False
    try:
        status_out = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()
        is_dirty = len(status_out) > 0
    except Exception:
        pass

    worktree_digest = compute_worktree_digest(profile, build_path)
    tokac_digest = file_sha256(tokac)
    rt_obj_path = build_path / "lib" / "sys" / ("toka_rt.o" if sys.platform != "win32" else "toka_rt.obj")
    rt_obj_digest = file_sha256(rt_obj_path)

    compiler_version = "unknown"
    try:
        ver_out = subprocess.check_output([str(tokac), "--version"], cwd=ROOT, text=True).strip()
        compiler_version = ver_out.splitlines()[0] if ver_out else "unknown"
    except Exception:
        pass

    any_skipped = any("skipped" in rec["result"] for rec in evidence_records)
    overall_result = "incomplete (environment lacks TCP network-bind capability: RCP-G6a-TCP / RCP-G7a unverified)" if any_skipped else "candidate-pass"
    if args.conformance_output:
        output = {
            "schema": "toka.restricted-cancellation-profile-conformance",
            "version": 1,
            "base_revision": base_rev,
            "is_dirty": is_dirty,
            "worktree_digest": worktree_digest,
            "compiler": {
                "version": compiler_version,
                "tokac_sha256": tokac_digest,
                "runtime_object_sha256": rt_obj_digest,
            },
            "profile": {
                "schema": profile["schema"],
                "version": profile["version"],
                "path": "spec/restricted_cancellation_profile.v1.json",
                "canonical_sha256": canonical_sha256(profile),
            },
            "evidence": evidence_records,
            "result": overall_result,
        }
        args.conformance_output.resolve().write_text(
            json.dumps(output, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    if any_skipped:
        print("Restricted Cancellation Profile v1 candidate gate INCOMPLETE (environment lacks TCP network-bind capability: RCP-G6a-TCP / RCP-G7a unverified)")
        sys.exit(1)
    else:
        print("Restricted Cancellation Profile v1 candidate gate PASSED")


if __name__ == "__main__":
    main()
