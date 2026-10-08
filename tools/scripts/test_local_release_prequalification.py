#!/usr/bin/env python3

"""Contract checks for the local RC prequalification entry point."""

import argparse
import ast
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tools/scripts/prequalify_release.py"
DOCKERFILE = ROOT / "tools/docker/Dockerfile.release-qualification"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)



def label_rejection_errors(result, output):
    errors = []
    if type(result.returncode) is not int or result.returncode != 1:
        errors.append("expected label rejection exit 1, actual %r" % result.returncode)
    # Match the rejection category, not a specific supported-version list.
    diagnostic = re.fullmatch(
        r"release label (?:must be |rejected: )[^\r\n]*\bcanonical\b[^\r\n]*\btag\b[.]?",
        result.stderr.strip(), re.IGNORECASE)
    if diagnostic is None:
        errors.append("expected a canonical release-label rejection diagnostic, actual %r" % result.stderr)
    if output.exists():
        errors.append("label rejection created output: " + str(output))
    return errors


def check_rejection_controls(root, diagnostics):
    output = root / "controlled-output"
    fixtures = [
        ("current-wording", 1, "release label must be a canonical v0.11.x/v0.12.x/v0.13.x tag\n", False, True),
        ("alternate-wording", 1, "Release label rejected: expected a canonical version tag.\n", False, True),
        ("exit-success", 0, "release label must be a canonical tag\n", False, False),
        ("bool-exit", True, "release label must be a canonical tag\n", False, False),
        ("float-exit", 1.0, "release label must be a canonical tag\n", False, False),
        ("wrong-exit", 2, "release label must be a canonical tag\n", False, False),
        ("signal-exit", -15, "release label must be a canonical tag\n", False, False),
        ("ordinary-error", 1, "permission denied\n", False, False),
        ("missing-helper", 1, "helper not found\n", False, False),
        ("missing-diagnostic", 1, "", False, False),
        ("generic-version-error", 1, "unsupported version\n", False, False),
        ("mixed-failure", 1, "release label must be a canonical tag\nTraceback: helper missing\n", False, False),
        ("created-empty-directory", 1, "release label must be a canonical tag\n", True, False),
        ("created-report", 1, "release label must be a canonical tag\n", True, False),
    ]
    receipts = []
    for name, code, stderr, created, accepted in fixtures:
        if created:
            output.mkdir()
            if name == "created-report":
                (output / "report.json").write_text("{}")
        result = subprocess.CompletedProcess(["independent controlled fixture"], code, "", stderr)
        errors = label_rejection_errors(result, output)
        receipts.append({"name": name, "synthetic_control": True, "exit_code": code,
                         "stderr": stderr, "output_created": created,
                         "expected_accept": accepted, "errors": errors})
        require((not errors) == accepted, "rejection control mismatch: " + json.dumps(receipts[-1]))
        if created:
            for entry in output.iterdir():
                entry.unlink()
            output.rmdir()
    (diagnostics / "rejection-controls.json").write_text(json.dumps(receipts, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--diagnostics-dir", type=Path)
    args = parser.parse_args()
    diagnostics = args.diagnostics_dir or ROOT / "build/prequalification-contract" / uuid.uuid4().hex
    diagnostics.mkdir(parents=True, exist_ok=True)
    sequence = 0

    def run(argv, **options):
        nonlocal sequence
        sequence += 1
        prefix = diagnostics / ("%03d" % sequence)
        try:
            raw_options = dict(options, text=False)
            raw_result = subprocess.run(argv, **raw_options)
            result = subprocess.CompletedProcess(argv, raw_result.returncode,
                raw_result.stdout.decode("utf-8", errors="replace"),
                raw_result.stderr.decode("utf-8", errors="replace"))
        except OSError as error:
            (prefix.with_suffix(".json")).write_text(json.dumps({"argv": argv, "cwd": str(options.get("cwd")),
                "exit_code": None, "termination": "not_started", "error": str(error),
                "stdout_available": False, "stderr_available": False}, indent=2))
            raise
        prefix.with_suffix(".stdout").write_bytes(raw_result.stdout)
        prefix.with_suffix(".stderr").write_bytes(raw_result.stderr)
        prefix.with_suffix(".json").write_text(json.dumps({"argv": argv, "cwd": str(options.get("cwd")),
            "exit_code": result.returncode if result.returncode >= 0 else None,
            "signal": -result.returncode if result.returncode < 0 else None}, indent=2))
        if result.returncode:
            print("Subcommand returned %d; raw receipt: %s" % (result.returncode, prefix), file=sys.stderr)
        return result
    gate = ast.parse((ROOT / "tools/scripts/release_gate.py").read_text(encoding="utf-8"))
    stages = next(node.value for node in ast.walk(gate)
                  if isinstance(node, ast.Assign) and
                  any(isinstance(target, ast.Name) and target.id == "stages" for target in node.targets))
    build_stage = stages.elts[0]
    require(ast.literal_eval(build_stage.elts[0]) == "build", "build must remain the first release stage")
    commands = build_stage.elts[1].elts
    def literals(command):
        return [item.value for item in command.elts if isinstance(item, ast.Constant)]
    prepare = next(i for i, command in enumerate(commands)
                   if "--prepare-runtime-only" in literals(command))
    ctest = next(i for i, command in enumerate(commands) if "ctest" in literals(command))
    require(0 < prepare < ctest, "fresh source runtime objects must be prepared after build and before CTest")

    with tempfile.TemporaryDirectory(prefix="toka-local-prequalification-test-") as temporary:
        output = Path(temporary) / "output"
        command = [
            sys.executable, str(RUNNER), "--dry-run", "--revision", "HEAD",
            "--version", "v0.11.0", "--target", "native",
            "--target", "linux-arm64", "--target", "linux-x64",
            "--output-dir", str(output),
        ]
        result = run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        require(result.returncode == 0, "dry-run failed:\n%s%s" % (result.stdout, result.stderr))
        summary = json.loads((output / "local-release-prequalification-summary.json").read_text(encoding="utf-8"))
        require(summary["schema"] == "toka.local-release-prequalification",
                "summary schema changed")
        require(summary["version"] == 1 and summary["result"] == "planned",
                "dry-run must produce a planned v1 summary")
        require(summary["version_label"] == "v0.11.0",
                "local prequalification used the wrong 0.11.0 label")
        targets = {entry["target"] for entry in summary["targets"]}
        require({"linux-arm64", "linux-x64"}.issubset(targets),
                "dry-run omitted Docker Linux targets")
        require(any(entry["executor"] == "native" for entry in summary["targets"]),
                "dry-run omitted the native gate")
        require("source-linux-arm64" in result.stdout and
                "source-linux-x64" in result.stdout,
                "Docker targets must have isolated source checkouts")
        require(result.stdout.count("git clone --no-checkout --no-local") ==
                len(summary["targets"]),
                "each resolved target must clone an isolated source checkout")
        docker_targets = sum(1 for entry in summary["targets"]
                             if entry["executor"] == "docker")
        require(result.stdout.count("--build-dir /src/build") == docker_targets,
                "Docker gates must use the isolated checkout's standard build directory")

        default_output = Path(temporary) / "default-output"
        default_run = run([
            sys.executable, str(RUNNER), "--dry-run", "--target", "native",
            "--output-dir", str(default_output),
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(default_run.returncode == 0 and json.loads(
            (default_output / "local-release-prequalification-summary.json").read_text(
                encoding="utf-8"))["version_label"] == "v0.11.0",
                "default prequalification version did not move to 0.11.0")

        invalid = run([
            sys.executable, str(RUNNER), "--dry-run", "--docker-cores", "0",
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(invalid.returncode != 0 and "must be positive" in invalid.stderr,
                "invalid Docker parallelism must fail before a prequalification run")
        check_rejection_controls(Path(temporary), diagnostics)
        for label in ("v0.11.01", "v1.0.0-rc.13", "v0.10.0"):
            rejected_output = Path(temporary) / ("rejected-" + label)
            invalid_label = run([
                sys.executable, str(RUNNER), "--dry-run", "--version", label,
                "--output-dir", str(rejected_output),
            ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            errors = label_rejection_errors(invalid_label, rejected_output)
            require(not errors, "label rejection contract failed for %s: %s; receipt %s; inspect prequalify_release.py label validation" %
                    (label, "; ".join(errors), diagnostics / ("%03d" % sequence)))
        for label in ("v0.11.0", "v0.12.0", "v0.13.0"):
            planned_output = Path(temporary) / ("planned-" + label)
            legal_label = run([
                sys.executable, str(RUNNER), "--dry-run", "--version", label,
                "--target", "native", "--output-dir", str(planned_output),
            ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            require(legal_label.returncode == 0, "valid label planning failed: " + label + "; receipt " + str(diagnostics / ("%03d" % sequence)))
            planned = json.loads((planned_output / "local-release-prequalification-summary.json").read_text())
            require(planned["schema"] == "toka.local-release-prequalification" and
                    type(planned["version"]) is int and planned["version"] == 1 and
                    planned["result"] == "planned" and planned["version_label"] == label,
                    "valid-label planned identity changed: " + label)

    text = DOCKERFILE.read_text(encoding="utf-8")
    require("ARG BASE_IMAGE=ubuntu:24.04" in text,
            "Docker qualification image must support the ARM64 runner base")
    require("llvm.sh ${LLVM_VERSION}" in text and "curl" in text and "libssl-dev" in text,
            "Docker qualification image must install the CI LLVM and package prerequisites")
    print("Local release prequalification contract PASSED")


if __name__ == "__main__":
    main()
