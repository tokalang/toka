#!/usr/bin/env python3
"""Qualify official/compress with native, lock, offline, and import evidence."""

from __future__ import annotations

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "compress"


class QualificationError(RuntimeError):
    pass


def run(argv: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
    )
    if result.returncode != 0:
        raise QualificationError(
            "command failed (%d): %s\nstdout:\n%s\nstderr:\n%s"
            % (result.returncode, " ".join(argv), result.stdout, result.stderr)
        )
    return result


def pkg_config(package: str, mode: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        raise QualificationError("official/compress requires pkg-config for zlib qualification")
    return run([tool, mode, package], cwd=ROOT).stdout.split()


def optional_pkg_libs(package: str) -> list[str]:
    tool = shutil.which("pkg-config")
    if tool is None:
        return []
    result = subprocess.run(
        [tool, "--libs", package], cwd=ROOT, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return result.stdout.split() if result.returncode == 0 else []


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
        '    name = "compress_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        compress = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import official/compress::{Encoder, package_name}\n\n"
        "fn main() -> i32 {\n"
        '    if !package_name().as_str().equals("compress") { return 1 }\n'
        "    auto encoder# = Encoder::gzip(-1:i32).unwrap()\n"
        "    if encoder#.finish().is_err() { return 2 }\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )


def link_program(compiler: str, ir: Path, bridge: Path, output: Path) -> None:
    args = [compiler, str(ir), str(bridge), str(ROOT / "lib" / "sys" / "toka_rt.o"),
            "-o", str(output), *pkg_config("zlib", "--libs")]
    # This is inherited runtime configuration only when the local runtime was
    # built with optional TLS support; it is not an official/compress dep.
    args.extend(optional_pkg_libs("openssl"))
    if platform.system() == "Darwin":
        sdk = run(["xcrun", "--show-sdk-path"], cwd=ROOT).stdout.strip()
        args.extend(["-isysroot", sdk])
    run(args, cwd=ROOT)


def compile_and_run(tokac: Path, sdk: Path, package: Path, source: Path,
                    bridge: Path, output: Path) -> None:
    ir = output.with_suffix(".ll")
    run([str(tokac), "-I", str(sdk), "-I", str(package / "lib"),
         "--emit-llvm", str(source), "-o", str(ir)], cwd=ROOT)
    compiler = os.environ.get("CC") or shutil.which("clang")
    if compiler is None:
        raise QualificationError("official/compress requires clang or CC for native qualification")
    link_program(compiler, ir, bridge, output)
    run([str(output)], cwd=ROOT)


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    runtime = ROOT / "lib" / "sys" / "toka_rt.o"
    compiler = os.environ.get("CC") or shutil.which("clang")
    if not toka.is_file() or not tokac.is_file() or not runtime.is_file() or compiler is None:
        raise QualificationError("build toka, tokac, and lib/sys/toka_rt.o before qualifying official/compress")

    with tempfile.TemporaryDirectory(prefix="toka-compress-package-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work)
        dependency = work / "compress"
        shutil.copytree(PACKAGE, dependency)
        bridge = work / "compress_zlib.o"
        run([compiler, "-Wall", "-Wextra", "-Werror", "-c",
             str(dependency / "native" / "compress_zlib.c"), "-o", str(bridge),
             *pkg_config("zlib", "--cflags")], cwd=ROOT)

        compile_and_run(tokac, sdk, dependency, dependency / "tests" / "compress_v1.tk",
                        bridge, work / "compress_v1")

        project = work / "consumer"
        write_consumer(project, dependency)
        base_env = dict(os.environ)
        base_env.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk)})
        run([str(toka), "fetch"], cwd=project, env=base_env)
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"compress" not in locked:
            raise QualificationError("compress consumer did not produce a v1 lock with compress")

        offline_env = dict(base_env)
        offline_env["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], cwd=project, env=offline_env)
        if lock.read_bytes() != locked:
            raise QualificationError("offline compress fetch changed package.lock")
        compile_and_run(tokac, sdk, dependency, project / "src" / "main.tk",
                        bridge, work / "compress_consumer")

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-compress-package-v1",
        "stages": {
            "native_zlib_bridge": "pass",
            "streaming_boundary_suite": "pass",
            "locked_local_dependency": "pass",
            "offline_lock_replay": "pass",
            "native_public_import_build_run": "pass",
        },
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, QualificationError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error))
        raise SystemExit(1)
