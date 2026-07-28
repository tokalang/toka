#!/usr/bin/env python3
"""Build and execute the bounded service-kit qualification."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
SQLITE = ROOT / "official" / "sqlite"
SERVICE_KIT = ROOT / "examples" / "service-kit"


def run(argv: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )
    return result


def pkg_config(package: str, mode: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        raise RuntimeError("service-kit qualification requires pkg-config")
    return run([tool, mode, package], cwd=ROOT).stdout.split()


def optional_pkg_libs(package: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        return []
    probe = subprocess.run([tool, "--libs", package], cwd=ROOT, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return probe.stdout.split() if probe.returncode == 0 else []


def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    runtime = ROOT / "lib" / "sys" / "toka_rt.o"
    compiler = os.environ.get("CC") or shutil.which("clang")
    if not tokac.is_file() or not runtime.is_file() or compiler is None:
        raise RuntimeError("build tokac and lib/sys/toka_rt.o before qualifying service-kit")

    with tempfile.TemporaryDirectory(prefix="toka-service-kit-") as temporary:
        work = Path(temporary)
        bridge_object = work / "sqlite_bridge.o"
        run([compiler, "-c", str(SQLITE / "native" / "sqlite_preflight.c"),
             "-o", str(bridge_object), *pkg_config("sqlite3", "--cflags")], cwd=ROOT)
        for source_name in ("dispatcher", "loopback"):
            program_ir = work / (source_name + ".ll")
            program = work / source_name
            run([str(tokac), "-I", str(ROOT / "lib"), "-I", str(SQLITE / "lib"),
                 "-I", str(SERVICE_KIT / "lib"), "--emit-llvm",
                 str(SERVICE_KIT / "tests" / (source_name + ".tk")), "-o", str(program_ir)], cwd=ROOT)
            link_args = [compiler, str(program_ir), str(bridge_object), str(runtime),
                         "-o", str(program), *pkg_config("sqlite3", "--libs")]
            link_args.extend(optional_pkg_libs("openssl"))
            if platform.system() == "Darwin":
                sdk = run(["xcrun", "--show-sdk-path"], cwd=ROOT).stdout.strip()
                link_args.extend(["-isysroot", sdk])
            run(link_args, cwd=ROOT)
            run([str(program)], cwd=ROOT)

    print("service-kit qualification: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
