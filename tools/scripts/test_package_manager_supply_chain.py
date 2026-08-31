#!/usr/bin/env python3
"""Offline end-to-end qualification for Toka package resolution."""

from __future__ import annotations

import io
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading


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


def test_windows_absolute_path_dependency(root: Path) -> None:
    manifest_path = root / "windows-absolute-path.tk"
    manifest_path.write_text(
        manifest(
            "windows_path",
            [("dependency", '"D:/toka/packages/child/../dependency"')],
        ),
        encoding="utf-8",
    )
    dependency = parse_manifest(manifest_path)[0]
    assert dependency.kind == "path"
    assert dependency.locator == "D:/toka/packages/dependency"
    assert dependency.selector == "-"


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
    (dependency / "native" / "bridge.m").write_text("int toka_bridge_objc(void) { return 2; }\n", encoding="utf-8")
    (dependency / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "bridge",\n'
        '    identity = "official/bridge",\n'
        '    version = "1.0.0",\n'
        "    dependencies = (),\n"
        "    native = (required = true, sources = (\"native/bridge.c\", \"native/bridge.m\"), libraries = (\"zlib\"), frameworks = (\"AppKit\"))\n"
        ")\n",
        encoding="utf-8",
    )
    module = dependency / "lib" / "official" / "bridge.tk"
    module.parent.mkdir(parents=True)
    module.write_text("pub fn value() -> i32 { return 1 }\n", encoding="utf-8")
    write_package(project, "consumer", [("bridge", '"../bridge"')])
    resolve(project)
    plan = native_build_plan(project / "package.lock", project / ".toka", target="macos")
    assert plan["schema"] == "toka.native-package-plan-v2"
    assert plan["target"] == "macos"
    assert plan["packages"] == [{
        "alias": "bridge",
        "root": str(dependency.resolve()),
        "sources": [str((dependency / "native" / "bridge.c").resolve()), str((dependency / "native" / "bridge.m").resolve())],
        "pkg_config": ["zlib"],
        "frameworks": ["AppKit"],
        "system_libraries": [],
        "ffi_resources": [],
    }]

    manifest = (dependency / "package.tk")
    manifest.write_text(
        manifest.read_text(encoding="utf-8").replace("native/bridge.c", "../bridge.c"),
        encoding="utf-8",
    )
    resolve(project)
    expect_error(
        lambda: native_build_plan(project / "package.lock", project / ".toka", target="macos"),
        "native.sources must use relative native/*.c or native/*.m paths",
    )


def test_conditional_native_build_metadata(root: Path) -> None:
    workspace = root / "conditional-native-build-metadata"
    project = workspace / "consumer"
    dependency = workspace / "bridge"
    dependency.mkdir(parents=True)
    (dependency / "native").mkdir()
    for source in ("common.c", "macos.m", "linux.c", "windows.c"):
        (dependency / "native" / source).write_text(
            "int toka_bridge_%s(void) { return 1; }\n" % source.replace(".", "_"),
            encoding="utf-8",
        )
    (dependency / "package.tk").write_text(
        "pub const PACKAGE = (\n"
        '    name = "bridge",\n'
        '    identity = "official/bridge",\n'
        '    version = "1.0.0",\n'
        '    targets = ("macos", "linux", "windows"),\n'
        "    dependencies = (),\n"
        "    native = (\n"
        "        required = true,\n"
        '        sources = ("native/common.c"),\n'
        '        macos = (sources = ("native/macos.m"), frameworks = ("AppKit")),\n'
        '        linux = (sources = ("native/linux.c"), pkg_config = ("zlib")),\n'
        '        windows = (sources = ("native/windows.c"), system_libraries = ("ws2_32",)),\n'
        "        ffi_resources = ((\n"
        '            name = "Window", acquire = "toka_bridge_open",\n'
        '            release = "toka_bridge_close", ownership = "owned",\n'
        "            nullable = false, thread_affinity = \"ui\", send = false\n"
        "        ))\n"
        "    )\n"
        ")\n",
        encoding="utf-8",
    )
    module = dependency / "lib" / "official" / "bridge.tk"
    module.parent.mkdir(parents=True)
    module.write_text("pub fn value() -> i32 { return 1 }\n", encoding="utf-8")
    write_package(project, "consumer", [("bridge", '"../bridge"')])
    resolve(project)

    macos = native_build_plan(project / "package.lock", project / ".toka", target="macos")
    assert macos["packages"][0]["sources"] == [
        str((dependency / "native" / "common.c").resolve()),
        str((dependency / "native" / "macos.m").resolve()),
    ]
    assert macos["packages"][0]["frameworks"] == ["AppKit"]
    assert macos["packages"][0]["pkg_config"] == []
    assert macos["packages"][0]["ffi_resources"] == [{
        "name": "Window", "acquire": "toka_bridge_open",
        "release": "toka_bridge_close", "ownership": "owned",
        "nullable": False, "thread_affinity": "ui", "send": False,
    }]

    linux = native_build_plan(project / "package.lock", project / ".toka", target="linux")
    assert linux["packages"][0]["sources"] == [
        str((dependency / "native" / "common.c").resolve()),
        str((dependency / "native" / "linux.c").resolve()),
    ]
    assert linux["packages"][0]["pkg_config"] == ["zlib"]
    assert linux["packages"][0]["frameworks"] == []

    windows = native_build_plan(project / "package.lock", project / ".toka", target="windows")
    assert windows["packages"][0]["sources"] == [
        str((dependency / "native" / "common.c").resolve()),
        str((dependency / "native" / "windows.c").resolve()),
    ]
    assert windows["packages"][0]["system_libraries"] == ["ws2_32"]

    expect_error(
        lambda: native_build_plan(project / "package.lock", project / ".toka", target="android"),
        "unsupported native target: android",
    )

    manifest = dependency / "package.tk"
    manifest.write_text(
        manifest.read_text(encoding="utf-8").replace("thread_affinity = \"ui\", send = false", "thread_affinity = \"ui\", send = true"),
        encoding="utf-8",
    )
    resolve(project)
    expect_error(
        lambda: native_build_plan(project / "package.lock", project / ".toka", target="macos"),
        "UI-affine native resource cannot be Send: bridge",
    )

    manifest.write_text(
        manifest.read_text(encoding="utf-8").replace(
            "required = true,", "required = true, cflags = (\"-Dunsafe\",),"
        ),
        encoding="utf-8",
    )
    resolve(project)
    expect_error(
        lambda: native_build_plan(project / "package.lock", project / ".toka", target="macos"),
        "unsupported native manifest field: cflags",
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
    catalog = {
        "schema_version": 1,
        "totalPackages": 2,
        "packages": [
            {
                "name": "reg",
                "version": "1.0.0",
                "latest_version": "1.0.0",
                "installable": True,
                "versions": [{
                    "version": "1.0.0",
                    "tarball_url": archive.as_uri(),
                    "sha256": file_sha256(archive),
                }],
            },
            {
                "name": "child",
                "version": "1.0.0",
                "latest_version": "1.0.0",
                "installable": True,
                "versions": [{
                    "version": "1.0.0",
                    "tarball_url": child_archive.as_uri(),
                    "sha256": file_sha256(child_archive),
                }],
            },
        ],
    }
    (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
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
        os.environ["TOKA_REGISTRY_URL"] = (root / "unreachable-registry").as_uri()
        resolve(project)
        assert installed.is_dir()
        os.environ["TOKA_REGISTRY_URL"] = registry.as_uri()

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

        original_hash = catalog["packages"][0]["versions"][0]["sha256"]
        shutil.rmtree(installed)
        cached.unlink()
        catalog["packages"][0]["versions"][0]["sha256"] = "0" * 64
        (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
        expect_error(lambda: resolve(project), "registry catalog does not match package.lock")
        assert (project / "package.lock").read_bytes() == lock_bytes
        catalog["packages"][0]["versions"][0]["sha256"] = original_hash
        (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
        resolve(project)
        assert installed.is_dir() and file_sha256(cached) == entry.archive_sha256

        mismatch = root / "registry-digest-mismatch"
        catalog["packages"][0]["versions"][0]["sha256"] = "0" * 64
        (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
        write_package(mismatch, "root", [("reg", '"1.0.0"')])
        expect_error(lambda: resolve(mismatch), "does not match registry catalog")
        assert not (mismatch / "package.lock").exists()
        catalog["packages"][0]["versions"][0]["sha256"] = original_hash
        (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")

        (installed / "lib" / "reg" / "mod.tk").write_text("corrupt\n", encoding="utf-8")
        expect_error(lambda: resolve(project, offline=True), "does not match package.lock")
        assert (project / "package.lock").read_bytes() == lock_bytes

        rollback = root / "rollback-project"
        write_package(rollback, "root", [("reg", '"1.0.0"'), ("evil", '"1.0.0"')])
        evil = registry / "evil-1.0.0.tar.gz"
        add_tar_bytes(evil, "../escaped", b"bad")
        catalog["packages"].append({
            "name": "evil",
            "version": "1.0.0",
            "latest_version": "1.0.0",
            "installable": True,
            "versions": [{
                "version": "1.0.0",
                "tarball_url": evil.as_uri(),
                "sha256": file_sha256(evil),
            }],
        })
        catalog["totalPackages"] = 3
        (registry / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
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
    (dependency / "lib" / "dep" / "mod.tk").write_text(
        "pub const ANSWER = 41\n"
        "pub fn value() -> i32 { return 1 }\n",
        encoding="utf-8",
    )
    environment = os.environ.copy()
    environment["TOKA_LIB"] = str(ROOT / "lib")

    def run(*arguments: str, offline: bool = False) -> None:
        command_environment = environment.copy()
        if offline:
            command_environment["TOKA_OFFLINE"] = "1"
        result = subprocess.run(
            [str(toka), *arguments],
            cwd=project,
            env=command_environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode != 0:
            raise AssertionError(
                "toka command failed (%s): %s\nstdout:\n%s\nstderr:\n%s" % (
                    result.returncode,
                    " ".join(arguments),
                    result.stdout,
                    result.stderr,
                )
            )

    run("add", str(dependency))
    entries = read_lock(project / "package.lock")
    assert list(entries) == ["dep"]
    locked = (project / "package.lock").read_bytes()
    run("fetch", offline=True)
    assert (project / "package.lock").read_bytes() == locked

    # A locked consumer must import both executable symbols and public
    # constants from its dependency; mapping alone is not sufficient evidence.
    (project / "src").mkdir()
    (project / "src" / "main.tk").write_text(
        "import dep::{ANSWER, value}\n\n"
        "fn main() -> i32 {\n"
        "    return ANSWER + value() - 42\n"
        "}\n",
        encoding="utf-8",
    )
    mappings = compiler_mappings(project / "package.lock", project / ".toka")
    assert len(mappings) == 1 and mappings[0].startswith("dep="), mappings
    suffix = ".exe" if sys.platform == "win32" else ""
    executable = project / ("locked-const-consumer" + suffix)
    compile_result = subprocess.run(
        [
            str(toka.parent / ("tokac" + suffix)),
            "-I", environment["TOKA_LIB"],
            "--pkg", mappings[0],
            "src/main.tk",
            "-o", str(executable),
        ],
        cwd=project,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if compile_result.returncode != 0:
        raise AssertionError(
            "locked public const consumer did not compile:\nstdout:\n%s\nstderr:\n%s" % (
                compile_result.stdout,
                compile_result.stderr,
            )
        )
    subprocess.run([str(executable)], cwd=project, env=environment, check=True)

    run("up")
    run("rm", "dep")
    assert read_lock(project / "package.lock") == {}


def test_toka_publish_and_consume(root: Path, toka: Path) -> None:
    workspace = root / "publish-consume"
    producer = workspace / "producer"
    registry = workspace / "registry"
    consumer = workspace / "consumer"
    write_package(producer, "replica", [])
    (producer / "lib" / "replica" / "mod.tk").write_text(
        "pub fn value() -> i32 { return 42 }\n", encoding="utf-8"
    )
    (producer / "README.md").write_text("# replica\n", encoding="utf-8")
    (producer / "LICENSE").write_text("test license\n", encoding="utf-8")
    (producer / "tests").mkdir()
    (producer / "tests" / "qualification.tk").write_text(
        "fn main() -> i32 { return 0 }\n", encoding="utf-8"
    )
    registry.mkdir(parents=True)

    class LocalRegistry(BaseHTTPRequestHandler):
        archive = registry / "replica-1.0.0.tar.gz"
        received_upload = False

        def do_POST(self) -> None:
            if self.path != "/api/publish":
                self.send_error(404)
                return
            if self.headers.get("Authorization") != "Bearer test-token":
                self.send_error(401)
                return
            payload = self.rfile.read(int(self.headers["Content-Length"]))
            start = payload.find(b"\r\n\r\n")
            end = payload.rfind(b"\r\n--")
            if start < 0 or end <= start:
                self.send_error(400, "invalid multipart upload")
                return
            self.__class__.archive.write_bytes(payload[start + 4:end])
            self.__class__.received_upload = True
            self.send_response(201)
            self.end_headers()

        def do_GET(self) -> None:
            if self.path == "/catalog.json":
                if not self.__class__.archive.is_file():
                    self.send_error(404)
                    return
                archive_hash = hashlib.sha256(self.__class__.archive.read_bytes()).hexdigest()
                catalog = {
                    "schema_version": 1,
                    "totalPackages": 1,
                    "packages": [{
                        "name": "replica",
                        "version": "1.0.0",
                        "latest_version": "1.0.0",
                        "installable": True,
                        "versions": [{
                            "version": "1.0.0",
                            "tarball_url": "/packages/replica-1.0.0.tar.gz",
                            "sha256": archive_hash,
                        }],
                    }],
                }
                body = json.dumps(catalog).encode("utf-8")
                content_type = "application/json"
            elif self.path == "/packages/replica-1.0.0.tar.gz" and self.__class__.archive.is_file():
                body = self.__class__.archive.read_bytes()
                content_type = "application/gzip"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *arguments) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), LocalRegistry)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    registry_url = "http://127.0.0.1:%d" % server.server_port
    environment = os.environ.copy()
    environment["TOKA_LIB"] = str(ROOT / "lib")
    suffix = ".exe" if sys.platform == "win32" else ""
    try:
        packaged = subprocess.run(
            [str(toka), "publish"], cwd=producer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if packaged.returncode != 0 or "tagged GitHub Release" not in packaged.stdout:
            raise AssertionError(
                "release archive preparation failed:\nstdout:\n%s\nstderr:\n%s" % (
                    packaged.stdout, packaged.stderr,
                )
        )
        archive_path = producer / "replica-1.0.0.tar.gz"
        assert archive_path.is_file()
        first_archive = archive_path.read_bytes()
        with tarfile.open(archive_path, "r:gz") as archive:
            assert {"README.md", "LICENSE", "tests/qualification.tk"}.issubset(archive.getnames())
            for member in archive.getmembers():
                assert member.isfile()
                assert (member.uid, member.gid, member.uname, member.gname, member.mtime) == (
                    0, 0, "", "", 0,
                )

        os.utime(producer / "README.md", (12345, 12345))
        repackaged = subprocess.run(
            [str(toka), "publish"], cwd=producer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if repackaged.returncode != 0:
            raise AssertionError(
                "deterministic package rebuild failed:\nstdout:\n%s\nstderr:\n%s" % (
                    repackaged.stdout, repackaged.stderr,
                )
            )
        assert archive_path.read_bytes() == first_archive

        environment["TOKA_REGISTRY_URL"] = registry_url
        environment["TOKA_REGISTRY_PUBLISH_TOKEN"] = "test-token"
        published = subprocess.run(
            [str(toka), "publish"], cwd=producer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if published.returncode != 0:
            raise AssertionError(
                "local package publish failed:\nstdout:\n%s\nstderr:\n%s" % (
                    published.stdout, published.stderr,
                )
            )
        assert LocalRegistry.received_upload and LocalRegistry.archive.is_file()

        write_package(consumer, "consumer", [])

        searched = subprocess.run(
            [str(toka), "search", "replica"], cwd=consumer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if searched.returncode != 0 or "replica\t1.0.0" not in searched.stdout:
            raise AssertionError(
                "registry search did not consume the static catalog:\nstdout:\n%s\nstderr:\n%s" % (
                    searched.stdout, searched.stderr,
                )
            )

        added = subprocess.run(
            [str(toka), "add", "replica"], cwd=consumer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if added.returncode != 0:
            raise AssertionError(
                "local package consumer setup failed (exit %d):\nstdout:\n%s\nstderr:\n%s" % (
                    added.returncode, added.stdout, added.stderr,
                )
            )
        mappings = compiler_mappings(consumer / "package.lock", consumer / ".toka")
        assert len(mappings) == 1 and mappings[0].startswith("replica="), mappings
        (consumer / "src").mkdir()
        (consumer / "src" / "main.tk").write_text(
            "import replica::{value}\n\n"
            "fn main() -> i32 {\n"
            "    return value() - 42\n"
            "}\n",
            encoding="utf-8",
        )
        executable = consumer / ("replica-consumer" + suffix)
        compiled = subprocess.run(
            [
                str(toka.parent / ("tokac" + suffix)),
                "-I", environment["TOKA_LIB"],
                "--pkg", mappings[0],
                "src/main.tk",
                "-o", str(executable),
            ],
            cwd=consumer, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                "published package consumer did not compile:\nstdout:\n%s\nstderr:\n%s" % (
                    compiled.stdout, compiled.stderr,
                )
            )
        subprocess.run([str(executable)], cwd=consumer, env=environment, check=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


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
        test_windows_absolute_path_dependency(root)
        test_safe_extract(root)
        test_path_graph(root)
        test_official_mapping(root)
        test_native_build_metadata(root)
        test_conditional_native_build_metadata(root)
        test_cycles_and_conflicts(root)
        test_registry_and_rollback(root)
        test_git_and_remove(root)
        if args.toka:
            test_toka_cli(root, args.toka.resolve())
            test_toka_publish_and_consume(root, args.toka.resolve())
    print("PASS: package manager supply-chain qualification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
