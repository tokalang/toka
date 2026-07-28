#!/usr/bin/env python3
"""Qualify official/regex through direct and locked-package build paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "regex"


class QualificationError(RuntimeError):
    pass


def run(argv: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=120,
    )
    if result.returncode != 0:
        raise QualificationError(
            "command failed: %s\nstdout:\n%s\nstderr:\n%s"
            % (" ".join(argv), result.stdout, result.stderr)
        )
    return result


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
        '    name = "regex_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        regex = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"regex_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import official/regex::{Regex}\n\n"
        "fn main() -> i32 {\n"
        '    auto regex = Regex::compile("a(b|c)+d?").unwrap()\n'
        '    if !regex.is_match("ac") { return 1 }\n'
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    if not toka.is_file() or not tokac.is_file():
        raise QualificationError("build toka and tokac before package qualification")

    with tempfile.TemporaryDirectory(prefix="toka-regex-package-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work)
        dependency = work / "regex"
        shutil.copytree(PACKAGE, dependency)
        environment = dict(os.environ)
        environment.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk)})

        direct = work / "regex_v1"
        run([
            str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
            str(dependency / "tests" / "regex_v1.tk"), "-o", str(direct),
        ], cwd=work, env=environment)
        run([str(direct)], cwd=work, env=environment)

        project = work / "consumer"
        write_consumer(project, dependency)
        run([str(toka), "fetch"], cwd=project, env=environment)
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"regex" not in locked:
            raise QualificationError("regex consumer did not produce a v1 lock")
        offline = dict(environment)
        offline["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], cwd=project, env=offline)
        if lock.read_bytes() != locked:
            raise QualificationError("offline regex fetch changed package.lock")
        run([str(toka), "build"], cwd=project, env=offline)
        run([str(project / "target" / "debug" / "regex_consumer")], cwd=project, env=offline)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-regex-package-v1",
        "stages": {
            "regex_profile_suite": "pass",
            "locked_local_dependency": "pass",
            "offline_lock_replay": "pass",
            "public_import_build_run": "pass",
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
