#!/usr/bin/env python3
"""Qualify official/openai_compat through direct and locked-package paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "openai_compat"


def run(argv: list[str], cwd: Path, env: dict[str, str]) -> None:
    result = subprocess.run(argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    if result.returncode != 0:
        raise RuntimeError("command failed: %s\nstdout:\n%s\nstderr:\n%s" %
                           (" ".join(argv), result.stdout, result.stderr))


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    if not toka.is_file() or not tokac.is_file():
        raise RuntimeError("build toka and tokac before qualifying official/openai_compat")
    with tempfile.TemporaryDirectory(prefix="toka-openai-compat-") as temporary:
        work = Path(temporary)
        sdk = work / "sdk" / "lib"
        shutil.copytree(ROOT / "lib", sdk)
        (sdk / "toolchain").mkdir(exist_ok=True)
        shutil.copy2(ROOT / "tools" / "scripts" / "toka_build.py", sdk / "toolchain" / "toka_build.py")
        dependency = work / "openai_compat"
        shutil.copytree(PACKAGE, dependency)
        env = dict(os.environ)
        env.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk)})
        direct = work / "openai_compat_v1"
        run([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
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
            "    auto input = SseEvent(event = string::from(\"message\"), data = string::from(\"[DONE]\"), id = Option<string>::None)\n"
            "    auto events = decode_sse_event(cede input, OpenAiCompatLimits::new(128:usize, 1:usize).unwrap())\n"
            "    if events.is_err() { return 1 }\n"
            "    if events.unwrap().len() != 1:usize { return 2 }\n"
            "    return 0\n}\n", encoding="utf-8")
        run([str(toka), "fetch"], project, env)
        lock = (project / "package.lock").read_bytes()
        if not lock.startswith(b"toka-lock-v1\n") or b"openai_compat" not in lock:
            raise RuntimeError("openai_compat consumer did not produce a v1 lock")
        offline = dict(env)
        offline["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], project, offline)
        if (project / "package.lock").read_bytes() != lock:
            raise RuntimeError("offline fetch changed package lock")
        run([str(toka), "build"], project, offline)
        run([str(project / "target" / "debug" / "openai_compat_consumer")], project, offline)
    print(json.dumps({"result": "pass", "schema": "toka.official-openai-compat-package-v1",
                      "stages": {"semantic_fixture": "pass", "locked_local_dependency": "pass",
                                 "offline_lock_replay": "pass", "public_import_build_run": "pass"},
                      "version": 1}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
