#!/usr/bin/env python3
"""Verify official/redis through the locked local-package and offline paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "redis"


class QualificationError(RuntimeError):
    pass


def run(argv: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
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
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )
    return result


def write_consumer(project: Path, dependency: Path) -> None:
    (project / "src").mkdir(parents=True)
    (project / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "redis_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        redis = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"redis_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import official/redis::{RedisCommand, RedisDecode, decode_one}\n"
        "import std/vec::{Vec}\n\n"
        "fn main() -> i32 {\n"
        '    auto command = RedisCommand::new("PING")\n'
        "    if command.into_wire().is_err() { return 1 }\n"
        "    auto frame# = Vec<u8>::new()\n"
        "    frame#.push('+' as u8)\n"
        "    frame#.push('O' as u8)\n"
        "    frame#.push('K' as u8)\n"
        "    frame#.push('\\r' as u8)\n"
        "    frame#.push('\\n' as u8)\n"
        "    match decode_one(frame).unwrap() {\n"
        "        auto RedisDecode::Complete(_) => return 0\n"
        "        _ => return 2\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )


def make_sdk(work: Path) -> Path:
    library = work / "sdk" / "lib"
    shutil.copytree(ROOT / "lib", library)
    toolchain = library / "toolchain"
    toolchain.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "tools" / "scripts" / "toka_build.py", toolchain / "toka_build.py")
    return library


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    if not toka.is_file() or not tokac.is_file():
        raise QualificationError("build toka and tokac before package qualification")

    with tempfile.TemporaryDirectory(prefix="toka-redis-package-") as temporary:
        work = Path(temporary)
        base_env = dict(os.environ)
        base_env.update({"TOKAC": str(tokac), "TOKA_LIB": str(make_sdk(work))})
        test_env = dict(os.environ)
        test_env.pop("TOKA_LIB", None)
        test_env.pop("TOKAC", None)
        include = ["-I", str(ROOT / "lib"), "-I", str(PACKAGE / "lib")]
        # The native TLS runtime is linked only for executables emitted
        # directly under /tmp by the current toolchain, so keep these short-
        # lived qualification binaries there rather than in a child folder.
        for name in ("codec_v1", "client_v1", "transport_v2", "pool_v1"):
            executable = Path("/tmp") / f"toka-redis-qualify-{os.getpid()}-{name}"
            run([str(tokac), *include, str(PACKAGE / "tests" / f"{name}.tk"),
                 "-o", str(executable)], cwd=ROOT, env=test_env)
            run([str(executable)], cwd=ROOT, env=test_env)
        dependency = work / "redis"
        shutil.copytree(PACKAGE, dependency)
        project = work / "consumer"
        write_consumer(project, dependency)

        run([str(toka), "fetch"], cwd=project, env=base_env)
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"redis" not in locked:
            raise QualificationError("Redis consumer did not produce a v1 lock with redis")

        offline_env = dict(base_env)
        offline_env["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], cwd=project, env=offline_env)
        if lock.read_bytes() != locked:
            raise QualificationError("offline Redis fetch changed package.lock")
        run([str(toka), "build"], cwd=project, env=offline_env)
        run([str(project / "target" / "debug" / "redis_consumer")], cwd=project, env=offline_env)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-redis-package-v1",
        "stages": {
            "locked_local_dependency": "pass",
            "offline_lock_replay": "pass",
            "public_import_build_run": "pass",
            "resp2_codec": "pass",
            "serial_client": "pass",
            "verified_tls_and_pipeline": "pass",
            "bounded_connection_pool": "pass",
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
