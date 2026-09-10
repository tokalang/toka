#!/usr/bin/env python3
"""One unfiltered B1 baseline/candidate integration run; never bless or retry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from compare_thread_sync_baseline import suite

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-source", type=Path, required=True)
    parser.add_argument("--baseline-build", type=Path, required=True)
    parser.add_argument("--candidate-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    targets = {"baseline": (args.baseline_source.resolve(), args.baseline_build.resolve()),
               "candidate": (ROOT, args.candidate_build.resolve())}
    probe = ROOT / "docs/semantic_core/review/b1_view_return_probe.tk"
    metadata = {"probe_sha256": hashlib.sha256(probe.read_bytes()).hexdigest(), "runs": {}}
    for name, (source, build) in targets.items():
        directory = args.output / name
        directory.mkdir()
        env = dict(os.environ, TOKAC=str(build / "bin/tokac"), TOKA_LIB=str(source / "lib"), CORES="4", BLESS="0")
        for key in ("TOKA_USE_LIB_CACHE", "TOKA_CACHED_LIB_ARCHIVE", "TOKA_CACHED_LIB_OBJECTS_FILE", "TOKA_CACHED_LIB_OBJECTS_MAP"):
            env.pop(key, None)
        results = []
        for mode in ("normal", "shadow"):
            command = [str(build / "bin/tokac"), str(probe), "--check-only"]
            if mode == "shadow": command.append("--non-call-transfer-shadow=json")
            result = subprocess.run(command, cwd=source, env=env, capture_output=True, text=True, timeout=60)
            (directory / ("view-" + mode + ".stderr")).write_text(result.stderr)
            (directory / ("view-" + mode + ".stdout")).write_text(result.stdout)
            results.append(result)
        assert results[0].returncode == results[1].returncode == 0 and results[0].stderr == results[1].stderr
        ir = directory / "view.ll"
        lowered = subprocess.run([str(build / "bin/tokac"), str(probe), "--emit-llvm", "-o", str(ir)],
            cwd=source, env=env, capture_output=True, text=True, timeout=60)
        assert lowered.returncode == 0, lowered.stderr
        body = re.search(r"^define %str @bad\(\) \{.*?^}", ir.read_text(), re.M | re.S)
        assert body and "@Encap_string_drop" in body[0] and "ret %str" in body[0]
        assert body[0].index("@Encap_string_drop") < body[0].rindex("ret %str")
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=source)
        metadata["runs"][name] = {"source": str(source), "build": str(build), "revision": revision,
            "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(), "view_accepted": True,
            "view_cleanup_before_return": True, "view_function_ir_sha256": hashlib.sha256(body[0].encode()).hexdigest()}
        # Preserve and compare pre-existing integrations that used to be
        # hidden by earlier assertions in the route scripts.
        provider = directory / "factory_provider.tk"
        provider.write_text("pub fn make() -> fn(cede i32) -> i32 {\n"
                            "return { value => cede value }:fn(cede i32) -> i32\n}\n")
        emitted = subprocess.run([str(build / "bin/tokac"), "-c", "--emit-interface", str(provider),
            "-o", str(provider.with_suffix(".o"))], cwd=source, env=env, capture_output=True, text=True, timeout=60)
        assert emitted.returncode == 0, emitted.stderr
        provider.rename(provider.with_suffix(".source"))
        consumer = directory / "factory_consumer.tk"
        consumer.write_text("import factory_provider::{make}\nfn main() -> i32 {\n"
                            "auto callback = make()\nauto value = 7:i32\nreturn callback(value)\n}\n")
        factory = subprocess.run([str(build / "bin/tokac"), "-I", str(directory), str(consumer), "--check-only"],
            cwd=source, env=env, capture_output=True, text=True, timeout=60)
        (directory / "factory.stderr").write_text(factory.stderr)
        metadata["runs"][name]["hidden_factory"] = {"rc": factory.returncode,
            "environment_unavailable": "CallableReturnEnvironmentUnavailable" in factory.stderr}
        arena = subprocess.run([str(build / "bin/tokac"), "tests/semantics/stage1_return_matrix/arena_raw_handle_return.tk", "--check-only"],
            cwd=source, env=env, capture_output=True, text=True, timeout=60)
        (directory / "arena.stderr").write_text(arena.stderr)
        metadata["runs"][name]["arena"] = {"rc": arena.returncode,
            "capability_mismatch": "AccessCapabilityMismatch" in arena.stderr}
    assert metadata["runs"]["baseline"]["revision"].startswith("72407112")
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    def run_target(name):
        source, build = targets[name]
        env = dict(os.environ, TOKAC=str(build / "bin/tokac"), TOKA_LIB=str(source / "lib"), CORES="4", BLESS="0")
        for key in ("TOKA_USE_LIB_CACHE", "TOKA_CACHED_LIB_ARCHIVE", "TOKA_CACHED_LIB_OBJECTS_FILE", "TOKA_CACHED_LIB_OBJECTS_MAP"):
            env.pop(key, None)
        commands = [("tools-build", ["cmake", "--build", str(build), "--parallel", "4"]),
            ("pass", ["bash", "tools/scripts/test_pass.sh"]),
            ("fail", [sys.executable, "tools/scripts/test_verify_fail.py"]),
            ("ctest", ["ctest", "--test-dir", str(build), "--output-on-failure", "-j", "2"])]
        outcomes = {}
        for label, command in commands:
            print(name + ": start " + label, flush=True)
            start = time.monotonic()
            with (args.output / name / (label + ".log")).open("w") as log:
                result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
            outcomes[label] = {"returncode": result.returncode, "seconds": round(time.monotonic() - start, 2), "command": command}
            print(name + ": finished " + label + " rc=" + str(result.returncode), flush=True)
            if label == "tools-build" and result.returncode:
                raise RuntimeError(name + " tools build failed; suites not started")
        return outcomes
    # The historical PASS runner uses /tmp/tokac_tests independently of cwd.
    # Serialize versions; separate worktrees alone do not isolate artifacts.
    metadata["commands"] = {name: run_target(name) for name in targets}
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    comparison = {}
    for label in ("pass", "fail", "ctest"):
        before = suite(args.output / "baseline", label)
        after = suite(args.output / "candidate", label)
        old, new = set(before["failures"]), set(after["failures"])
        comparison[label] = {"baseline": before, "candidate": after,
            "added": sorted(new - old), "recovered": sorted(old - new), "remaining": sorted(old & new)}
    (args.output / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")
    print(json.dumps({label: {"baseline": comparison[label]["baseline"]["passed"],
        "candidate": comparison[label]["candidate"]["passed"], "added": comparison[label]["added"],
        "recovered": comparison[label]["recovered"]} for label in comparison}, indent=2), flush=True)


if __name__ == "__main__":
    main()
