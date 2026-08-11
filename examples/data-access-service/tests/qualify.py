#!/usr/bin/env python3
"""Build and run the data-access reference service qualification."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
EXAMPLE = ROOT / "examples" / "data-access-service"
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from registry_fixture import materialize_locked_library


def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )
    return result


def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    if not tokac.is_file():
        raise RuntimeError("build tokac before qualifying data-access-service")
    with tempfile.TemporaryDirectory(prefix="toka-data-access-service-") as work:
        work = Path(work)
        router_library = materialize_locked_library(
            ROOT, "registry_router_consumer", "router", work)
        include = ["-I", str(ROOT / "lib"),
                   "-I", str(ROOT / "official" / "postgres" / "lib"),
                   "-I", str(ROOT / "official" / "redis" / "lib"),
                   "-I", str(router_library), "-I", str(EXAMPLE / "lib")]
        for name in ("compile_v1", "loopback_v1"):
            executable = work / name
            run([str(tokac), *include, str(EXAMPLE / "tests" / (name + ".tk")),
                 "-o", str(executable)])
            result = run([str(executable)])
            if name == "loopback_v1":
                for event in (
                    '"request_id":"data-access-1","route":"health","source":"local","status":"200"',
                    '"request_id":"missing-404","route":"unmatched","source":"router","status":"404"',
                    '"request_id":"note-database","route":"note.show","source":"database","status":"200"',
                    '"request_id":"note-cache","route":"note.show","source":"cache","status":"200"',
                ):
                    if event not in result.stdout:
                        raise RuntimeError("missing request-correlated JSON log event: " + event)
    print("data-access-service qualification: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
