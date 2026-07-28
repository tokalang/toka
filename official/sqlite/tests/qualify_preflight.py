#!/usr/bin/env python3
"""Compile and execute the opt-in native SQLite bridge preflight."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "sqlite"


def run(argv: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )
    return result


def pkg_config(package: str, mode: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        raise RuntimeError("official/sqlite requires pkg-config for native qualification")
    result = run([tool, mode, package], cwd=ROOT)
    return result.stdout.split()


def optional_pkg_libs(package: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        return []
    probe = subprocess.run(
        [tool, "--libs", package], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if probe.returncode != 0:
        return []
    return probe.stdout.split()


def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    runtime = ROOT / "lib" / "sys" / "toka_rt.o"
    compiler = os.environ.get("CC") or shutil.which("clang")
    if not tokac.is_file() or not runtime.is_file() or compiler is None:
        raise RuntimeError("build tokac and lib/sys/toka_rt.o before qualifying official/sqlite")

    with tempfile.TemporaryDirectory(prefix="toka-sqlite-") as temporary:
        work = Path(temporary)
        bridge_object = work / "sqlite_preflight.o"
        run([compiler, "-c", str(PACKAGE / "native" / "sqlite_preflight.c"),
             "-o", str(bridge_object), *pkg_config("sqlite3", "--cflags")], cwd=ROOT)
        for source_name in ("preflight", "vertical"):
            program_ir = work / (source_name + ".ll")
            program = work / source_name
            run([str(tokac), "-I", str(ROOT / "lib"), "-I", str(PACKAGE / "lib"),
                 "--emit-llvm", str(PACKAGE / "tests" / (source_name + ".tk")),
                 "-o", str(program_ir)], cwd=ROOT)

            link_args = [compiler, str(program_ir), str(bridge_object), str(runtime),
                         "-o", str(program), *pkg_config("sqlite3", "--libs")]
            # A runtime built with optional TLS support still needs its own link
            # dependencies when this package performs a standalone native link.
            # This is inherited runtime configuration, not a SQLite dependency.
            link_args.extend(optional_pkg_libs("openssl"))
            if platform.system() == "Darwin":
                sdk = run(["xcrun", "--show-sdk-path"], cwd=ROOT).stdout.strip()
                link_args.extend(["-isysroot", sdk])
            run(link_args, cwd=ROOT)
            run([str(program)], cwd=ROOT)

    print("official/sqlite native preflight: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
