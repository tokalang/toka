#!/usr/bin/env python3
"""Verify official/router through locked local-package and offline paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "router"


class QualificationError(RuntimeError):
    pass


def run(argv: list[str], *, cwd: Path, env: dict[str, str]) -> None:
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


def write_consumer(project: Path, dependency: Path) -> None:
    (project / "src").mkdir(parents=True)
    (project / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "router_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        router = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"router_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import official/router::{Router}\n"
        "import core/option::{Option}\n"
        "import stdx/net/http::{HttpMethod}\n\n"
        "fn main() -> i32 {\n"
        "    auto router# = Router::new()\n"
        '    if router#.add(HttpMethod::GET, "/items/:id", "items.show").is_err() { return 1 }\n'
        '    match cede router.recognize(HttpMethod::GET, "/items/7?source=lock") {\n'
        "        auto Option::Some('matched) => {\n"
        '            if !\'matched.param("id").unwrap().as_str().equals("7") { return 2 }\n'
        "            return 0\n"
        "        }\n"
        "        _ => return 3\n"
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

    with tempfile.TemporaryDirectory(prefix="toka-router-package-") as temporary:
        work = Path(temporary)
        base_env = dict(os.environ)
        base_env.update({"TOKAC": str(tokac), "TOKA_LIB": str(make_sdk(work))})
        dependency = work / "router"
        shutil.copytree(PACKAGE, dependency)
        project = work / "consumer"
        write_consumer(project, dependency)

        run([str(toka), "fetch"], cwd=project, env=base_env)
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"router" not in locked:
            raise QualificationError("Router consumer did not produce a v1 lock with router")

        offline_env = dict(base_env)
        offline_env["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], cwd=project, env=offline_env)
        if lock.read_bytes() != locked:
            raise QualificationError("offline Router fetch changed package.lock")
        run([str(toka), "build"], cwd=project, env=offline_env)
        run([str(project / "target" / "debug" / "router_consumer")], cwd=project, env=offline_env)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-router-package-v1",
        "stages": {
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
