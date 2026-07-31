#!/usr/bin/env python3
"""Reproducible evidence for the non-semantic Slice 0 resolver audit."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"
PACKAGE = ROOT / "lib" / "toolchain" / "toka_package.py"
FIXTURE = ROOT / "tests" / "conformance" / "ownership" / "handle_payload_permission.tk"


def run(*args: str, cwd: Path | None = None, env: dict[str, str] | None = None) -> str:
    completed = subprocess.run(args, cwd=cwd or ROOT, text=True,
                               env={**os.environ, **(env or {})},
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode:
        raise RuntimeError("command failed:\n$ %s\n%s%s" %
                           (" ".join(args), completed.stdout, completed.stderr))
    return completed.stdout


def manifest(*args: str, source: Path = FIXTURE,
             env: dict[str, str] | None = None) -> dict[str, object]:
    return json.loads(run(str(TOKAC), "--dump-dependencies=json", "-I", str(ROOT / "lib"),
                          *args, str(source), env=env))


def root_coordinate(document: dict[str, object]) -> dict[str, object]:
    root = document["roots"][0]
    return document["modules"][root]["shadow_coordinate"]


def coordinate_with_path(document: dict[str, object], path: str) -> dict[str, object]:
    for module in document["modules"].values():
        coordinate = module["shadow_coordinate"]
        if coordinate["logical_module_path"] == path:
            return coordinate
    raise KeyError("no module has logical module path " + path)


def workspace_manifest(source: Path, root: Path, node: str = "workspace-test-v1") -> dict[str, object]:
    return manifest("--workspace-node", node, "--workspace-root", str(root), source=source)


def module_coordinate(document: dict[str, object], source: Path) -> dict[str, object]:
    return document["modules"][str(source.resolve())]["shadow_coordinate"]


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    direct = root_coordinate(manifest())
    assert direct["status"] == "unknown"
    assert direct["reason"] == "no resolver graph node identity for module"

    workspace = root_coordinate(manifest("--workspace-node", "workspace-test-v1",
                                         "--workspace-root", str(ROOT)))
    assert workspace == {
        "status": "known", "crate_id": "workspace-test-v1",
        "logical_module_path": "tests/conformance/ownership/handle_payload_permission",
        "origin": "workspace", "reason": "",
    }
    missing_workspace = root_coordinate(manifest("--workspace-root", str(ROOT)))
    assert missing_workspace == {
        "status": "unknown", "crate_id": "", "logical_module_path": "",
        "origin": "workspace", "reason": "missing workspace node identity",
    }

    with tempfile.TemporaryDirectory() as temporary:
        temporary_root = Path(temporary)

        relocation_coordinates = []
        for name in ("original", "relocated"):
            workspace_root = temporary_root / name
            source = workspace_root / "src" / "entry.tk"
            source.parent.mkdir(parents=True)
            source.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
            relocation_coordinates.append(root_coordinate(workspace_manifest(
                source, workspace_root, "workspace-relocation-v1")))
        assert relocation_coordinates == [{
            "status": "known", "crate_id": "workspace-relocation-v1",
            "logical_module_path": "src/entry", "origin": "workspace", "reason": "",
        }] * 2

        real_root = temporary_root / "real-workspace"
        real_source = real_root / "src" / "entry.tk"
        real_source.parent.mkdir(parents=True)
        real_source.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
        symlink_root = temporary_root / "symlink-workspace"
        symlink_root.symlink_to(real_root, target_is_directory=True)
        real_coordinate = root_coordinate(workspace_manifest(
            real_source, real_root, "workspace-symlink-v1"))
        symlink_coordinate = root_coordinate(workspace_manifest(
            symlink_root / "src" / "entry.tk", symlink_root, "workspace-symlink-v1"))
        assert real_coordinate == symlink_coordinate == {
            "status": "known", "crate_id": "workspace-symlink-v1",
            "logical_module_path": "src/entry", "origin": "workspace", "reason": "",
        }

        package_root = temporary_root / "package-install"
        package_module = package_root / "lib" / "demo.tk"
        package_module.parent.mkdir(parents=True)
        package_module.write_text("pub fn value() -> i32 { return 7 }\n", encoding="utf-8")
        package_app = temporary_root / "package-app"
        package_source = package_app / "entry.tk"
        package_app.mkdir()
        package_source.write_text("import first\nfn main() -> i32 { return 0 }\n", encoding="utf-8")
        first = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "first=" + str(package_module), "--pkg-node", "first=pkg-alias-v1",
            source=package_source)
        package_source.write_text("import second\nfn main() -> i32 { return 0 }\n", encoding="utf-8")
        second = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "second=" + str(package_module), "--pkg-node", "second=pkg-alias-v1",
            source=package_source)
        assert coordinate_with_path(first, "demo") == coordinate_with_path(second, "demo") == {
            "status": "known", "crate_id": "pkg-alias-v1",
            "logical_module_path": "demo", "origin": "package", "reason": "",
        }
        missing_package = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "second=" + str(package_module), source=package_source)
        assert module_coordinate(missing_package, package_module) == {
            "status": "unknown", "crate_id": "", "logical_module_path": "",
            "origin": "package",
            "reason": "missing locked package node identity for 'second'",
        }

        replay_root = temporary_root / "replay-workspace"
        replay_root.mkdir()
        replay_source = replay_root / "lib.tk"
        replay_main = replay_root / "main.tk"
        replay_source.write_text("pub fn value() -> i32 { return 7 }\n", encoding="utf-8")
        replay_main.write_text(
            "import ./lib::{value}\nfn main() -> i32 { return value() - 7 }\n",
            encoding="utf-8")
        replay_args = ("--workspace-node", "workspace-replay-v1",
                       "--workspace-root", str(replay_root))
        source_replay = manifest(*replay_args, source=replay_main)
        source_coordinate = module_coordinate(source_replay, replay_source)
        run(str(TOKAC), *replay_args, "-c", "--emit-interface", str(replay_source),
            "-o", str(replay_root / "lib.o"))
        replay_tki = replay_root / "lib.tki"
        assert replay_tki.is_file()
        replay_source.rename(replay_root / "lib.tk.hidden")
        tki_replay = manifest(*replay_args, source=replay_main)
        tki_coordinate = coordinate_with_path(tki_replay, "lib")
        assert source_coordinate == tki_coordinate

        cache_root = temporary_root / "cache-workspace"
        cache_root.mkdir()
        cache_source = cache_root / "lib.tk"
        cache_main = cache_root / "main.tk"
        cache_source.write_text("pub fn value() -> i32 { return 7 }\n", encoding="utf-8")
        cache_main.write_text(
            "import ./lib::{value}\nfn main() -> i32 { return value() - 7 }\n",
            encoding="utf-8")
        cache_dir = cache_root / "cache"
        (cache_dir / "objects").mkdir(parents=True)
        (cache_dir / "interfaces").mkdir()
        cache_env = {"TOKA_BUILD_DIR": str(cache_dir), "TOKA_USE_LIB_CACHE": "1"}
        run(str(TOKAC), "--workspace-node", "workspace-replay-v1",
            "--workspace-root", str(cache_root), "-c", "--emit-interface", str(cache_source),
            "-o", str(cache_dir / "objects" / "lib.o"), env=cache_env)
        cached_replay = manifest("--workspace-node", "workspace-replay-v1",
                                 "--workspace-root", str(cache_root), source=cache_main,
                                 env=cache_env)
        cached_coordinate = coordinate_with_path(cached_replay, "lib")
        assert source_coordinate == tki_coordinate
        assert cached_coordinate == {
            "status": "known", "crate_id": "workspace-replay-v1",
            "logical_module_path": "lib", "origin": "workspace", "reason": "",
        }

        forged_root = temporary_root / "forged-workspace"
        forged_root.mkdir()
        forged_source = forged_root / "lib.tk"
        forged_main = forged_root / "main.tk"
        forged_source.write_text("pub fn value() -> i32 { return 7 }\n", encoding="utf-8")
        forged_main.write_text(
            "import ./lib::{value}\nfn main() -> i32 { return value() - 7 }\n",
            encoding="utf-8")
        run(str(TOKAC), "--workspace-node", "workspace-replay-v1",
            "--workspace-root", str(forged_root), "-c", "--emit-interface", str(forged_source),
            "-o", str(forged_root / "lib.o"))
        forged_tki = forged_root / "lib.tki"
        forged_text = forged_tki.read_text(encoding="utf-8")
        forged_text = forged_text.replace("// @meta source_hash:", "// @meta source_hash: any\n// ignored:", 1)
        forged_text = re.sub(r"^// @meta source_path: .*", "// @meta source_path: /forged/outside.tk",
                             forged_text, flags=re.MULTILINE)
        forged_tki.write_text(forged_text, encoding="utf-8")
        forged_source.rename(forged_root / "lib.tk.hidden")
        forged_replay = manifest("--workspace-node", "workspace-replay-v1",
                                 "--workspace-root", str(forged_root), source=forged_main)
        assert coordinate_with_path(forged_replay, "lib") == cached_coordinate

        lock = temporary_root / "package.lock"
        lock.write_text(
            "toka-lock-v1\n"
            "package\tunicode\tpath\t/locked/unicode\t/locked/unicode\t-\t"
            + "a" * 64 + "\t-\n", encoding="utf-8")
        first = run(sys.executable, str(PACKAGE), "compiler-node-mappings", "--lock", str(lock))
        second = run(sys.executable, str(PACKAGE), "compiler-node-mappings", "--lock", str(lock))
    assert first == second
    entries = dict(line.split("=", 1) for line in first.splitlines())
    assert entries["unicode"] == entries["official/unicode"]
    assert entries["unicode"].startswith("pkg-v1-")

    toolchain = manifest("--workspace-node", "workspace-test-v1",
                         "--workspace-root", str(ROOT),
                         source=ROOT / "tests" / "import_test" / "module_alias.tk")
    std_io = coordinate_with_path(toolchain, "std/io")
    assert std_io["status"] == "known"
    assert std_io["origin"] == "toolchain"
    assert std_io["logical_module_path"] == "std/io"
    assert std_io["crate_id"].startswith("toolchain-v1-")

    print("encap Slice 0 resolver audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, KeyError, IndexError, json.JSONDecodeError) as error:
        print("encap Slice 0 resolver audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
