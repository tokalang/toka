#!/usr/bin/env python3
"""Build and run the data-access reference service qualification."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
EXAMPLE = ROOT / "examples" / "data-access-service"


def run(argv: list[str]) -> None:
    result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )


def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    if not tokac.is_file():
        raise RuntimeError("build tokac before qualifying data-access-service")
    include = ["-I", str(ROOT / "lib"),
               "-I", str(ROOT / "official" / "postgres" / "lib"),
               "-I", str(ROOT / "official" / "redis" / "lib"),
               "-I", str(ROOT / "official" / "router" / "lib"),
               "-I", str(EXAMPLE / "lib")]
    with tempfile.TemporaryDirectory(prefix="toka-data-access-service-") as work:
        for name in ("compile_v1", "loopback_v1"):
            executable = Path(work) / name
            run([str(tokac), *include, str(EXAMPLE / "tests" / (name + ".tk")),
                 "-o", str(executable)])
            run([str(executable)])
    print("data-access-service qualification: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
