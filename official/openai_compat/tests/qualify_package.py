#!/usr/bin/env python3
"""Qualify official/openai_compat through direct and locked-package paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from dataclasses import dataclass


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


def run(argv: list[str], cwd: Path, env: dict[str, str]) -> None:
    result = subprocess.run(argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    if result.returncode != 0:
        raise QualificationError("command failed: %s\nstdout:\n%s\nstderr:\n%s" %
                                 (" ".join(argv), result.stdout, result.stderr))


def make_sdk(work: Path, toolchain: Toolchain) -> Path:
    if toolchain.source_root is None:
        return toolchain.library
    library = work / "sdk" / "lib"
    shutil.copytree(toolchain.library, library)
    toolchain_dir = library / "toolchain"
    toolchain_dir.mkdir(exist_ok=True)
    shutil.copy2(toolchain.source_root / "tools" / "scripts" / "toka_build.py",
                 toolchain_dir / "toka_build.py")
    return library


def main() -> int:
    toolchain = resolve_toolchain()
    with tempfile.TemporaryDirectory(prefix="toka-openai-compat-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work, toolchain)
        dependency = work / "openai_compat"
        shutil.copytree(PACKAGE, dependency)
        env = dict(os.environ)
        env.update({"TOKAC": str(toolchain.tokac), "TOKA_LIB": str(sdk)})
        direct = work / "openai_compat_v1"
        run([str(toolchain.tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
             str(dependency / "tests" / "openai_compat_v1.tk"), "-o", str(direct)], work, env)
        run([str(direct)], work, env)

        project = work / "consumer"
        (project / "src").mkdir(parents=True)
        (project / "package.tk").write_text(
            "pub const PACKAGE = (\n    name = \"openai_compat_consumer\",\n"
            "    version = \"0.1.0\",\n    dependencies = (\n"
            "        openai_compat = %s,\n    )\n)\n" % json.dumps(str(dependency)), encoding="utf-8")
        (project / "build.tk").write_text(
            "import build::{Executable, run_build}\n\nfn main() -> i32 {\n"
            "    auto app# = Executable::make(c\"openai_compat_consumer\", c\"src/main.tk\")\n"
            "    return run_build(app)\n}\n", encoding="utf-8")
        (project / "src" / "main.tk").write_text(
            "import core/option::{Option}\n"
            "import official/openai_compat::{OpenAiCompatLimits, decode_sse_event}\n"
            "import stdx/net/sse::{SseEvent}\n\n"
            "fn main() -> i32 {\n"
            "    auto input = SseEvent::new(cede string::from(\"message\"), cede string::from(\"[DONE]\"), cede Option<string>::None)\n"
            "    auto events = decode_sse_event(cede input, OpenAiCompatLimits::new(128:usize, 1:usize).unwrap())\n"
            "    if events.is_err() { return 1 }\n"
            "    if events.unwrap().len() != 1:usize { return 2 }\n"
            "    return 0\n}\n", encoding="utf-8")
        run([str(toolchain.toka), "fetch"], project, env)
        lock = (project / "package.lock").read_bytes()
        if not lock.startswith(b"toka-lock-v1\n") or b"openai_compat" not in lock:
            raise RuntimeError("openai_compat consumer did not produce a v1 lock")
        offline = dict(env)
        offline["TOKA_OFFLINE"] = "1"
        run([str(toolchain.toka), "fetch"], project, offline)
        if (project / "package.lock").read_bytes() != lock:
            raise RuntimeError("offline fetch changed package lock")
        run([str(toolchain.toka), "build"], project, offline)
        run([str(project / "target" / "debug" / "openai_compat_consumer")], project, offline)
    print(json.dumps({"result": "pass", "schema": "toka.official-openai-compat-package-v1",
                      "stages": {"semantic_fixture": "pass", "locked_local_dependency": "pass",
                                 "offline_lock_replay": "pass", "public_import_build_run": "pass"},
                      "version": 1}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, QualificationError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error))
        raise SystemExit(1)
