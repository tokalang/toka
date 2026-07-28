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
    encode_lock,
    file_sha256,
    LockEntry,
    native_build_plan,
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


def write_official_package(root: Path, name: str) -> None:
    root.mkdir(parents=True)
    (root / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        f'    name = "{name}",\n'
        f'    identity = "official/{name}",\n'
        '    version = "1.0.0",\n'
        "    dependencies = ()\n"
        ")\n",
        encoding="utf-8",
    )
    module = root / "lib" / "official" / (name + ".tk")
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


def test_hash_and_lock(root: Path) -> None:
    vector = root / "sha256-vector"
    vector.write_bytes(b"abc")
    assert file_sha256(vector) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

    tree = root / "hash-tree"
    write_package(tree, "hash_tree", [])
    first = tree_sha256(tree)
    assert len(first) == 64 and first == tree_sha256(tree)
    (tree / "lib" / "hash_tree" / "mod.tk").write_text("changed\n", encoding="utf-8")
    assert tree_sha256(tree) != first

    lock = root / "strict.lock"
    package_path = str(tree.resolve())
    valid = LockEntry("tree", "path", package_path, package_path, "-", tree_sha256(tree), [])
    lock.write_text(encode_lock({"tree": valid}), encoding="utf-8")
    assert list(read_lock(lock)) == ["tree"]
    previous = lock.read_bytes()

    dangling = LockEntry("tree", "path", package_path, package_path, "-", tree_sha256(tree), ["missing"])
    lock.write_text(encode_lock({"tree": dangling}), encoding="utf-8")
    expect_error(lambda: read_lock(lock), "invalid locked dependency reference")
    lock.write_bytes(previous)

    invalid_git = LockEntry("gitpkg", "git", "repo#commit=x", "moving-tag", "-", "0" * 64, [])
    lock.write_text(encode_lock({"gitpkg": invalid_git}), encoding="utf-8")
    expect_error(lambda: read_lock(lock), "invalid locked Git package")
    lock.write_bytes(previous)


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
    assert not (root / "traversal-output").exists()

    absolute = root / "absolute.tar.gz"
    add_tar_bytes(absolute, "/outside", b"bad")
    expect_error(lambda: safe_extract(absolute, root / "absolute-output"), "escapes")
    assert not (root / "absolute-output").exists()

    link = root / "link.tar.gz"
    with tarfile.open(link, "w:gz") as output:
        info = tarfile.TarInfo("link")
        info.type = tarfile.SYMTYPE
        info.linkname = "/tmp/outside"
        output.addfile(info)
    expect_error(lambda: safe_extract(link, root / "link-output"), "link or special")
    assert not (root / "link-output").exists()

    conflict = root / "conflict.tar.gz"
    with tarfile.open(conflict, "w:gz") as output:
        file_info = tarfile.TarInfo("node")
        file_info.size = 1
        output.addfile(file_info, io.BytesIO(b"x"))
        child_info = tarfile.TarInfo("node/child")
        child_info.size = 1
        output.addfile(child_info, io.BytesIO(b"y"))
    expect_error(lambda: safe_extract(conflict, root / "conflict-output"), "file/directory conflict")
    assert not (root / "conflict-output").exists()

    oversized = root / "oversized.tar.gz"
    add_tar_bytes(oversized, "large", b"12345")
    expect_error(
        lambda: safe_extract(oversized, root / "oversized-output", max_file_size=4),
        "size limit",
    )
    assert not (root / "oversized-output").exists()

    corrupt = root / "corrupt.tar.gz"
    corrupt.write_bytes(b"not a tar archive")
    expect_error(lambda: safe_extract(corrupt, root / "corrupt-output"), "not a gzip")
    assert not (root / "corrupt-output").exists()


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


def test_official_mapping(root: Path) -> None:
    workspace = root / "official-mapping"
    project = workspace / "consumer"
    dependency = workspace / "redis"
    write_official_package(dependency, "redis")
    write_package(project, "consumer", [("redis", '"../redis"')])
    resolve(project)
    mappings = compiler_mappings(project / "package.lock", project / ".toka")
    expected = "official/redis=" + str((dependency / "lib" / "official" / "redis.tk").resolve())
    assert mappings == [expected], mappings


def test_native_build_metadata(root: Path) -> None:
    workspace = root / "native-build-metadata"
    project = workspace / "consumer"
    dependency = workspace / "bridge"
    dependency.mkdir(parents=True)
    (dependency / "native").mkdir()
    (dependency / "native" / "bridge.c").write_text("int toka_bridge(void) { return 1; }\n", encoding="utf-8")
    (dependency / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "bridge",\n'
        '    identity = "official/bridge",\n'
        '    version = "1.0.0",\n'
        "    dependencies = (),\n"
        "    native = (required = true, sources = (\"native/bridge.c\"), libraries = (\"zlib\"))\n"
        ")\n",
        encoding="utf-8",
    )
    module = dependency / "lib" / "official" / "bridge.tk"
    module.parent.mkdir(parents=True)
    module.write_text("pub fn value() -> i32 { return 1 }\n", encoding="utf-8")
    write_package(project, "consumer", [("bridge", '"../bridge"')])
    resolve(project)
    plan = native_build_plan(project / "package.lock", project / ".toka")
    assert plan["schema"] == "toka.native-package-plan-v1"
    assert plan["packages"] == [{
        "alias": "bridge",
        "root": str(dependency.resolve()),
        "sources": [str((dependency / "native" / "bridge.c").resolve())],
        "libraries": ["zlib"],
    }]

    manifest = (dependency / "package.tk")
    manifest.write_text(
        manifest.read_text(encoding="utf-8").replace("native/bridge.c", "../bridge.c"),
        encoding="utf-8",
    )
    resolve(project)
    expect_error(
        lambda: native_build_plan(project / "package.lock", project / ".toka"),
        "native.sources must use relative native/*.c paths",
    )


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
    child = root / "registry-child"
    write_package(child, "child", [])
    child_archive = registry / "child-1.0.0.tar.gz"
    make_archive(child, child_archive)
    package = root / "registry-package"
    write_package(package, "reg", [("child", '"1.0.0"')])
    archive = registry / "reg-1.0.0.tar.gz"
    make_archive(package, archive)
    (registry / "catalog.json").write_text(
        '{"packages":[{"name":"reg","version":"1.0.0"},{"name":"child","version":"1.0.0"}]}\n',
        encoding="utf-8",
    )
    old_registry = os.environ.get("TOKA_REGISTRY_URL")
    os.environ["TOKA_REGISTRY_URL"] = registry.as_uri()
    try:
        project = root / "registry-project"
        write_package(project, "root", [("reg", '"1.0.0"')])
        entries = resolve(project)
        assert sorted(entries) == ["child", "reg"]
        entry = entries["reg"]
        assert entry.archive_sha256 != "-"
        lock_bytes = (project / "package.lock").read_bytes()
        installed = project / ".toka" / "packages" / "reg-1.0.0"
        assert installed.is_dir()
        resolve(project, offline=True)
        assert (project / "package.lock").read_bytes() == lock_bytes

        shutil.rmtree(installed)
        resolve(project, offline=True)
        assert installed.is_dir()
        shutil.rmtree(installed)
        cached = project / ".toka" / "cache" / "archives" / (entry.archive_sha256 + ".tar.gz")
        cached.write_bytes(b"corrupt cache")
        expect_error(lambda: resolve(project, offline=True), "missing or corrupt")
        assert not installed.exists()
        resolve(project)
        assert installed.is_dir() and file_sha256(cached) == entry.archive_sha256

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
        assert not (rollback / ".toka" / "packages" / "child-1.0.0").exists()
        assert not (rollback / "escaped").exists()

        (project / "package.tk").write_text(manifest("root-after-remove", []), encoding="utf-8")
        resolve(project)
        assert read_lock(project / "package.lock") == {}
        assert not installed.exists()
        assert not (project / ".toka" / "packages" / "child-1.0.0").exists()
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
        test_hash_and_lock(root)
        test_safe_extract(root)
        test_path_graph(root)
        test_official_mapping(root)
        test_native_build_metadata(root)
        test_cycles_and_conflicts(root)
        test_registry_and_rollback(root)
        test_git_and_remove(root)
        if args.toka:
            test_toka_cli(root, args.toka.resolve())
    print("PASS: package manager supply-chain qualification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
