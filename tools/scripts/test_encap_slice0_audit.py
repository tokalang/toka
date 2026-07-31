#!/usr/bin/env python3
"""Reproducible evidence for the non-semantic Slice 0 resolver audit."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"
PACKAGE = ROOT / "lib" / "toolchain" / "toka_package.py"
FIXTURE = ROOT / "tests" / "conformance" / "ownership" / "handle_payload_permission.tk"


def run(*args: str, cwd: Path | None = None) -> str:
    completed = subprocess.run(args, cwd=cwd or ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode:
        raise RuntimeError("command failed:\n$ %s\n%s%s" %
                           (" ".join(args), completed.stdout, completed.stderr))
    return completed.stdout


def manifest(*args: str) -> dict[str, object]:
    return json.loads(run(str(TOKAC), "--dump-dependencies=json", "-I", str(ROOT / "lib"),
                          *args, str(FIXTURE)))


def root_coordinate(document: dict[str, object]) -> dict[str, object]:
    root = document["roots"][0]
    return document["modules"][root]["shadow_coordinate"]


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    direct = root_coordinate(manifest())
    assert direct["status"] == "unknown"
    assert direct["reason"] == "no resolver graph node identity for module"

    workspace = root_coordinate(manifest("--workspace-node", "workspace-test-v1",
                                         "--workspace-root", str(ROOT)))
    assert workspace == {
        "status": "known", "crate_id": "workspace-test-v1",
        "logical_module_path": "tests/conformance/ownership/handle_payload_permission",
        "origin": "workspace", "reason": "",
    }

    with tempfile.TemporaryDirectory() as temporary:
        lock = Path(temporary) / "package.lock"
        lock.write_text(
            "toka-lock-v1\n"
            "package\tunicode\tpath\t/locked/unicode\t/locked/unicode\t-\t"
            + "a" * 64 + "\t-\n", encoding="utf-8")
        first = run(sys.executable, str(PACKAGE), "compiler-node-mappings", "--lock", str(lock))
        second = run(sys.executable, str(PACKAGE), "compiler-node-mappings", "--lock", str(lock))
    assert first == second
    entries = dict(line.split("=", 1) for line in first.splitlines())
    assert entries["unicode"] == entries["official/unicode"]
    assert entries["unicode"].startswith("pkg-v1-")

    print("encap Slice 0 resolver audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, KeyError, IndexError, json.JSONDecodeError) as error:
        print("encap Slice 0 resolver audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
