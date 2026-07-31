#!/usr/bin/env python3
"""Qualify official/unicode through direct and locked-package build paths."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "official" / "unicode"


class QualificationError(RuntimeError):
    pass


def run(argv: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=180,
    )
    if result.returncode != 0:
        raise QualificationError(
            "command failed: %s\\nstdout:\\n%s\\nstderr:\\n%s"
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
        '    name = "unicode_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        unicode = %s,\n"
        "    )\n"
        ")\n" % json.dumps(str(dependency)),
        encoding="utf-8",
    )
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"unicode_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src" / "main.tk").write_text(
        "import core/option::{Option}\n"
        "import official/unicode::{grapheme_count, grapheme_slice}\n\n"
        "fn main() -> i32 {\n"
        '    if grapheme_count("ÄB").unwrap() != 2:usize { return 1 }\n'
        '    auto first = grapheme_slice("ÄB", 0:usize, 1:usize).unwrap()\n'
        "    match cede first {\n"
        '        auto Option<str>::Some(\'value) => { if !\'value.equals("Ä") { return 2 } }\n'
        "        Option<str>::None => return 2\n"
        "    }\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )


def selected_package_helper(project: Path, env: dict[str, str]) -> Path | None:
    candidates = (
        project / "lib" / "toolchain" / "toka_package.py",
        project.parent / "lib" / "toolchain" / "toka_package.py",
        project.parent.parent / "lib" / "toolchain" / "toka_package.py",
        project.parent.parent.parent / "lib" / "toolchain" / "toka_package.py",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    toka_lib = env.get("TOKA_LIB")
    if toka_lib:
        candidate = Path(toka_lib) / "toolchain" / "toka_package.py"
        if candidate.is_file():
            return candidate.resolve()
    return None


def fetch_diagnostics(project: Path, env: dict[str, str]) -> str:
    toka_lib = env.get("TOKA_LIB", "<unset>")
    helper = selected_package_helper(project, env)
    manifest = project / "package.tk"
    manifest_text = manifest.read_text(encoding="utf-8") if manifest.is_file() else "<missing>"
    return (
        "selected package helper: %s\n"
        "TOKA_LIB: %s\n"
        "consumer package.tk:\n%s"
    ) % (helper, toka_lib, manifest_text)


def main() -> int:
    toka = ROOT / "build" / "bin" / "toka"
    tokac = ROOT / "build" / "bin" / "tokac"
    if not toka.is_file() or not tokac.is_file():
        raise QualificationError("build toka and tokac before package qualification")

    environment = dict(os.environ)
    environment.update({"TOKAC": str(tokac)})
    run([sys.executable, str(PACKAGE / "tools" / "generate_tables.py"), "--check"],
        cwd=PACKAGE, env=environment)

    with tempfile.TemporaryDirectory(prefix="toka-unicode-package-") as temporary:
        work = Path(temporary)
        sdk = make_sdk(work)
        dependency = work / "unicode"
        shutil.copytree(PACKAGE, dependency)
        environment["TOKA_LIB"] = str(sdk)

        for fixture in ("unicode_v1.tk", "grapheme_break_corpus.tk"):
            executable = work / fixture.removesuffix(".tk")
            run([
                str(tokac), "-I", str(sdk), "-I", str(dependency / "lib"),
                str(dependency / "tests" / fixture), "-o", str(executable),
            ], cwd=work, env=environment)
            run([str(executable)], cwd=work, env=environment)

        project = work / "consumer"
        write_consumer(project, dependency)
        expected_helper = (sdk / "toolchain" / "toka_package.py").resolve()
        if selected_package_helper(project, environment) != expected_helper:
            raise QualificationError(
                "consumer did not select the isolated SDK package helper\n" +
                fetch_diagnostics(project, environment)
            )
        try:
            run([str(toka), "fetch"], cwd=project, env=environment)
        except QualificationError as error:
            raise QualificationError(str(error) + "\n" + fetch_diagnostics(project, environment)) from error
        lock = project / "package.lock"
        locked = lock.read_bytes()
        if not locked.startswith(b"toka-lock-v1\n") or b"unicode" not in locked:
            raise QualificationError("unicode consumer did not produce a v1 lock")
        offline = dict(environment)
        offline["TOKA_OFFLINE"] = "1"
        try:
            run([str(toka), "fetch"], cwd=project, env=offline)
        except QualificationError as error:
            raise QualificationError(str(error) + "\n" + fetch_diagnostics(project, offline)) from error
        if lock.read_bytes() != locked:
            raise QualificationError("offline unicode fetch changed package.lock")
        run([str(toka), "build"], cwd=project, env=offline)
        run([str(project / "target" / "debug" / "unicode_consumer")], cwd=project, env=offline)

    print(json.dumps({
        "result": "pass",
        "schema": "toka.official-unicode-package-v1",
        "stages": {
            "locked_unicode_data": "pass",
            "unicode_api_suite": "pass",
            "uax29_grapheme_break_corpus": "pass",
            "locked_local_dependency": "pass",
            "offline_lock_replay": "pass",
            "public_import_build_run": "pass",
        },
        "unicode_version": "17.0.0",
        "uax29_revision": 47,
    }, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, QualificationError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error))
        raise SystemExit(1)
