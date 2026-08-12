#!/usr/bin/env python3

"""Contract checks for the local RC prequalification entry point."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tools/scripts/prequalify_release.py"
DOCKERFILE = ROOT / "tools/docker/Dockerfile.release-qualification"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    with tempfile.TemporaryDirectory(prefix="toka-local-prequalification-test-") as temporary:
        output = Path(temporary) / "output"
        command = [
            sys.executable, str(RUNNER), "--dry-run", "--revision", "HEAD",
            "--version", "v1.0.0-rc.4", "--target", "native",
            "--target", "linux-arm64", "--target", "linux-x64",
            "--output-dir", str(output),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        require(result.returncode == 0, "dry-run failed:\n%s%s" % (result.stdout, result.stderr))
        summary = json.loads((output / "local-release-prequalification-summary.json").read_text(encoding="utf-8"))
        require(summary["schema"] == "toka.local-release-prequalification",
                "summary schema changed")
        require(summary["version"] == 1 and summary["result"] == "planned",
                "dry-run must produce a planned v1 summary")
        targets = {entry["target"] for entry in summary["targets"]}
        require({"linux-arm64", "linux-x64"}.issubset(targets),
                "dry-run omitted Docker Linux targets")
        require(any(entry["executor"] == "native" for entry in summary["targets"]),
                "dry-run omitted the native gate")

        invalid = subprocess.run([
            sys.executable, str(RUNNER), "--dry-run", "--docker-cores", "0",
        ], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(invalid.returncode != 0 and "must be positive" in invalid.stderr,
                "invalid Docker parallelism must fail before a prequalification run")

    text = DOCKERFILE.read_text(encoding="utf-8")
    require("ARG BASE_IMAGE=ubuntu:24.04" in text,
            "Docker qualification image must support the ARM64 runner base")
    require("llvm.sh ${LLVM_VERSION}" in text and "curl" in text and "libssl-dev" in text,
            "Docker qualification image must install the CI LLVM and package prerequisites")
    print("Local release prequalification contract PASSED")


if __name__ == "__main__":
    main()
