#!/usr/bin/env python3
"""Qualify the macOS native window/Metal vertical slice through `toka build`."""

from __future__ import annotations

import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "gui"


def run(argv: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    result = subprocess.run(argv, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(
            "command failed (%s): %s\nstdout:\n%s\nstderr:\n%s"
            % (result.returncode, " ".join(argv), result.stdout, result.stderr)
        )


def expect_failure(argv: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    result = subprocess.run(argv, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode == 0:
        raise RuntimeError("expected failure: %s" % " ".join(argv))


def make_sdk(work: Path, runtime: Path) -> Path:
    library = work / "sdk" / "lib"
    shutil.copytree(ROOT / "lib", library)
    shutil.copy2(ROOT / "tools" / "scripts" / "toka_build.py", library / "toolchain" / "toka_build.py")
    shutil.copy2(runtime, library / "sys" / "toka_rt.o")
    return library


def write_consumer(project: Path, dependency: Path) -> None:
    (project / "src").mkdir(parents=True)
    (project / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "gui_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        gui = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"gui_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    shutil.copy2(PACKAGE / "tests" / "smoke.tk", project / "src" / "main.tk")
    (project / "src" / "fixture.png").write_bytes(base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wl6SgAAAABJRU5ErkJggg=="
    ))
    (project / "src" / "fixture.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
        '<path fill="#52d273" d="M2 2h12v12H2z"/>'
        '</svg>',
        encoding="utf-8",
    )


def main() -> int:
    tool_dir = Path(os.environ.get("TOKA_GUI_BUILD_BIN", ROOT / "build" / "bin"))
    toka = tool_dir / "toka"
    tokac = tool_dir / "tokac"
    runtime = tool_dir.parent / "lib" / "sys" / "toka_rt.o"
    if not toka.is_file() or not tokac.is_file() or not runtime.is_file():
        raise RuntimeError("build toka, tokac, and lib/sys/toka_rt.o before qualifying official/gui")

    with tempfile.TemporaryDirectory(prefix="toka-gui-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work, runtime)
        dependency = work / "gui"
        shutil.copytree(PACKAGE, dependency)
        consumer = work / "consumer"
        write_consumer(consumer, dependency)
        environment = dict(os.environ)
        environment.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk), "TOKA_OFFLINE": "1"})
        run([str(toka), "fetch"], cwd=consumer, env=environment)
        expect_failure([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
                        "--check-only", str(dependency / "tests" / "window_clone_rejected.tk")],
                       cwd=consumer, env=environment)
        expect_failure([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
                        "--check-only", str(dependency / "tests" / "window_thread_spawn_rejected.tk")],
                       cwd=consumer, env=environment)
        expect_failure([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
                        "--check-only", str(dependency / "tests" / "app_thread_spawn_rejected.tk")],
                       cwd=consumer, env=environment)
        run([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
             "--check-only", str(dependency / "tests" / "host_event_source_compile.tk")],
            cwd=consumer, env=environment)
        run([str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
             "--check-only", str(dependency / "examples" / "settings.tk")],
            cwd=consumer, env=environment)
        run([str(toka), "build"], cwd=consumer, env=environment)
        program = consumer / "target" / "debug" / "gui_consumer"
        if not program.is_file():
            raise RuntimeError("toka build did not produce the GUI consumer")
        run([str(program)], cwd=consumer, env=environment)
        shutil.copy2(PACKAGE / "tests" / "image_smoke_template.tk", consumer / "src" / "main.tk")
        run([str(toka), "build"], cwd=consumer, env=environment)
        run([str(program)], cwd=consumer, env=environment)

    print("official/gui macOS spike qualification: PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
