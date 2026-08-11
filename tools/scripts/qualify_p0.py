#!/usr/bin/env python3
"""Collect deterministic P0 qualification evidence without masking service gates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
LOCAL_SUITES = (
    ("agent_service", ROOT / "examples" / "agent-service" / "tests" / "qualify.py"),
    # Migrated packages qualify in their canonical repositories.
    ("compress", ROOT / "official" / "compress" / "tests" / "qualify_package.py"),
    ("postgres", ROOT / "official" / "postgres" / "tests" / "qualify_package.py"),
    ("redis", ROOT / "official" / "redis" / "tests" / "qualify_package.py"),
    ("unicode", ROOT / "official" / "unicode" / "tests" / "qualify_package.py"),
    ("openai_compat", ROOT / "official" / "openai_compat" / "tests" / "qualify_package.py"),
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
    parser.add_argument("--real-services", action="store_true",
                        help="run the Docker PostgreSQL/Redis compatibility matrix")
    parser.add_argument("--report", type=Path, default=ROOT / "build" / "p0-qualification.json")
    args = parser.parse_args()

    stages: dict[str, object] = {}
    for name, script in LOCAL_SUITES:
        stages[name] = run([sys.executable, str(script)])

    release_gate = "not-run"
    if args.real_services:
        with tempfile.TemporaryDirectory(prefix="toka-p0-real-services-") as temporary:
            service_report = Path(temporary) / "data-access-real-service.json"
            result = run([sys.executable, str(ROOT / "tools" / "scripts" / "qualify_data_access_real.py"),
                          "--tokac", str(ROOT / "build" / "bin" / "tokac"),
                          "--report", str(service_report)])
            if service_report.is_file():
                result["evidence"] = json.loads(service_report.read_text(encoding="utf-8"))
            stages["data_access_real_service"] = result
            release_gate = "pass" if result["status"] == "pass" else "not-run" if result["exit_code"] == 2 else "failed"

    local_passed = all(stage["status"] == "pass" for name, stage in stages.items()
                       if name != "data_access_real_service")
    result = "pass" if local_passed and release_gate == "pass" else "local-pass" if local_passed else "failed"
    report = {
        "schema": "toka.p0-qualification-v1",
        "result": result,
        "release_gate": release_gate,
        "stages": stages,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    print(json.dumps({"result": result, "release_gate": release_gate, "report": str(args.report)},
                     sort_keys=True, separators=(",", ":")))
    return 0 if result in ("pass", "local-pass") else 1


if __name__ == "__main__":
    raise SystemExit(main())
