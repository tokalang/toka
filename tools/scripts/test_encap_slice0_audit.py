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

        # Stage 1 (2.1.1 / R1 / PLAN-1): shared outer /lib/ ancestor, alias swap,
        # relocation invariance, and overlapping/missing Fail-Closed checks.
        for prefix_name in ("prefix-alpha", "prefix-beta"):
            shared_cache = temporary_root / prefix_name / "lib" / "cache"
            pkg_a_one = shared_cache / "package-a" / "lib" / "one.tk"
            pkg_a_peer = shared_cache / "package-a" / "lib" / "peer.tk"
            pkg_b_two = shared_cache / "package-b" / "lib" / "two.tk"
            pkg_a_one.parent.mkdir(parents=True)
            pkg_b_two.parent.mkdir(parents=True)
            pkg_a_peer.write_text("pub fn peer_val() -> i32 { return 3 }\n", encoding="utf-8")
            pkg_a_one.write_text("import ./peer::{peer_val}\npub fn one_val() -> i32 { return peer_val() }\n", encoding="utf-8")
            pkg_b_two.write_text("pub fn two_val() -> i32 { return 4 }\n", encoding="utf-8")

            dual_app = temporary_root / f"dual-app-{prefix_name}"
            dual_app.mkdir()
            dual_entry = dual_app / "entry.tk"
            dual_entry.write_text("import first\nimport second\nfn main() -> i32 { return 0 }\n", encoding="utf-8")

            forward = manifest(
                "--workspace-node", "workspace-dual-v1", "--workspace-root", str(dual_app),
                "--pkg", "first=" + str(pkg_a_one), "--pkg-node", "first=package-A",
                "--pkg", "second=" + str(pkg_b_two), "--pkg-node", "second=package-B",
                source=dual_entry)
            swapped = manifest(
                "--workspace-node", "workspace-dual-v1", "--workspace-root", str(dual_app),
                "--pkg", "second=" + str(pkg_a_one), "--pkg-node", "second=package-A",
                "--pkg", "first=" + str(pkg_b_two), "--pkg-node", "first=package-B",
                source=dual_entry)
            for doc in (forward, swapped):
                assert module_coordinate(doc, pkg_a_one) == {
                    "status": "known", "crate_id": "package-A",
                    "logical_module_path": "one", "origin": "package", "reason": "",
                }
                assert module_coordinate(doc, pkg_a_peer) == {
                    "status": "known", "crate_id": "package-A",
                    "logical_module_path": "peer", "origin": "package", "reason": "",
                }
                assert module_coordinate(doc, pkg_b_two) == {
                    "status": "known", "crate_id": "package-B",
                    "logical_module_path": "two", "origin": "package", "reason": "",
                }

        overlap_root = temporary_root / "overlap-pkg" / "lib"
        overlap_one = overlap_root / "one.tk"
        overlap_two = overlap_root / "two.tk"
        overlap_root.mkdir(parents=True)
        overlap_one.write_text("pub fn a() -> i32 { return 1 }\n", encoding="utf-8")
        overlap_two.write_text("pub fn b() -> i32 { return 2 }\n", encoding="utf-8")
        overlap_entry = package_app / "overlap_entry.tk"
        overlap_entry.write_text("import first\nimport second\nfn main() -> i32 { return 0 }\n", encoding="utf-8")
        conflicting_crates = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "first=" + str(overlap_one), "--pkg-node", "first=package-A",
            "--pkg", "second=" + str(overlap_two), "--pkg-node", "second=package-B",
            source=overlap_entry)
        assert module_coordinate(conflicting_crates, overlap_one) == {
            "status": "unknown", "crate_id": "", "logical_module_path": "",
            "origin": "package",
            "reason": "conflicting package root bindings for 'first' and 'second'",
        }
        assert module_coordinate(conflicting_crates, overlap_two) == {
            "status": "unknown", "crate_id": "", "logical_module_path": "",
            "origin": "package",
            "reason": "conflicting package root bindings for 'first' and 'second'",
        }

        partial_missing_node = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "first=" + str(overlap_one), "--pkg-node", "first=package-A",
            "--pkg", "second=" + str(overlap_two),
            source=overlap_entry)
        assert module_coordinate(partial_missing_node, overlap_one) == {
            "status": "unknown", "crate_id": "", "logical_module_path": "",
            "origin": "package",
            "reason": "missing locked package node identity for 'second'",
        }

        sibling_lib = temporary_root / "shared-mod-lib" / "lib"
        sib_alpha_mod = sibling_lib / "pkg_alpha" / "mod.tk"
        sib_alpha_peer = sibling_lib / "pkg_alpha" / "peer.tk"
        sib_beta_mod = sibling_lib / "pkg_beta" / "mod.tk"
        sib_alpha_mod.parent.mkdir(parents=True)
        sib_beta_mod.parent.mkdir(parents=True)
        sib_alpha_peer.write_text("pub fn p() -> i32 { return 1 }\n", encoding="utf-8")
        sib_alpha_mod.write_text("import ./peer\npub fn a() -> i32 { return 1 }\n", encoding="utf-8")
        sib_beta_mod.write_text("pub fn b() -> i32 { return 2 }\n", encoding="utf-8")
        sibling_mod_doc = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "first=" + str(sib_alpha_mod), "--pkg-node", "first=package-A",
            "--pkg", "second=" + str(sib_beta_mod), "--pkg-node", "second=package-B",
            source=overlap_entry)
        assert module_coordinate(sibling_mod_doc, sib_alpha_mod) == {
            "status": "known", "crate_id": "package-A",
            "logical_module_path": "pkg_alpha/mod", "origin": "package", "reason": "",
        }
        assert module_coordinate(sibling_mod_doc, sib_alpha_peer) == {
            "status": "known", "crate_id": "package-A",
            "logical_module_path": "pkg_alpha/peer", "origin": "package", "reason": "",
        }
        assert module_coordinate(sibling_mod_doc, sib_beta_mod) == {
            "status": "known", "crate_id": "package-B",
            "logical_module_path": "pkg_beta/mod", "origin": "package", "reason": "",
        }

        nested_outer = temporary_root / "nested-pkg" / "lib" / "outer.tk"
        nested_inner = temporary_root / "nested-pkg" / "lib" / "inner" / "lib" / "inner_mod.tk"
        nested_inner.parent.mkdir(parents=True)
        nested_outer.write_text("pub fn o() -> i32 { return 1 }\n", encoding="utf-8")
        nested_inner.write_text("pub fn i() -> i32 { return 2 }\n", encoding="utf-8")
        conflicting_paths = manifest(
            "--workspace-node", "workspace-package-v1", "--workspace-root", str(package_app),
            "--pkg", "first=" + str(nested_outer), "--pkg-node", "first=package-A",
            "--pkg", "second=" + str(nested_inner), "--pkg-node", "second=package-A",
            source=overlap_entry)
        assert module_coordinate(conflicting_paths, nested_inner) == {
            "status": "unknown", "crate_id": "", "logical_module_path": "",
            "origin": "package",
            "reason": "conflicting package root bindings for 'first' and 'second'",
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
