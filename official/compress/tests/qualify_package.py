#!/usr/bin/env python3
"""Qualify official/compress with native zlib and libzstd, lock, offline, import, and negative linkage evidence."""

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
        raise QualificationError("official/compress requires pkg-config for qualification")
    return run([tool, mode, package], cwd=ROOT).stdout.split()


def verify_zstd_version() -> None:
    tool = shutil.which("pkg-config")
    if tool is None:
        raise QualificationError("official/compress requires pkg-config for qualification")
    res = subprocess.run([tool, "--atleast-version=1.4.0", "libzstd"], cwd=ROOT)
    if res.returncode != 0:
        raise QualificationError("official/compress requires libzstd >= 1.4.0 (pkg-config --atleast-version=1.4.0 libzstd failed)")


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
        "import official/compress::{Encoder, package_name}\n"
        "import official/compress/http::{gzip_response}\n"
        "import stdx/net/http::{HttpResponse}\n\n"
        "fn main() -> i32 {\n"
        '    if !package_name().as_str().equals("compress") { return 1 }\n'
        "    auto encoder# = Encoder::gzip(-1:i32).unwrap()\n"
        "    if encoder#.finish().is_err() { return 2 }\n"
        "    auto zstd_enc# = Encoder::zstd(-1:i32).unwrap()\n"
        "    if zstd_enc#.finish().is_err() { return 3 }\n"
        "    auto response# = HttpResponse::ok(string::from(\"body\"))\n"
        "    if gzip_response(cede response, -1:i32).is_err() { return 4 }\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"compress_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )


def write_plain_http_consumer(project: Path) -> None:
    (project / "src").mkdir(parents=True)
    (project / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "plain_http_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (),\n"
        ")\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import stdx/net/http::{HttpResponse}\n\n"
        "fn main() -> i32 {\n"
        "    auto response = HttpResponse::ok(string::from(\"body\"))\n"
        "    if response.to_string().len() == 0:usize { return 1 }\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"plain_http_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )


def link_program(compiler: str, ir: Path, bridges: list[Path], output: Path) -> None:
    args = [compiler, str(ir), *[str(b) for b in bridges], str(ROOT / "lib" / "sys" / "toka_rt.o"),
            "-o", str(output), *pkg_config("zlib", "--libs"), *pkg_config("libzstd", "--libs")]
    args.extend(optional_pkg_libs("openssl"))
    if platform.system() == "Darwin":
        sdk = run(["xcrun", "--show-sdk-path"], cwd=ROOT).stdout.strip()
        args.extend(["-isysroot", sdk])
    run(args, cwd=ROOT)


def compile_and_run(tokac: Path, sdk: Path, package: Path, source: Path,
                    bridges: list[Path], output: Path) -> None:
    ir = output.with_suffix(".ll")
    run([str(tokac), "-I", str(sdk), "-I", str(package / "lib"),
         "--emit-llvm", str(source), "-o", str(ir)], cwd=ROOT)
    compiler = os.environ.get("CC") or shutil.which("clang")
    if compiler is None:
        raise QualificationError("official/compress requires clang or CC for native qualification")
    link_program(compiler, ir, bridges, output)
    run([str(output)], cwd=ROOT)


def assert_no_zstd_linkage(program: Path) -> None:
    if platform.system() == "Darwin":
        tool = shutil.which("otool")
        if tool:
            res = run([tool, "-L", str(program)], cwd=ROOT)
            if "zstd" in res.stdout.lower():
                raise QualificationError("plain_http_consumer improperly linked libzstd: " + res.stdout)
    else:
        tool = shutil.which("ldd")
        if tool:
            res = run([tool, str(program)], cwd=ROOT)
            if "zstd" in res.stdout.lower():
                raise QualificationError("plain_http_consumer improperly linked libzstd: " + res.stdout)


def main() -> int:
    verify_zstd_version()
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

        bridge_zlib = work / "compress_zlib.o"
        run([compiler, "-Wall", "-Wextra", "-Werror", "-c",
             str(dependency / "native" / "compress_zlib.c"), "-o", str(bridge_zlib),
             *pkg_config("zlib", "--cflags")], cwd=ROOT)

        bridge_zstd = work / "compress_zstd.o"
        run([compiler, "-Wall", "-Wextra", "-Werror", "-c",
             str(dependency / "native" / "compress_zstd.c"), "-o", str(bridge_zstd),
             *pkg_config("libzstd", "--cflags")], cwd=ROOT)

        bridges = [bridge_zlib, bridge_zstd]

        compile_and_run(tokac, sdk, dependency, dependency / "tests" / "compress_v1.tk",
                        bridges, work / "compress_v1")
        compile_and_run(tokac, sdk, dependency, dependency / "tests" / "zstd_v1.tk",
                        bridges, work / "compress_zstd_v1")
        compile_and_run(tokac, sdk, dependency, dependency / "tests" / "http_v1.tk",
                        bridges, work / "compress_http_v1")

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
        run([str(toka), "build"], cwd=project, env=offline_env)
        program = project / "target" / "debug" / "compress_consumer"
        if not program.is_file():
            raise QualificationError("toka build did not produce the native package consumer")
        run([str(program)], cwd=project, env=offline_env)

        plain = work / "plain_http_consumer"
        write_plain_http_consumer(plain)
        no_zlib_pkg_config = work / "no-zlib-pkg-config"
        no_zlib_pkg_config.mkdir()
        plain_env = dict(base_env)
        plain_env["PKG_CONFIG_LIBDIR"] = str(no_zlib_pkg_config)
        run([str(toka), "build"], cwd=plain, env=plain_env)
        plain_program = plain / "target" / "debug" / "plain_http_consumer"
        if not plain_program.is_file():
            raise QualificationError("plain HTTP consumer did not build without zlib/zstd package discovery")
        run([str(plain_program)], cwd=plain, env=plain_env)
        assert_no_zstd_linkage(plain_program)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-compress-package-v1",
        "stages": {
            "native_zlib_bridge": "pass",
            "native_zstd_bridge": "pass",
            "streaming_boundary_suite": "pass",
            "streaming_zstd_suite": "pass",
            "http_content_encoding_policy": "pass",
            "locked_local_dependency": "pass",
            "offline_lock_replay": "pass",
            "native_toka_build_run": "pass",
            "plain_http_consumer_without_zlib_zstd": "pass",
            "negative_linkage_check": "pass",
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
