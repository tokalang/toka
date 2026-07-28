#!/usr/bin/env python3
"""Compile and execute the opt-in native SQLite bridge preflight."""

from __future__ import annotations

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "sqlite"


def run(argv: list[str], *, cwd: Path,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        env=env,
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


def make_sdk(work: Path) -> Path:
    library = work / "sdk" / "lib"
    shutil.copytree(ROOT / "lib", library)
    toolchain = library / "toolchain"
    toolchain.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "tools" / "scripts" / "toka_build.py", toolchain / "toka_build.py")
    return library


def write_consumer(project: Path, dependency: Path) -> None:
    (project / "src").mkdir(parents=True)
    (project / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "sqlite_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        sqlite = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"sqlite_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import official/sqlite::{Database}\n\n"
        "fn main() -> i32 {\n"
        '    auto db# = Database::open(":memory:").unwrap()\n'
        '    if db#.execute("CREATE TABLE t (value INTEGER)").is_err() { return 1 }\n'
        "    if db#.close().is_err() { return 2 }\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    runtime = ROOT / "lib" / "sys" / "toka_rt.o"
    compiler = os.environ.get("CC") or shutil.which("clang")
    if not toka.is_file() or not tokac.is_file() or not runtime.is_file() or compiler is None:
        raise RuntimeError("build toka, tokac, and lib/sys/toka_rt.o before qualifying official/sqlite")

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

        sdk = make_sdk(work)
        dependency = work / "sqlite"
        shutil.copytree(PACKAGE, dependency)
        consumer = work / "consumer"
        write_consumer(consumer, dependency)
        environment = dict(os.environ)
        environment.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk), "TOKA_OFFLINE": "1"})
        run([str(toka), "fetch"], cwd=consumer, env=environment)
        run([str(toka), "build"], cwd=consumer, env=environment)
        program = consumer / "target" / "debug" / "sqlite_consumer"
        if not program.is_file():
            raise RuntimeError("toka build did not produce SQLite consumer")
        run([str(program)], cwd=consumer, env=environment)

    print("official/sqlite qualification: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
