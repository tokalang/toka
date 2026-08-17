#!/usr/bin/env python3
"""Collect deterministic P0 qualification evidence without masking service gates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
LOCAL_SUITES = (
    ("agent_service", ROOT / "examples" / "agent-service" / "tests" / "qualify.py"),
)


def run(argv: list[str]) -> dict[str, object]:
    completed = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, timeout=300)
    return {
        "command": argv,
        "status": "pass" if completed.returncode == 0 else "failed",
        "exit_code": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, default=ROOT / "build" / "p0-qualification.json")
    args = parser.parse_args()

    stages: dict[str, object] = {}
    for name, script in LOCAL_SUITES:
        stages[name] = run([sys.executable, str(script)])

    local_passed = all(stage["status"] == "pass" for stage in stages.values())
    result = "pass" if local_passed else "failed"
    report = {
        "schema": "toka.p0-qualification-v1",
        "result": result,
        "stages": stages,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    print(json.dumps({"result": result, "report": str(args.report)},
                     sort_keys=True, separators=(",", ":")))
    return 0 if result == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
