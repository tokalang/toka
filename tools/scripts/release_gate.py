#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


SCHEMA = "toka.release-gate"
SCHEMA_VERSION = 2
ASYNC_FIXTURES = (
    "tests/pass/g09_async_basic.tk",
    "tests/pass/g09_async_suspension_state.tk",
    "tests/pass/g09_async_detached_lifecycle.tk",
    "tests/pass/g10_async_http_server_test.tk",
    "tests/pass/g10_async_io_test.tk",
    "tests/pass/g10_async_net_test.tk",
)
STAGE_NAMES = (
    "build", "pass", "fail", "warn", "semantic_replay",
    "cache_invalidation", "tooling", "incremental",
    "native_build_reference", "qslite", "async", "sanitizer",
    "package_smoke",
)


def native_package_target():
    if sys.platform == "darwin":
        os_name = "macos"
    elif sys.platform.startswith("linux"):
        os_name = "linux"
    else:
        raise RuntimeError("release gate supports native Linux and macOS only")
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        arch = "arm64"
    elif machine in ("x86_64", "amd64"):
        arch = "x64"
    else:
        raise RuntimeError("unsupported release architecture: " + machine)
    return os_name, arch


def strip_ansi(value):
    return re.sub(r"\x1b\[[0-9;]*m", "", value)


def parse_counts(name, output):
    clean = strip_ansi(output)
    counts = {}
    if name == "build":
        detailed = re.search(
            r"(\d+)% tests passed,\s*(\d+) tests failed out of (\d+)",
            clean,
        )
        compact = re.search(
            r"100% tests passed out of (\d+)",
            clean,
        )
        if detailed:
            failed = int(detailed.group(2))
            total = int(detailed.group(3))
        elif compact:
            failed = 0
            total = int(compact.group(1))
        else:
            failed = None
            total = None
        if failed is not None:
            counts["ctest"] = {
                "passed": total - failed,
                "failed": failed,
                "total": total,
            }
    elif name == "pass":
        passed = re.findall(r"Passed:\s*(\d+)", clean)
        failed = re.findall(r"Failed:\s*(\d+)", clean)
        if passed:
            counts["pass_suite"] = {
                "passed": int(passed[-1]),
                "failed": int(failed[-1]) if failed else 0,
            }
        conformance = re.search(
            r"Conformance Suite Results:\s*(\d+) Passed,\s*(\d+) Failed",
            clean,
        )
        if conformance:
            counts["conformance"] = {
                "passed": int(conformance.group(1)),
                "failed": int(conformance.group(2)),
            }
    elif name in ("fail", "warn", "async"):
        passed = re.findall(r"Passed:\s*(\d+)", clean)
        failed = re.findall(r"Failed:\s*(\d+)", clean)
        if passed:
            counts["passed"] = int(passed[-1])
        if failed:
            counts["failed"] = int(failed[-1])
    elif name == "semantic_replay":
        match = re.search(r"Semantic replay cases passed:\s*(\d+).*Semantic replay cases failed:\s*(\d+)", clean, re.S)
        if match:
            counts = {"passed": int(match.group(1)), "failed": int(match.group(2))}
    elif name == "cache_invalidation":
        match = re.search(r"Semantic cache cases passed:\s*(\d+).*Semantic cache cases failed:\s*(\d+)", clean, re.S)
        if match:
            counts = {"passed": int(match.group(1)), "failed": int(match.group(2))}
    elif name == "tooling":
        results = {}
        for line in clean.splitlines():
            try:
                data = json.loads(line)
            except ValueError:
                continue
            schema = data.get("schema")
            if schema:
                results[schema] = data
        check_schemas = (
            "toka.developer-experience",
            "toka.ai-tooling-test",
            "toka.semantic-index-test",
            "toka.lsp-protocol",
        )
        scale = results.get("toka.tooling-scale")
        evaluation = results.get("toka.ai-coding-evaluation")
        if (all(schema in results for schema in check_schemas) and
                scale is not None and evaluation is not None):
            counts = {
                "checks": sum(int(results[schema].get("count", 0))
                              for schema in check_schemas),
                "evaluation_tasks": int(evaluation.get("tasks", 0)),
                "scale_edits": int(scale.get("soak", {}).get("edits", 0)),
                "scale_lines": int(scale.get("fixture", {}).get("lines", 0)),
                "scale_modules": int(scale.get("fixture", {}).get("modules", 0)),
                "suites": 6,
            }
    elif name == "sanitizer":
        for line in reversed(clean.splitlines()):
            try:
                data = json.loads(line)
            except ValueError:
                continue
            if data.get("schema") == "toka.fz3-reliability-audit":
                counts = dict(data.get("counts", {}))
                counts["total"] = sum(data.get("counts", {}).values())
                break
    elif name == "package_smoke":
        for line in reversed(clean.splitlines()):
            try:
                data = json.loads(line)
            except ValueError:
                continue
            if data.get("schema") == "toka.release-package-smoke":
                counts = {"checks": int(data.get("count", 0))}
                break
    elif name == "native_build_reference":
        for line in reversed(clean.splitlines()):
            try:
                data = json.loads(line)
            except ValueError:
                continue
            if data.get("schema") == "toka.native-build-reference":
                counts = {
                    "cycles": int(data.get("cycles", 0)),
                    "modules": int(data.get("module_count", 0)),
                }
                break
    elif name == "qslite":
        reference = None
        toolchain = None
        for line in clean.splitlines():
            try:
                data = json.loads(line)
            except ValueError:
                continue
            if data.get("schema") == "toka.qslite-reference":
                reference = data
            elif data.get("schema") == "toka.qslite-toolchain":
                toolchain = data
        if reference is not None and toolchain is not None:
            counts = {
                "corruption_cases": int(reference.get("corruption_cases", 0)),
                "operations": int(reference.get("operation_count", 0)),
                "toolchain_stages": len(toolchain.get("stages", {})),
            }
    return counts


def run_commands(name, commands, root, log_dir, env):
    output_parts = []
    returncode = 0
    for command in commands:
        result = subprocess.run(
            command,
            cwd=str(root),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        output_parts.append(result.stdout)
        if result.returncode != 0:
            returncode = result.returncode
            break
    output = "".join(output_parts)
    (log_dir / (name + ".log")).write_text(output, encoding="utf-8")
    sys.stdout.write("[%s] %s\n" % ("PASS" if returncode == 0 else "FAIL", name))
    sys.stdout.flush()
    if returncode != 0:
        sys.stdout.write(output)
    return {
        "counts": parse_counts(name, output),
        "exit_code": returncode,
        "name": name,
        "result": "pass" if returncode == 0 else "fail",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--version", default="v1.0.0-rc.8")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--work-dir", default="/tmp/toka-release-gate")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = (root / args.build_dir).resolve()
    work_dir = Path(args.work_dir).resolve()
    if work_dir.exists():
        shutil.rmtree(str(work_dir))
    log_dir = work_dir / "logs"
    log_dir.mkdir(parents=True)
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    os_name, arch = native_package_target()
    expected_target = os_name + "-" + arch
    if args.target != expected_target:
        raise SystemExit("target %s does not match native runner %s" % (args.target, expected_target))

    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=str(root), text=True
    ).strip()
    source_dirty = bool(subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        cwd=str(root), text=True,
    ).strip())
    if source_dirty and not args.allow_dirty:
        report = {
            "result": "fail",
            "revision": revision,
            "schema": SCHEMA,
            "source_dirty": True,
            "stages": [
                {"counts": {}, "exit_code": None, "name": name,
                 "result": "not_run"}
                for name in STAGE_NAMES
            ],
            "target": args.target,
            "version": SCHEMA_VERSION,
            "version_label": args.version,
        }
        output_path.write_text(
            json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        print(json.dumps(report, sort_keys=True, separators=(",", ":")))
        raise SystemExit(1)
    env = os.environ.copy()
    env["TOKAC"] = str(build_dir / "bin" / "tokac")
    env["TOKA"] = str(build_dir / "bin" / "toka")
    env["TOKA_LIB"] = str(root / "lib")
    env["CORES"] = env.get("CORES", str(max(1, os.cpu_count() or 1)))

    asan_dir = work_dir / "asan-build"
    audit_timeout = "30"
    archive = build_dir / ("toka-%s-%s-%s.tar.gz" % (args.version, os_name, arch))
    toka_command = [
        env["TOKAC"], "-I", "lib", "-I", str(build_dir / "generated"),
        "-I", "tools/toka",
        "tools/toka/src/main.tk", "-o", str(build_dir / "bin" / "toka"),
        "-O3",
    ]
    package_tool_commands = (
        [env["TOKAC"], "-I", "lib", "-I", str(build_dir / "generated"), "-I", "tools/tokafmt", "tools/tokafmt/src/main.tk", "-o", str(build_dir / "bin" / "tokafmt"), "-O3"],
        ["cmake", "--build", str(build_dir), "--target", "tokalsp"],
    )
    stages = (
        ("build", (
            ["cmake", "--build", str(build_dir), "--parallel", env["CORES"]],
            ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
            [sys.executable, "tools/scripts/test_pass.py", "--prepare-runtime-only"],
            toka_command,
        )),
        ("pass", (
            [sys.executable, "tools/scripts/test_pass.py",
             "--exclude-file", "spec/ci_quarantined_pass_tests.list"],
            [sys.executable, "tools/run_conformance.py",
             "--build-dir", str(build_dir)],
        )),
        ("fail", ([sys.executable, "tools/scripts/test_verify_fail.py",
                   "--exclude-file", "spec/ci_quarantined_fail_tests.list"],)),
        ("warn", ([sys.executable, "tools/scripts/test_verify_warn.py"],)),
        ("semantic_replay", (
            ["tools/scripts/test_semantic_replay.sh"],
            [sys.executable, "tools/scripts/audit_handle_grammar.py",
             "--quick", "--tokac", env["TOKAC"],
             "--build-dir", str(build_dir)],
            ["bash", "tools/scripts/test_outcome_body_recheck.sh"],
            ["bash", "tools/scripts/test_semantic_manifest_build_profile.sh"],
            ["bash", "tools/scripts/test_semantic_manifest_attestation.sh"],
            ["bash", "tools/scripts/test_semantic_manifest_attestation_build.sh"],
        )),
        ("cache_invalidation", (
            ["tools/scripts/test_tki_unsafe_revalidation.sh"],
            ["tools/scripts/test_tki_cache_validation.sh"],
            ["tools/scripts/test_semantic_cache_invalidation.sh"],
            ["tools/scripts/test_mixed_core_cache.sh"],
        )),
        ("tooling", (
            [sys.executable, "tools/scripts/test_developer_experience.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_release_workflow.py"],
            [sys.executable, "tools/scripts/test_local_release_prequalification.py"],
            [sys.executable, "tools/scripts/test_ai_tooling.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_ai_authoring_friction.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_ai_completion_card.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_json_cli_contract.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_ai_coding_evaluation.py"],
            [sys.executable, "tools/scripts/evaluate_ai_coding.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_semantic_index.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_cede_obligation_evidence.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_taskhandle_lifecycle.py",
             "--build-dir", str(build_dir),
             "--conformance-output", str(build_dir / ("taskhandle-lifecycle-conformance-%s.json" % args.target))],
            [sys.executable, "tools/scripts/test_async_runtime_boundary.py"],
            [sys.executable, "tools/scripts/test_capability_pilot.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_semantic_diff_preview.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_lsp_protocol.py",
             "--build-dir", str(build_dir)],
            [sys.executable, "tools/scripts/test_tooling_scale.py",
             "--build-dir", str(build_dir)],
        )),
        ("incremental", (["tools/scripts/test_incremental_build.sh"],)),
        ("native_build_reference", (
            ["tools/scripts/test_native_build_reference.sh"],
            [sys.executable, "tools/scripts/test_native_build_identity.py"],
            [sys.executable, "tools/scripts/qualify_native_build.py",
             "--cycles", "100",
             "--work-root", str(work_dir / "native-build-work"),
             "--report", str(log_dir / "native-build-reference.json")],
        )),
        ("qslite", (
            [sys.executable, "tools/scripts/qualify_qslite.py",
             "--output", str(log_dir / "qslite-reference.json")],
            [sys.executable, "tools/scripts/qualify_qslite_toolchain.py",
             "--output", str(log_dir / "qslite-toolchain.json")],
        )),
        ("async", ([sys.executable, "tools/scripts/test_pass.py"] + list(ASYNC_FIXTURES),)),
        ("sanitizer", (
            ["cmake", "-S", str(root), "-B", str(asan_dir), "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_CXX_FLAGS=-O1 -g -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer", "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr"],
            ["cmake", "--build", str(asan_dir), "--parallel", env["CORES"]],
            [sys.executable, "tools/scripts/audit_fz3_reliability.py", "--tokac", str(asan_dir / "bin" / "tokac"), "--timeout", audit_timeout],
        )),
        ("package_smoke", package_tool_commands + (
            [sys.executable, "tools/scripts/test_package_manager_supply_chain.py", "--toka", env["TOKA"]],
            ["tools/scripts/package_release.sh", args.version],
            [sys.executable, "tools/scripts/test_release_package.py", str(archive),
             "--version", args.version],
        )),
    )

    report_stages = []
    failed = False
    sanitizer_env = env.copy()
    if sys.platform == "darwin":
        sanitizer_env["ASAN_OPTIONS"] = (
            "detect_leaks=0:detect_container_overflow=0"
        )
    for name, commands in stages:
        if failed:
            report_stages.append({
                "counts": {}, "exit_code": None, "name": name,
                "result": "not_run",
            })
            continue
        stage_env = sanitizer_env if name == "sanitizer" else env
        stage = run_commands(name, commands, root, log_dir, stage_env)
        report_stages.append(stage)
        failed = stage["result"] != "pass"

    report = {
        "result": "fail" if failed else "pass",
        "revision": revision,
        "schema": SCHEMA,
        "source_dirty": source_dirty,
        "stages": report_stages,
        "target": args.target,
        "version": SCHEMA_VERSION,
        "version_label": args.version,
    }
    output_path.write_text(
        json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
