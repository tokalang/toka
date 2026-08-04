#!/usr/bin/env python3
"""Qualify QSLite through TKI, incremental, lock, and offline paths."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "toka.qslite-toolchain"


class QualificationError(RuntimeError):
    pass


def target_name() -> str:
    system = {"Darwin": "macos", "Linux": "linux"}.get(
        platform.system(), platform.system().lower()
    )
    machine = {"x86_64": "x64", "AMD64": "x64", "aarch64": "arm64"}.get(
        platform.machine(), platform.machine().lower()
    )
    return system + "-" + machine


def semantic_lock_sha256(payload: bytes) -> str:
    lines = payload.decode("utf-8").splitlines()
    if not lines or lines[0] != "toka-lock-v1":
        raise QualificationError("QSLite package emitted a malformed lockfile")
    normalized = [lines[0]]
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != 8 or fields[0] != "package":
            raise QualificationError("QSLite package emitted a malformed lock entry")
        if fields[2] == "path":
            fields[3] = "<path-package>"
            fields[4] = "<path-package>"
        normalized.append("\t".join(fields))
    encoded = ("\n".join(normalized) + "\n").encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def run(
    argv: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    expected: int = 0,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        argv,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
    )
    if result.returncode != expected:
        raise QualificationError(
            "command returned %d, expected %d: %s\nstdout:\n%s\nstderr:\n%s"
            % (result.returncode, expected, " ".join(argv), result.stdout, result.stderr)
        )
    return result


def parse_plan(result: subprocess.CompletedProcess[str]) -> dict[str, object]:
    try:
        plan = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise QualificationError("incremental planner did not emit JSON") from error
    if not isinstance(plan, dict):
        raise QualificationError("incremental planner emitted a non-object plan")
    return plan


def link_sdk(test_lib: Path) -> None:
    test_lib.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "lib").iterdir():
        destination = test_lib / source.name
        if source.is_dir():
            shutil.copytree(source, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(source, destination)
    shutil.copy2(ROOT / "tools/scripts/toka_build.py", test_lib / "toolchain/toka_build.py")


def qualify_tki(work: Path, tokac: Path, base_env: dict[str, str]) -> None:
    replay = work / "tki"
    library = replay / "lib"
    module = library / "qslite"
    module.mkdir(parents=True)
    link_sdk(library)
    shutil.copy2(ROOT / "examples/qslite/lib/qslite/storage.tk", module / "storage.tk")
    shutil.copy2(ROOT / "examples/qslite/tests/vertical.tk", replay / "vertical.tk")

    env = dict(base_env)
    env["TOKA_LIB"] = str(library)
    provider_object = module / "storage.o"
    run([str(tokac), "-c", str(module / "storage.tk"), "-o", str(provider_object)], cwd=replay, env=env)
    interface = module / "storage.tki"
    if not interface.is_file():
        raise QualificationError("QSLite provider did not emit storage.tki")
    (module / "storage.tk").rename(module / "storage.tk.hidden")
    executable = replay / "vertical"
    run(
        [str(tokac), str(replay / "vertical.tk"), str(provider_object), "-o", str(executable)],
        cwd=replay,
        env=env,
    )
    output = run([str(executable)], cwd=replay, env=env).stdout
    if "PASS: qslite persistent vertical slice" not in output:
        raise QualificationError("source-less QSLite replay returned unexpected output")


def qualify_incremental(work: Path, toka: Path, tokac: Path, base_env: dict[str, str]) -> None:
    project = work / "incremental"
    shutil.copytree(ROOT / "examples/qslite", project)
    sdk = work / "incremental-sdk/lib"
    link_sdk(sdk)
    env = dict(base_env)
    env.update(
        {
            "TOKAC": str(tokac),
            "TOKA_LIB": str(sdk),
            "TOKA_PKG_ARGS": "-I " + str(project / "lib"),
        }
    )
    manifest = project / ".toka/build/manifest.json"
    command = [str(toka), "build", "--plan", "-m", str(manifest)]
    first = parse_plan(run(command, cwd=project, env=env))
    if first.get("status") != "dirty":
        raise QualificationError("first QSLite incremental plan was not dirty")
    run([str(toka), "build", "-m", str(manifest)], cwd=project, env=env)
    clean = parse_plan(run(command, cwd=project, env=env))
    if clean.get("status") != "clean":
        raise QualificationError("unchanged QSLite incremental plan was not clean: %s" % clean)

    storage = project / "lib/qslite/storage.tk"
    original = storage.read_text(encoding="utf-8")
    storage.write_text(original + "\n", encoding="utf-8")
    changed = parse_plan(run(command, cwd=project, env=env))
    if changed.get("status") != "dirty":
        raise QualificationError("QSLite dependency edit did not dirty the plan")
    modules = changed.get("dirty_modules")
    if not isinstance(modules, dict) or not any(path.endswith("/storage.tk") for path in modules):
        raise QualificationError("QSLite dependency edit did not identify storage.tk")
    storage.write_text(original, encoding="utf-8")
    restored = parse_plan(run(command, cwd=project, env=env))
    if restored.get("status") != "clean":
        raise QualificationError("restored QSLite source did not recover the clean plan")


def write_consumer(project: Path, dependency: Path) -> None:
    project.mkdir(parents=True)
    (project / "src").mkdir()
    locator = json.dumps(str(dependency))
    manifest = (
        "pub const PACKAGE = (\n"
        '    name = "qslite_consumer",\n'
        '    version = "0.1.0",\n'
        "    dependencies = (\n"
        "        qslite = %s,\n"
        "    )\n"
        ")\n"
    ) % locator
    (project / "package.tk").write_text(manifest, encoding="utf-8")
    (project / "build.tk").write_text(
        "import build::{Executable, run_build}\n\n"
        "fn main() -> i32 {\n"
        '    auto app# = Executable::make(c"qslite_consumer", c"src/main.tk")\n'
        "    return run_build(app)\n"
        "}\n",
        encoding="utf-8",
    )
    (project / "src/main.tk").write_text(
        "import std/io::{remove_file}\n"
        "import qslite::{open_database}\n\n"
        "fn main() -> i32 {\n"
        '    auto path = string::from("consumer.qslite")\n'
        "    remove_file(path.clone())\n"
        "    auto opened = open_database(cede path.clone())\n"
        "    if opened.is_err() { return 1 }\n"
        "    auto database# = opened.unwrap()\n"
        '    if database#.upsert(7:u64, string::from("locked")).is_err() { return 2 }\n'
        "    if database.get(7:u64).is_none() { return 3 }\n"
        "    remove_file(path)\n"
        "    return 0\n"
        "}\n",
        encoding="utf-8",
    )


def qualify_package(work: Path, toka: Path, tokac: Path, base_env: dict[str, str]) -> str:
    dependency = work / "qslite-package"
    shutil.copytree(ROOT / "examples/qslite", dependency)
    project = work / "consumer"
    write_consumer(project, dependency)
    sdk = work / "package-sdk/lib"
    link_sdk(sdk)
    env = dict(base_env)
    env.update({"TOKAC": str(tokac), "TOKA_LIB": str(sdk)})

    run([str(toka), "fetch"], cwd=project, env=env)
    lock = project / "package.lock"
    locked = lock.read_bytes()
    if b"qslite" not in locked or len(locked) < 100:
        raise QualificationError("QSLite lockfile did not contain resolved integrity facts")
    offline_env = dict(env)
    offline_env["TOKA_OFFLINE"] = "1"
    run([str(toka), "fetch"], cwd=project, env=offline_env)
    if lock.read_bytes() != locked:
        raise QualificationError("offline QSLite fetch changed package.lock")
    run([str(toka), "build"], cwd=project, env=offline_env)
    executable = project / "target/debug/qslite_consumer"
    run([str(executable)], cwd=project, env=offline_env)
    return semantic_lock_sha256(locked)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tokac", type=Path, default=ROOT / "build/bin/tokac")
    parser.add_argument("--toka", type=Path, default=ROOT / "build/bin/toka")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()
    tokac = args.tokac.resolve()
    toka = args.toka.resolve()
    base_env = dict(os.environ)
    temporary = Path(tempfile.mkdtemp(prefix="qslite-toolchain-")).resolve()
    try:
        work = temporary
        qualify_tki(work, tokac, base_env)
        qualify_incremental(work, toka, tokac, base_env)
        lock_sha256 = qualify_package(work, toka, tokac, base_env)
    finally:
        if args.keep_work:
            print("work_root=" + str(temporary), file=os.sys.stderr)
        else:
            shutil.rmtree(temporary, ignore_errors=True)
    revision = run(["git", "rev-parse", "HEAD"], cwd=ROOT, env=base_env).stdout.strip()
    report = {
        "compiler_revision": revision,
        "lock_semantic_sha256": lock_sha256,
        "platform": target_name(),
        "result": "pass",
        "schema": SCHEMA,
        "stages": {
            "incremental_dependency_recovery": "pass",
            "incremental_first_build": "pass",
            "incremental_no_op": "pass",
            "locked_package_build": "pass",
            "offline_lock_replay": "pass",
            "source_less_tki": "pass",
        },
        "version": 1,
    }
    encoded = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
    if args.output:
        args.output.resolve().write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, QualificationError, subprocess.TimeoutExpired) as error:
        print("FAIL: " + str(error), file=os.sys.stderr)
        raise SystemExit(1)
