#!/usr/bin/env python3
"""Build the mock-first agent-service vertical slice without credentials."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EXAMPLE = ROOT / "examples" / "agent-service"

def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    if not tokac.is_file():
        raise RuntimeError("build tokac before qualifying agent-service")
    include = ["-I", str(ROOT / "lib"), "-I", str(ROOT / "official" / "postgres" / "lib"),
               "-I", str(ROOT / "official" / "redis" / "lib"), "-I", str(ROOT / "official" / "router" / "lib"),
               "-I", str(ROOT / "official" / "openai_compat" / "lib"),
               "-I", str(EXAMPLE / "lib")]
    with tempfile.TemporaryDirectory(prefix="toka-agent-service-") as work:
        work = Path(work)
        for name in ("compile_v1", "agent_service_v1"):
            executable = work / name
            subprocess.run([str(tokac), *include, str(EXAMPLE / "tests" / f"{name}.tk"),
                            "-o", str(executable)], cwd=ROOT, check=True, timeout=120)
            subprocess.run([str(executable)], cwd=ROOT, check=True, timeout=30)
    print("agent-service qualification: PASSED")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
