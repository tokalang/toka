#!/usr/bin/env python3
"""Qualify official/router through direct and locked-package build paths."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
import tempfile


PACKAGE = Path(__file__).resolve().parents[1]


class QualificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Toolchain:
    toka: Path
    tokac: Path
    library: Path
    source_root: Path | None


def source_toolchain(root: Path) -> Toolchain:
    root = root.resolve()
    toka = root / "build" / "bin" / "toka"
    tokac = root / "build" / "bin" / "tokac"
    library = root / "lib"
    if not toka.is_file() or not tokac.is_file() or not library.is_dir():
        raise QualificationError(
            "TOKA_ROOT must name a built Toka source checkout with build/bin/toka, "
            "build/bin/tokac, and lib/"
        )
    return Toolchain(toka=toka, tokac=tokac, library=library, source_root=root)


def installed_toolchain() -> Toolchain:
    toka = os.environ.get("TOKA")
    tokac = os.environ.get("TOKAC")
    library = os.environ.get("TOKA_LIB")
    if not toka or not tokac or not library:
        raise QualificationError(
            "standalone qualification requires TOKA, TOKAC, and TOKA_LIB, or TOKA_ROOT"
        )
    toka_path = Path(toka).resolve()
    tokac_path = Path(tokac).resolve()
    library_path = Path(library).resolve()
    if not toka_path.is_file() or not tokac_path.is_file() or not library_path.is_dir():
        raise QualificationError("TOKA, TOKAC, and TOKA_LIB must name usable Toka tools and library")
    return Toolchain(toka=toka_path, tokac=tokac_path, library=library_path, source_root=None)


def resolve_toolchain() -> Toolchain:
    configured_root = os.environ.get("TOKA_ROOT")
    if configured_root:
        return source_toolchain(Path(configured_root))

    monorepo_root = PACKAGE.parents[1]
    try:
        return source_toolchain(monorepo_root)
    except QualificationError:
        return installed_toolchain()


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


def make_sdk(work: Path, toolchain: Toolchain) -> Path:
    if toolchain.source_root is None:
        return toolchain.library
    library = work / "sdk" / "lib"
    shutil.copytree(toolchain.library, library)
    toolchain_dir = library / "toolchain"
    toolchain_dir.mkdir(exist_ok=True)
    shutil.copy2(toolchain.source_root / "tools" / "scripts" / "toka_build.py", toolchain_dir / "toka_build.py")
    return library


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


def main() -> int:
    toolchain = resolve_toolchain()

    with tempfile.TemporaryDirectory(prefix="toka-router-package-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work, toolchain)
        dependency = work / "router"
        shutil.copytree(PACKAGE, dependency)
        environment = dict(os.environ)
        environment.update({"TOKAC": str(toolchain.tokac), "TOKA_LIB": str(sdk)})

        direct = work / "router_v1"
        run([
            str(toolchain.tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
            str(dependency / "tests" / "router_v1.tk"), "-o", str(direct),
        ], cwd=work, env=environment)
        run([str(direct)], cwd=work, env=environment)

        project = work / "consumer"
        write_consumer(project, dependency)
        run([str(toolchain.toka), "fetch"], cwd=project, env=environment)
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"router" not in locked:
            raise QualificationError("Router consumer did not produce a v1 lock with router")
        offline = dict(environment)
        offline["TOKA_OFFLINE"] = "1"
        run([str(toolchain.toka), "fetch"], cwd=project, env=offline)
        if lock.read_bytes() != locked:
            raise QualificationError("offline Router fetch changed package.lock")
        run([str(toolchain.toka), "build"], cwd=project, env=offline)
        run([str(project / "target" / "debug" / "router_consumer")], cwd=project, env=offline)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-router-package-v1",
        "stages": {
            "router_profile_suite": "pass",
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
