#!/usr/bin/env python3
"""Offline end-to-end qualification for Toka package resolution."""

from __future__ import annotations

import io
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLCHAIN = ROOT / "lib" / "toolchain"
sys.path.insert(0, str(TOOLCHAIN))

from toka_package import (  # noqa: E402
    PackageError,
    Resolver,
    compiler_mappings,
    parse_manifest,
    read_lock,
    remove_dependency,
    tree_sha256,
)
from toka_safe_extract import ExtractionError, safe_extract  # noqa: E402


def manifest(name: str, dependencies: list[tuple[str, str]]) -> str:
    body = "".join('        %s = %s,\n' % item for item in dependencies)
    return (
        "pub const PACKAGE = (\n"
        f'    name = "{name}",\n'
        '    version = "1.0.0",\n'
        f"    dependencies = (\n{body}    )\n"
        ")\n"
    )


def write_package(root: Path, name: str, dependencies: list[tuple[str, str]]) -> None:
    root.mkdir(parents=True)
    (root / "package.tk").write_text(manifest(name, dependencies), encoding="utf-8")
    module = root / "lib" / name / "mod.tk"
    module.parent.mkdir(parents=True)
    module.write_text("pub fn value() -> i32 { return 1 }\n", encoding="utf-8")


def make_archive(source: Path, archive: Path) -> None:
    with tarfile.open(archive, "w:gz") as output:
        for path in sorted(source.rglob("*")):
            output.add(path, arcname=path.relative_to(source).as_posix(), recursive=False)


def add_tar_bytes(archive: Path, name: str, data: bytes, mode: int = 0o644) -> None:
    with tarfile.open(archive, "w:gz") as output:
        info = tarfile.TarInfo(name)
        info.size = len(data)
        info.mode = mode
        output.addfile(info, io.BytesIO(data))


def expect_error(function, text: str) -> None:
    try:
        function()
    except (PackageError, ExtractionError) as error:
        if text not in str(error):
            raise AssertionError("expected %r in %r" % (text, str(error)))
    else:
        raise AssertionError("expected failure containing: " + text)


def resolve(project: Path, *, offline: bool = False, refresh: bool = False):
    return Resolver(
        project / "package.tk",
        project / "package.lock",
        project / ".toka",
        offline=offline,
        refresh=refresh,
    ).run()


def test_safe_extract(root: Path) -> None:
    source = root / "archive-source"
    write_package(source, "safe", [])
    archive = root / "safe.tar.gz"
    make_archive(source, archive)
    destination = root / "safe-output"
    safe_extract(archive, destination)
    assert (destination / "package.tk").is_file()

    traversal = root / "traversal.tar.gz"
    add_tar_bytes(traversal, "../outside", b"bad")
    expect_error(lambda: safe_extract(traversal, root / "traversal-output"), "escapes")
    assert not (root / "outside").exists()

    link = root / "link.tar.gz"
    with tarfile.open(link, "w:gz") as output:
        info = tarfile.TarInfo("link")
        info.type = tarfile.SYMTYPE
        info.linkname = "/tmp/outside"
        output.addfile(info)
    expect_error(lambda: safe_extract(link, root / "link-output"), "link or special")

    oversized = root / "oversized.tar.gz"
    add_tar_bytes(oversized, "large", b"12345")
    expect_error(
        lambda: safe_extract(oversized, root / "oversized-output", max_file_size=4),
        "size limit",
    )


def test_path_graph(root: Path) -> None:
    workspace = root / "path-graph"
    project = workspace / "project"
    package_a = workspace / "a"
    package_b = workspace / "b"
    write_package(package_b, "b", [])
    write_package(package_a, "a", [("b", '"../b"')])
    write_package(project, "root", [("a", '"../a"')])

    entries = resolve(project)
    assert sorted(entries) == ["a", "b"]
    first_lock = (project / "package.lock").read_bytes()
    resolve(project, offline=True)
    assert (project / "package.lock").read_bytes() == first_lock
    mappings = compiler_mappings(project / "package.lock", project / ".toka")
    assert len(mappings) == 2 and mappings[0].startswith("a=")

    malformed = project / "package.lock"
    previous = malformed.read_bytes()
    malformed.write_text("broken\n", encoding="utf-8")
    expect_error(lambda: resolve(project), "malformed")
    malformed.write_bytes(previous)


def test_cycles_and_conflicts(root: Path) -> None:
    workspace = root / "graph-errors"
    cycle_root = workspace / "cycle-root"
    package_a = workspace / "cycle-a"
    package_b = workspace / "cycle-b"
    write_package(cycle_root, "root", [("a", '"../cycle-a"')])
    write_package(package_a, "a", [("b", '"../cycle-b"')])
    write_package(package_b, "b", [("a", '"../cycle-a"')])
    expect_error(lambda: resolve(cycle_root), "dependency cycle: a -> b -> a")
    assert not (cycle_root / "package.lock").exists()

    conflict_root = workspace / "conflict-root"
    left = workspace / "left"
    right = workspace / "right"
    bridge = workspace / "bridge"
    write_package(left, "shared", [])
    write_package(right, "shared", [])
    write_package(bridge, "bridge", [("shared", '"../right"')])
    write_package(
        conflict_root,
        "root",
        [("shared", '"../left"'), ("bridge", '"../bridge"')],
    )
    expect_error(lambda: resolve(conflict_root), "dependency conflict for alias: shared")
    assert not (conflict_root / "package.lock").exists()


def test_registry_and_rollback(root: Path) -> None:
    registry = root / "registry"
    registry.mkdir()
    package = root / "registry-package"
    write_package(package, "reg", [])
    archive = registry / "reg-1.0.0.tar.gz"
    make_archive(package, archive)
    (registry / "catalog.json").write_text(
        '{"packages":[{"name":"reg","version":"1.0.0"}]}\n',
        encoding="utf-8",
    )
    old_registry = os.environ.get("TOKA_REGISTRY_URL")
    os.environ["TOKA_REGISTRY_URL"] = registry.as_uri()
    try:
        project = root / "registry-project"
        write_package(project, "root", [("reg", '"1.0.0"')])
        entries = resolve(project)
        entry = entries["reg"]
        assert entry.archive_sha256 != "-"
        lock_bytes = (project / "package.lock").read_bytes()
        installed = project / ".toka" / "packages" / "reg-1.0.0"
        assert installed.is_dir()
        resolve(project, offline=True)
        assert (project / "package.lock").read_bytes() == lock_bytes

        (installed / "lib" / "reg" / "mod.tk").write_text("corrupt\n", encoding="utf-8")
        expect_error(lambda: resolve(project, offline=True), "does not match package.lock")
        assert (project / "package.lock").read_bytes() == lock_bytes

        rollback = root / "rollback-project"
        write_package(rollback, "root", [("reg", '"1.0.0"'), ("evil", '"1.0.0"')])
        evil = registry / "evil-1.0.0.tar.gz"
        add_tar_bytes(evil, "../escaped", b"bad")
        expect_error(lambda: resolve(rollback), "escapes")
        assert not (rollback / "package.lock").exists()
        assert not (rollback / ".toka" / "packages" / "reg-1.0.0").exists()
        assert not (rollback / "escaped").exists()
    finally:
        if old_registry is None:
            os.environ.pop("TOKA_REGISTRY_URL", None)
        else:
            os.environ["TOKA_REGISTRY_URL"] = old_registry


def test_git_and_remove(root: Path) -> None:
    workspace = root / "git-case"
    repository = workspace / "repo with spaces"
    write_package(repository, "gitpkg", [])
    subprocess.run(["git", "init", "--quiet"], cwd=repository, check=True)
    subprocess.run(["git", "add", "."], cwd=repository, check=True)
    subprocess.run(
        ["git", "-c", "user.name=Toka Test", "-c", "user.email=toka@example.invalid", "commit", "--quiet", "-m", "initial"],
        cwd=repository,
        check=True,
    )
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip()
    project = workspace / "project"
    expression = 'Git(%s, commit=%s)' % (json_string(str(repository)), json_string(commit))
    write_package(project, "root", [("gitpkg", expression)])
    entries = resolve(project)
    assert entries["gitpkg"].resolved == commit
    first = (project / "package.lock").read_bytes()
    resolve(project, offline=True)
    assert (project / "package.lock").read_bytes() == first

    path_package = workspace / "pathpkg"
    write_package(path_package, "pathpkg", [])
    project_manifest = project / "package.tk"
    project_manifest.write_text(
        manifest("root", [("gitpkg", expression), ("pathpkg", '"../pathpkg"')]),
        encoding="utf-8",
    )
    resolve(project)
    assert remove_dependency(project_manifest, "pathpkg")
    resolve(project)
    assert sorted(read_lock(project / "package.lock")) == ["gitpkg"]


def test_toka_cli(root: Path, toka: Path) -> None:
    workspace = root / "cli workspace"
    project = workspace / "project"
    dependency = workspace / "dep"
    write_package(project, "root", [])
    write_package(dependency, "dep", [])
    environment = os.environ.copy()
    environment["TOKA_LIB"] = str(ROOT / "lib")

    def run(*arguments: str, offline: bool = False) -> None:
        command_environment = environment.copy()
        if offline:
            command_environment["TOKA_OFFLINE"] = "1"
        subprocess.run(
            [str(toka), *arguments],
            cwd=project,
            env=command_environment,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    run("add", str(dependency))
    entries = read_lock(project / "package.lock")
    assert list(entries) == ["dep"]
    locked = (project / "package.lock").read_bytes()
    run("fetch", offline=True)
    assert (project / "package.lock").read_bytes() == locked
    run("up")
    run("rm", "dep")
    assert read_lock(project / "package.lock") == {}


def json_string(value: str) -> str:
    import json

    return json.dumps(value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--toka", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="toka-package-supply-chain-") as temporary:
        root = Path(temporary)
        test_safe_extract(root)
        test_path_graph(root)
        test_cycles_and_conflicts(root)
        test_registry_and_rollback(root)
        test_git_and_remove(root)
        if args.toka:
            test_toka_cli(root, args.toka.resolve())
    print("PASS: package manager supply-chain qualification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
