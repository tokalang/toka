#!/usr/bin/env python3
"""Stage 1 foundation verification for 0.19 inline field visibility migration.

Scope (Stage 1 only — strictly honest boundaries):
1. Authoritative package & module identity binding (XP-04..XP-06 & TKI-04
   resolver identity sub-items, closing R1 + PLAN-1).
2. TKI format version 6 + --encap-epoch=0.18|0.19 bidirectional isolation
   using plain `pub` interfaces without any `pkg` token (TKI-05, closing R4).
3. Same-SourceHash cross-epoch cache invalidation & sourceless rejection for
   both adjacent .tki and TOKA_BUILD_DIR/TOKA_USE_LIB_CACHE=1 (.tki/.o/.tke)
   (TKI-06, closing R4).

Note: `pkg` keyword lexing/parsing and default-private field visibility Sema
enforcement belong to Stage 2 and are NOT claimed by this Stage 1 gate.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
NEW_TOKAC = Path(os.environ.get("TOKAC", str(ROOT / "build" / "bin" / "tokac")))
PUBLISHED_V0181_TOKAC = Path(
    os.environ.get(
        "PUBLISHED_V0181_TOKAC",
        "/Users/zhyi/LumiCoreHome2/artifacts/evaluations/"
        "v0181-published-sdk-str-match-proof/unpacked/"
        "toka-v0.18.1-macos-arm64/bin/tokac",
    )
)
ARTIFACT_DIR = Path(
    os.environ.get(
        "STAGE1_ARTIFACT_DIR",
        "/Users/zhyi/LumiCoreHome2/artifacts/evaluations/019-stage1-encap-foundation",
    )
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


class ReceiptRecorder:
    def __init__(self) -> None:
        self.commands: list[dict[str, Any]] = []

    def run(
        self,
        label: str,
        argv: list[str],
        *,
        cwd: Path = ROOT,
        env_overrides: dict[str, str] | None = None,
        expect_rc: int | None = 0,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        if env_overrides:
            env.update(env_overrides)
        completed = subprocess.run(
            argv,
            cwd=str(cwd),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.commands.append(
            {
                "label": label,
                "argv": argv,
                "cwd": str(cwd),
                "env_overrides": env_overrides or {},
                "exit_code": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            }
        )
        if expect_rc is not None and completed.returncode != expect_rc:
            raise RuntimeError(
                f"[{label}] expected exit {expect_rc}, got {completed.returncode}\n"
                f"$ {' '.join(argv)}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        return completed


def records_by_basename(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for canonical_path, entry in payload.get("modules", {}).items():
        name = Path(canonical_path).name
        assert name not in result, f"duplicate basename {name}: {payload}"
        result[name] = entry
    return result


def parse_tki_meta(tki_path: Path) -> dict[str, str]:
    meta: dict[str, str] = {}
    for line in tki_path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("// @meta "):
            if not line.startswith("//"):
                break
            continue
        rest = line[len("// @meta ") :]
        if ":" in rest:
            k, v = rest.split(":", 1)
            meta[k.strip()] = v.strip()
    return meta


def verify_identity_subitems(recorder: ReceiptRecorder) -> dict[str, Any]:
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)

        def build_dual_pkg_tree(prefix_name: str) -> tuple[Path, Path, Path, Path]:
            root = base / prefix_name
            ws = root / "workspace"
            pkg_a = root / "lib" / "cache" / "package-a" / "lib"
            pkg_b = root / "lib" / "cache" / "package-b" / "lib"
            ws.mkdir(parents=True)
            pkg_a.mkdir(parents=True)
            pkg_b.mkdir(parents=True)
            (pkg_a / "one.tk").write_text(
                "import ./peer::{peer_val}\n"
                "pub fn one_val() -> i32 { return peer_val() + 1 }\n",
                encoding="utf-8",
            )
            (pkg_a / "peer.tk").write_text(
                "pub fn peer_val() -> i32 { return 10 }\n",
                encoding="utf-8",
            )
            (pkg_b / "two.tk").write_text(
                "pub fn two_val() -> i32 { return 20 }\n",
                encoding="utf-8",
            )
            main_tk = ws / "main.tk"
            main_tk.write_text(
                "import first\n"
                "import second\n"
                "fn main() -> i32 { return 0 }\n",
                encoding="utf-8",
            )
            return ws, main_tk, pkg_a / "one.tk", pkg_b / "two.tk"

        ws_a, main_a, one_a, two_a = build_dual_pkg_tree("prefix-alpha")
        ws_b, main_b, one_b, two_b = build_dual_pkg_tree("prefix-beta")

        # 1a. Shared outer /lib/ ancestor: package-a and package-b stay distinct
        res_order1 = recorder.run(
            "ID-01_shared_lib_ancestor_order1",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-stage1-v1",
                "--workspace-root",
                str(ws_a),
                "--pkg",
                f"first={one_a}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "--pkg",
                f"second={two_a}",
                "--pkg-node",
                "second=pkg-beta-v1",
                str(main_a),
            ],
        )
        records_order1 = records_by_basename(json.loads(res_order1.stdout))
        for fname, expected_crate, expected_logical in (
            ("one.tk", "pkg-alpha-v1", "one"),
            ("peer.tk", "pkg-alpha-v1", "peer"),
            ("two.tk", "pkg-beta-v1", "two"),
        ):
            coord = records_order1[fname]["shadow_coordinate"]
            assert coord["status"] == "known", coord
            assert coord["origin"] == "package", coord
            assert coord["crate_id"] == expected_crate, coord
            assert coord["logical_module_path"] == expected_logical, coord

        # 1b. Swapped first/second alias mapping + relocated root invariance
        res_swapped = recorder.run(
            "ID-02_alias_swap_and_relocation_invariance",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-stage1-v1",
                "--workspace-root",
                str(ws_b),
                "--pkg",
                f"second={one_b}",
                "--pkg-node",
                "second=pkg-alpha-v1",
                "--pkg",
                f"first={two_b}",
                "--pkg-node",
                "first=pkg-beta-v1",
                str(main_b),
            ],
        )
        records_swapped = records_by_basename(json.loads(res_swapped.stdout))
        for fname in ("one.tk", "peer.tk", "two.tk", "main.tk"):
            c1 = records_order1[fname]["shadow_coordinate"]
            c2 = records_swapped[fname]["shadow_coordinate"]
            assert c1["crate_id"] == c2["crate_id"], (fname, c1, c2)
            assert c1["logical_module_path"] == c2["logical_module_path"], (fname, c1, c2)

        # 1c. Overlapping package roots with conflicting CrateIds -> Fail-Closed
        peer_a = one_a.parent / "peer.tk"
        res_conflict = recorder.run(
            "ID-03_overlapping_pkg_roots_conflict_fail_closed",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-stage1-v1",
                "--workspace-root",
                str(ws_a),
                "--pkg",
                f"first={one_a}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "--pkg",
                f"second={peer_a}",
                "--pkg-node",
                "second=pkg-conflicting-v2",
                str(main_a),
            ],
        )
        records_conflict = records_by_basename(json.loads(res_conflict.stdout))
        for fname in ("one.tk", "peer.tk"):
            coord = records_conflict[fname]["shadow_coordinate"]
            assert coord["status"] == "unknown", coord
            assert coord["origin"] == "package", coord
            assert coord["crate_id"] == "", coord
            assert "conflicting package root bindings" in coord["reason"], coord

        # 1d. Partial missing --pkg-node on overlapping root -> Fail-Closed
        res_missing_node = recorder.run(
            "ID-04_partial_missing_pkg_node_fail_closed",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-stage1-v1",
                "--workspace-root",
                str(ws_a),
                "--pkg",
                f"first={one_a}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "--pkg",
                f"second={peer_a}",
                str(main_a),
            ],
        )
        records_missing = records_by_basename(json.loads(res_missing_node.stdout))
        for fname in ("one.tk", "peer.tk"):
            coord = records_missing[fname]["shadow_coordinate"]
            assert coord["status"] == "unknown", coord
            assert coord["origin"] == "package", coord
            assert coord["crate_id"] == "", coord
            assert "missing locked package node identity" in coord["reason"], coord

        # 1e. [STAGE1-2a] Shared /lib/ parent directory with sibling <pkg>/mod.tk packages
        shared_ws = base / "shared_lib_ws"
        shared_lib = shared_ws / "lib"
        alpha_dir = shared_lib / "pkg_alpha"
        beta_dir = shared_lib / "pkg_beta"
        alpha_dir.mkdir(parents=True)
        beta_dir.mkdir(parents=True)
        alpha_mod = alpha_dir / "mod.tk"
        alpha_peer = alpha_dir / "peer.tk"
        beta_mod = beta_dir / "mod.tk"
        shared_main = shared_ws / "main.tk"
        alpha_peer.write_text(
            "pub shape AlphaPeer(v: i32)\n"
            "impl AlphaPeer@Encap { pub v }\n",
            encoding="utf-8",
        )
        alpha_mod.write_text(
            "import ./peer::{AlphaPeer}\n"
            "pub shape AlphaMod(v: i32)\n"
            "impl AlphaMod@Encap { pub v }\n"
            "pub fn make_alpha() -> i32 {\n"
            "  auto p = AlphaPeer(v = 11)\n"
            "  return p.v\n"
            "}\n",
            encoding="utf-8",
        )
        beta_mod.write_text(
            "pub shape BetaMod(v: i32)\n"
            "impl BetaMod@Encap { pub v }\n"
            "pub fn make_beta() -> i32 { return 31 }\n",
            encoding="utf-8",
        )
        shared_main.write_text(
            "import first::{make_alpha}\n"
            "import second::{make_beta}\n"
            "fn main() -> i32 { return make_alpha() + make_beta() - 42 }\n",
            encoding="utf-8",
        )
        res_shared = recorder.run(
            "ID-05_shared_lib_sibling_mod_packages_distinct_identities",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-shared-v1",
                "--workspace-root",
                str(shared_ws),
                "--pkg",
                f"first={alpha_mod}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "--pkg",
                f"second={beta_mod}",
                "--pkg-node",
                "second=pkg-beta-v1",
                str(shared_main),
            ],
        )
        shared_modules = json.loads(res_shared.stdout)["modules"]
        alpha_mod_coord = shared_modules[str(alpha_mod.resolve())]["shadow_coordinate"]
        alpha_peer_coord = shared_modules[str(alpha_peer.resolve())]["shadow_coordinate"]
        beta_mod_coord = shared_modules[str(beta_mod.resolve())]["shadow_coordinate"]
        assert alpha_mod_coord == {
            "status": "known",
            "crate_id": "pkg-alpha-v1",
            "logical_module_path": "pkg_alpha/mod",
            "origin": "package",
            "reason": "",
        }, alpha_mod_coord
        assert alpha_peer_coord == {
            "status": "known",
            "crate_id": "pkg-alpha-v1",
            "logical_module_path": "pkg_alpha/peer",
            "origin": "package",
            "reason": "",
        }, alpha_peer_coord
        assert beta_mod_coord == {
            "status": "known",
            "crate_id": "pkg-beta-v1",
            "logical_module_path": "pkg_beta/mod",
            "origin": "package",
            "reason": "",
        }, beta_mod_coord

        # 1f. [STAGE1-2b] --pkg alias=/path/to/one.tk with .tk extension resolves one.tki when one.tk is hidden
        sourceless_pkg_ws, sl_main, sl_one, sl_two = build_dual_pkg_tree(
            "prefix-sourceless"
        )
        sl_peer = sl_one.parent / "peer.tk"
        recorder.run(
            "ID-06a_emit_package_tkis_for_sourceless_pkg_entry",
            [
                str(NEW_TOKAC),
                "--pkg",
                f"first={sl_one}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "-c",
                "-o",
                str(sl_peer.parent / "peer.o"),
                str(sl_peer),
            ],
        )
        recorder.run(
            "ID-06b_emit_package_entry_tki",
            [
                str(NEW_TOKAC),
                "--pkg",
                f"first={sl_one}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "-c",
                "-o",
                str(sl_one.parent / "one.o"),
                str(sl_one),
            ],
        )
        sl_one_tki = sl_one.with_suffix(".tki")
        sl_peer_tki = sl_peer.with_suffix(".tki")
        assert sl_one_tki.is_file() and sl_peer_tki.is_file()
        # Physically hide .tk sources while keeping --pkg first=<.../one.tk>
        sl_one.rename(sl_one.with_suffix(".tk.hidden"))
        sl_peer.rename(sl_peer.with_suffix(".tk.hidden"))
        res_sl_pkg = recorder.run(
            "ID-06c_sourceless_pkg_with_tk_extension_resolves_tki",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "ws-sl-v1",
                "--workspace-root",
                str(sourceless_pkg_ws),
                "--pkg",
                f"first={sl_one}",
                "--pkg-node",
                "first=pkg-alpha-v1",
                "--pkg",
                f"second={sl_two}",
                "--pkg-node",
                "second=pkg-beta-v1",
                str(sl_main),
            ],
        )
        sl_records = records_by_basename(json.loads(res_sl_pkg.stdout))
        assert sl_records["one.tki"]["kind"] == "interface", sl_records["one.tki"]
        assert sl_records["one.tki"]["shadow_coordinate"] == {
            "status": "known",
            "crate_id": "pkg-alpha-v1",
            "logical_module_path": "one",
            "origin": "package",
            "reason": "",
        }, sl_records["one.tki"]
        assert sl_records["peer.tki"]["kind"] == "interface", sl_records["peer.tki"]
        assert sl_records["peer.tki"]["shadow_coordinate"] == {
            "status": "known",
            "crate_id": "pkg-alpha-v1",
            "logical_module_path": "peer",
            "origin": "package",
            "reason": "",
        }, sl_records["peer.tki"]

        return {
            "order1": {
                k: v["shadow_coordinate"]
                for k, v in records_order1.items()
            },
            "shared_lib_sibling_mod": {
                "pkg_alpha_mod": alpha_mod_coord,
                "pkg_alpha_peer": alpha_peer_coord,
                "pkg_beta_mod": beta_mod_coord,
            },
            "sourceless_pkg_entry_tki": {
                "one.tki": sl_records["one.tki"]["shadow_coordinate"],
                "peer.tki": sl_records["peer.tki"]["shadow_coordinate"],
            },
            "conflict_one_reason": records_conflict["one.tk"]["shadow_coordinate"]["reason"],
            "missing_node_one_reason": records_missing["one.tk"]["shadow_coordinate"]["reason"],
        }


def assert_three_mode_rejected_zero_products(
    recorder: ReceiptRecorder,
    label_prefix: str,
    tokac_bin: Path,
    source_path: Path,
    extra_args: list[str],
    expected_substrings: tuple[str, ...],
) -> None:
    out_dir = source_path.parent
    run_cwd = out_dir if tokac_bin == PUBLISHED_V0181_TOKAC else ROOT
    for mode_name, mode_flags, product_path in (
        ("check", ["--check-only"], None),
        ("obj", ["-c", "-o", str(out_dir / f"{label_prefix}.o")], out_dir / f"{label_prefix}.o"),
        (
            "llvm",
            ["-c", "--emit-llvm", "-o", str(out_dir / f"{label_prefix}.ll")],
            out_dir / f"{label_prefix}.ll",
        ),
    ):
        if product_path and product_path.exists():
            product_path.unlink()
        completed = recorder.run(
            f"{label_prefix}_{mode_name}",
            [str(tokac_bin), *extra_args, *mode_flags, str(source_path)],
            cwd=run_cwd,
            expect_rc=1,
        )
        for sub in expected_substrings:
            assert sub in completed.stderr, (
                f"[{label_prefix}_{mode_name}] missing {sub!r} in stderr:\n{completed.stderr}"
            )
        if product_path:
            assert not product_path.exists(), (
                f"[{label_prefix}_{mode_name}] unexpected residual product: {product_path}"
            )


def verify_tki05_bidirectional_isolation(recorder: ReceiptRecorder) -> dict[str, Any]:
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)

        # Case A: v0.18.1 generates format 5 .tki -> 0.19 tokac rejects with E0901
        dir_v5 = base / "v5_to_v6"
        dir_v5.mkdir()
        plain_v5_tk = dir_v5 / "plain.tk"
        plain_v5_tki = dir_v5 / "plain.tki"
        consumer_v5_tk = dir_v5 / "main.tk"
        plain_v5_tk.write_text(
            "pub shape Plain(x: i32)\n"
            "impl Plain@Encap { pub x }\n"
            "pub fn get_x(p: Plain) -> i32 { return p.x }\n",
            encoding="utf-8",
        )
        consumer_v5_tk.write_text(
            "import ./plain::{Plain, get_x}\n"
            "fn main() -> i32 {\n"
            "  auto p = Plain(x = 42)\n"
            "  return get_x(p) - 42\n"
            "}\n",
            encoding="utf-8",
        )
        recorder.run(
            "TKI05_A_emit_v5_tki_with_published_v0181",
            [
                str(PUBLISHED_V0181_TOKAC),
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_v5),
                "-c",
                "-o",
                str(dir_v5 / "plain.o"),
                str(plain_v5_tk),
            ],
            cwd=dir_v5,
        )
        meta_v5 = parse_tki_meta(plain_v5_tki)
        assert meta_v5.get("format_version") == "5", meta_v5
        # Physically hide .tk source so fallback is impossible
        plain_v5_tk.rename(dir_v5 / "plain.tk.hidden")
        assert not plain_v5_tk.exists()
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_A_new_tokac_rejects_v5_tki",
            NEW_TOKAC,
            consumer_v5_tk,
            ["--workspace-node", "tki05-ws-v1", "--workspace-root", str(dir_v5)],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface format version mismatch (expected 6, got 5)",
            ),
        )

        # Case B: 0.19 tokac generates format 6 .tki -> published v0.18.1 rejects with E0901
        dir_v6 = base / "v6_to_v5"
        dir_v6.mkdir()
        plain_v6_tk = dir_v6 / "plain.tk"
        plain_v6_tki = dir_v6 / "plain.tki"
        consumer_v6_tk = dir_v6 / "main.tk"
        plain_v6_tk.write_text(
            "pub shape Plain(x: i32)\n"
            "impl Plain@Encap { pub x }\n"
            "pub fn get_x(p: Plain) -> i32 { return p.x }\n",
            encoding="utf-8",
        )
        consumer_v6_tk.write_text(
            "import ./plain::{Plain, get_x}\n"
            "fn main() -> i32 {\n"
            "  auto p = Plain(x = 42)\n"
            "  return get_x(p) - 42\n"
            "}\n",
            encoding="utf-8",
        )
        recorder.run(
            "TKI05_B_emit_v6_tki_with_new_tokac",
            [
                str(NEW_TOKAC),
                "--encap-epoch=0.18",
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_v6),
                "-c",
                "-o",
                str(dir_v6 / "plain.o"),
                str(plain_v6_tk),
            ],
        )
        meta_v6 = parse_tki_meta(plain_v6_tki)
        assert meta_v6.get("format_version") == "6", meta_v6
        assert meta_v6.get("encap_epoch") == "0.18", meta_v6
        plain_v6_tk.rename(dir_v6 / "plain.tk.hidden")
        assert not plain_v6_tk.exists()
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_B_published_v0181_rejects_v6_tki",
            PUBLISHED_V0181_TOKAC,
            consumer_v6_tk,
            ["--workspace-node", "tki05-ws-v1", "--workspace-root", str(dir_v6)],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface format version mismatch (expected 5, got 6)",
            ),
        )

        # Case C: Format 6 cross-epoch sourceless isolation (0.18 -> 0.19 and 0.19 -> 0.18)
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_C1_epoch019_rejects_epoch018_tki",
            NEW_TOKAC,
            consumer_v6_tk,
            [
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_v6),
            ],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface encap_epoch mismatch (expected 0.19, got 0.18)",
            ),
        )

        dir_e019 = base / "epoch019_to_018"
        dir_e019.mkdir()
        plain_e019_tk = dir_e019 / "plain.tk"
        plain_e019_tki = dir_e019 / "plain.tki"
        consumer_e019_tk = dir_e019 / "main.tk"
        plain_e019_tk.write_text(
            "pub shape Plain(x: i32)\n"
            "pub fn make_plain(v: i32) -> Plain { return Plain(x = v) }\n"
            "pub fn get_x(p: Plain) -> i32 { return p.x }\n",
            encoding="utf-8",
        )
        consumer_e019_tk.write_text(
            "import ./plain::{Plain, make_plain, get_x}\n"
            "fn main() -> i32 {\n"
            "  auto p = make_plain(42)\n"
            "  return get_x(p) - 42\n"
            "}\n",
            encoding="utf-8",
        )
        recorder.run(
            "TKI05_C2_emit_epoch019_tki",
            [
                str(NEW_TOKAC),
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_e019),
                "-c",
                "-o",
                str(dir_e019 / "plain.o"),
                str(plain_e019_tk),
            ],
        )
        meta_e019 = parse_tki_meta(plain_e019_tki)
        assert meta_e019.get("format_version") == "6", meta_e019
        assert meta_e019.get("encap_epoch") == "0.19", meta_e019
        plain_e019_tk.rename(dir_e019 / "plain.tk.hidden")
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_C2_epoch018_rejects_epoch019_tki",
            NEW_TOKAC,
            consumer_e019_tk,
            [
                "--encap-epoch=0.18",
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_e019),
            ],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface encap_epoch mismatch (expected 0.18, got 0.19)",
            ),
        )

        # Case D: Invalid --encap-epoch CLI value rejected
        bad_epoch = recorder.run(
            "TKI05_D_invalid_encap_epoch_rejected",
            [str(NEW_TOKAC), "--encap-epoch=0.17", "--check-only", str(consumer_e019_tk)],
            expect_rc=1,
        )
        assert "unsupported --encap-epoch value '0.17'" in bad_epoch.stderr

        # Case E: [STAGE1-1] Sourceless .tki stripped of `// @meta encap_epoch:` rejected
        # in all three modes under both --encap-epoch=0.19 and --encap-epoch=0.18
        dir_no_epoch = base / "missing_encap_epoch"
        shutil.copytree(dir_e019, dir_no_epoch)
        stripped_tki = dir_no_epoch / "plain.tki"
        stripped_lines = [
            line
            for line in stripped_tki.read_text(encoding="utf-8").splitlines()
            if not line.startswith("// @meta encap_epoch:")
        ]
        stripped_tki.write_text("\n".join(stripped_lines) + "\n", encoding="utf-8")
        assert "encap_epoch" not in parse_tki_meta(stripped_tki)
        assert not (dir_no_epoch / "plain.tk").exists()
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_E1_epoch019_rejects_missing_encap_epoch_tki",
            NEW_TOKAC,
            dir_no_epoch / "main.tk",
            [
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_no_epoch),
            ],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface encap_epoch mismatch (expected 0.19, got <missing>)",
            ),
        )
        assert_three_mode_rejected_zero_products(
            recorder,
            "TKI05_E2_epoch018_rejects_missing_encap_epoch_tki",
            NEW_TOKAC,
            dir_no_epoch / "main.tk",
            [
                "--encap-epoch=0.18",
                "--workspace-node",
                "tki05-ws-v1",
                "--workspace-root",
                str(dir_no_epoch),
            ],
            (
                "E0901",
                "Incompatible or stale interface file",
                "Interface encap_epoch mismatch (expected 0.18, got <missing>)",
            ),
        )

        return {
            "v5_meta": meta_v5,
            "v6_epoch018_meta": meta_v6,
            "v6_epoch019_meta": meta_e019,
            "missing_encap_epoch_rejected": True,
        }


def verify_tki06_same_source_hash_cache_invalidation(
    recorder: ReceiptRecorder,
) -> dict[str, Any]:
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        ws = base / "ws"
        build_cache = base / "build_cache"
        ws.mkdir()
        (build_cache / "interfaces").mkdir(parents=True)
        (build_cache / "objects").mkdir(parents=True)

        provider_tk = ws / "plain.tk"
        provider_tki = ws / "plain.tki"
        consumer_tk = ws / "main.tk"
        provider_source = (
            "pub shape Plain(x: i32)\n"
            "pub fn make_plain(v: i32) -> Plain { return Plain(x = v) }\n"
            "pub fn get_x(p: Plain) -> i32 { return p.x }\n"
        )
        provider_tk.write_text(provider_source, encoding="utf-8")
        consumer_tk.write_text(
            "import ./plain::{Plain, make_plain, get_x}\n"
            "fn main() -> i32 {\n"
            "  auto p = make_plain(7)\n"
            "  return get_x(p) - 7\n"
            "}\n",
            encoding="utf-8",
        )

        # Step 1: Compile provider under --encap-epoch=0.18 (adjacent .tki)
        recorder.run(
            "TKI06_1_build_provider_epoch018",
            [
                str(NEW_TOKAC),
                "--encap-epoch=0.18",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                "-c",
                "-o",
                str(ws / "plain.o"),
                str(provider_tk),
            ],
        )
        meta_018 = parse_tki_meta(provider_tki)

        # Consumer under 0.18 with -c prefers and hits adjacent plain.tki
        dep_018 = recorder.run(
            "TKI06_2_consumer_hit_adjacent_tki_epoch018",
            [
                str(NEW_TOKAC),
                "--encap-epoch=0.18",
                "-c",
                "--dump-dependencies=json",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                str(consumer_tk),
            ],
        )
        rec_018 = records_by_basename(json.loads(dep_018.stdout))["plain.tki"]
        assert rec_018["kind"] == "interface", rec_018
        assert rec_018["fallback_triggered"] is False, rec_018
        assert rec_018["cache_status"] == "Ok", rec_018

        # Step 2: Switch to default 0.19 with identical .tk bytes (same SourceHash)
        # Consumer must invalidate the 0.18 .tki and fallback to plain.tk
        dep_019_fallback = recorder.run(
            "TKI06_3_consumer_invalidates_018_tki_and_falls_back_to_source_under_019",
            [
                str(NEW_TOKAC),
                "-c",
                "--dump-dependencies=json",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                str(consumer_tk),
            ],
        )
        rec_019_fb = records_by_basename(json.loads(dep_019_fallback.stdout))["plain.tk"]
        assert rec_019_fb["kind"] == "source", rec_019_fb
        assert rec_019_fb["fallback_triggered"] is True, rec_019_fb
        assert rec_019_fb["cache_status"] == "FormatVersionMismatch", rec_019_fb
        assert (
            "Interface encap_epoch mismatch (expected 0.19, got 0.18)"
            in rec_019_fb["cache_status_reason"]
        ), rec_019_fb
        assert rec_019_fb["source_hash"] == meta_018["source_hash"], (
            rec_019_fb,
            meta_018,
        )

        # Step 3: Recompile provider under default 0.19 (without changing plain.tk)
        recorder.run(
            "TKI06_4_rebuild_provider_epoch019_same_source",
            [
                str(NEW_TOKAC),
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                "-c",
                "-o",
                str(ws / "plain.o"),
                str(provider_tk),
            ],
        )
        meta_019 = parse_tki_meta(provider_tki)

        # Same source_hash, different encap_epoch, different replay_surface_hash!
        assert meta_019["source_hash"] == meta_018["source_hash"], (meta_018, meta_019)
        assert meta_019["encap_epoch"] == "0.19", meta_019
        assert meta_019["replay_surface_hash"] != meta_018["replay_surface_hash"], (
            meta_018,
            meta_019,
        )

        # Consumer under default 0.19 now hits the newly regenerated 0.19 .tki
        dep_019_hit = recorder.run(
            "TKI06_5_consumer_hits_regenerated_019_tki",
            [
                str(NEW_TOKAC),
                "-c",
                "--dump-dependencies=json",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                str(consumer_tk),
            ],
        )
        rec_019_hit = records_by_basename(json.loads(dep_019_hit.stdout))["plain.tki"]
        assert rec_019_hit["kind"] == "interface", rec_019_hit
        assert rec_019_hit["fallback_triggered"] is False, rec_019_hit
        assert rec_019_hit["cache_status"] == "Ok", rec_019_hit

        # Step 4: Verify TOKA_BUILD_DIR + TOKA_USE_LIB_CACHE=1 (.tki + .tke sidecar) invalidation across epochs
        if provider_tki.exists():
            provider_tki.unlink()
        canonical_tk = str(provider_tk.resolve())
        fnv = 14695981039346656037
        for b in canonical_tk.encode("utf-8"):
            fnv ^= b
            fnv = (fnv * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        stem = f"{fnv:016x}"
        cached_tki = build_cache / "interfaces" / f"{stem}.tki"
        cached_tke = build_cache / "interfaces" / f"{stem}.tke"
        cached_obj = build_cache / "objects" / f"{stem}.o"

        cache_env = {
            "TOKA_BUILD_DIR": str(build_cache),
            "TOKA_USE_LIB_CACHE": "1",
        }

        # Populate TOKA_BUILD_DIR under default 0.19 first to capture 0.19 .tke digest
        recorder.run(
            "TKI06_6a_populate_build_cache_epoch019",
            [
                str(NEW_TOKAC),
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                "-c",
                "-o",
                str(cached_obj),
                str(provider_tk),
            ],
            env_overrides={"TOKA_BUILD_DIR": str(build_cache)},
        )
        tke_019 = json.loads(cached_tke.read_text(encoding="utf-8"))

        # Re-emit 0.18 .tki/.o/.tke directly into TOKA_BUILD_DIR
        recorder.run(
            "TKI06_6b_populate_build_cache_epoch018",
            [
                str(NEW_TOKAC),
                "--encap-epoch=0.18",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                "-c",
                "-o",
                str(cached_obj),
                str(provider_tk),
            ],
            env_overrides={"TOKA_BUILD_DIR": str(build_cache)},
        )
        tke_018 = json.loads(cached_tke.read_text(encoding="utf-8"))
        assert tke_019["evidence_sha256"] != tke_018["evidence_sha256"], (
            tke_018,
            tke_019,
        )
        # Hit under 0.18
        cache_hit_018 = recorder.run(
            "TKI06_7_build_cache_hit_under_018",
            [
                str(NEW_TOKAC),
                "--encap-epoch=0.18",
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                str(consumer_tk),
            ],
            env_overrides=cache_env,
        )
        cache_rec_018 = records_by_basename(json.loads(cache_hit_018.stdout))[f"{stem}.tki"]
        assert cache_rec_018["kind"] == "interface", cache_rec_018
        assert cache_rec_018["fallback_triggered"] is False, cache_rec_018
        assert cache_rec_018["cache_status"] == "Ok", cache_rec_018

        # Invalidate & fallback to .tk under default 0.19 when .tk is present
        cache_fb_019 = recorder.run(
            "TKI06_8_build_cache_invalidated_and_falls_back_under_019",
            [
                str(NEW_TOKAC),
                "--check-only",
                "--dump-dependencies=json",
                "--workspace-node",
                "tki06-ws-v1",
                "--workspace-root",
                str(ws),
                str(consumer_tk),
            ],
            env_overrides=cache_env,
        )
        cache_rec_019 = records_by_basename(json.loads(cache_fb_019.stdout))["plain.tk"]
        assert cache_rec_019["kind"] == "source", cache_rec_019
        assert cache_rec_019["fallback_triggered"] is True, cache_rec_019
        assert cache_rec_019["cache_status"] == "FormatVersionMismatch", cache_rec_019

        # Physically hide .tk source and restore adjacent 0.18 .tki: default 0.19 must reject with E0901
        shutil.copy2(build_cache / "interfaces" / f"{stem}.tki", provider_tki)
        provider_tk.rename(ws / "plain.tk.hidden")
        for mode_name, mode_flags, product in (
            ("check", ["--check-only"], None),
            ("obj", ["-c", "-o", str(ws / "out.o")], ws / "out.o"),
            ("llvm", ["-c", "--emit-llvm", "-o", str(ws / "out.ll")], ws / "out.ll"),
        ):
            res = recorder.run(
                f"TKI06_9_build_cache_sourceless_rejected_{mode_name}",
                [
                    str(NEW_TOKAC),
                    "--workspace-node",
                    "tki06-ws-v1",
                    "--workspace-root",
                    str(ws),
                    *mode_flags,
                    str(consumer_tk),
                ],
                env_overrides=cache_env,
                expect_rc=1,
            )
            assert "E0901" in res.stderr, res.stderr
            assert "Interface encap_epoch mismatch (expected 0.19, got 0.18)" in res.stderr
            if product:
                assert not product.exists(), product

        return {
            "source_hash": meta_018["source_hash"],
            "replay_surface_hash_018": meta_018["replay_surface_hash"],
            "replay_surface_hash_019": meta_019["replay_surface_hash"],
            "tke_sha256_018": tke_018["evidence_sha256"],
            "tke_sha256_019": tke_019["evidence_sha256"],
        }


def verify_consumer_regression_gates(recorder: ReceiptRecorder) -> None:
    gates: list[tuple[str, list[str]]] = [
        ("unit_semantic_manifest_envelope", [str(ROOT / "build" / "bin" / "toka_semantic_manifest_envelope")]),
        ("unit_semantic_manifest_attestation", [str(ROOT / "build" / "bin" / "toka_semantic_manifest_attestation")]),
        ("test_encap_slice0_audit", [sys.executable, "tools/scripts/test_encap_slice0_audit.py"]),
        ("test_encap_slice5_tki_audit", [sys.executable, "tools/scripts/test_encap_slice5_tki_audit.py"]),
        (
            "test_receiver_poststate_tki",
            [sys.executable, "tools/scripts/test_receiver_poststate_tki.py", "--build-dir", "build"],
        ),
        (
            "test_018_stage1_verification",
            [sys.executable, "tools/scripts/test_018_stage1_verification.py", "--build-dir", "build"],
        ),
        (
            "test_compositional_convergence",
            [sys.executable, "tools/scripts/test_compositional_convergence.py", "--build-dir", "build"],
        ),
        ("test_tki_cache_validation", ["bash", "tools/scripts/test_tki_cache_validation.sh"]),
        ("test_tki_unsafe_revalidation", ["bash", "tools/scripts/test_tki_unsafe_revalidation.sh"]),
        (
            "test_tki_excluded_syntax_revalidation",
            ["bash", "tools/scripts/test_tki_excluded_syntax_revalidation.sh"],
        ),
        ("test_outcome_body_recheck", ["bash", "tools/scripts/test_outcome_body_recheck.sh"]),
    ]
    for label, argv in gates:
        recorder.run(f"GATE_{label}", argv, cwd=ROOT, expect_rc=0)


def main() -> int:
    if not NEW_TOKAC.is_file():
        raise RuntimeError(f"missing new tokac binary: {NEW_TOKAC}")
    if not PUBLISHED_V0181_TOKAC.is_file():
        raise RuntimeError(f"missing published v0.18.1 tokac binary: {PUBLISHED_V0181_TOKAC}")

    recorder = ReceiptRecorder()
    identity_summary = verify_identity_subitems(recorder)
    tki05_summary = verify_tki05_bidirectional_isolation(recorder)
    tki06_summary = verify_tki06_same_source_hash_cache_invalidation(recorder)
    verify_consumer_regression_gates(recorder)

    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    receipt_payload = {
        "stage": "0.19-encap-field-visibility-stage1-foundation",
        "scope_boundary": (
            "Stage 1 ONLY: authoritative package/module identity binding (R1/PLAN-1), "
            "TKI format_version 6 + --encap-epoch=0.18|0.19 bidirectional isolation (TKI-05/R4), "
            "and same-SourceHash cross-epoch cache invalidation (TKI-06/R4). "
            "Stage 2 (`pkg` keyword + default-private field visibility Sema) is NOT claimed here."
        ),
        "new_tokac_path": str(NEW_TOKAC),
        "new_tokac_sha256": sha256_file(NEW_TOKAC),
        "published_v0181_tokac_path": str(PUBLISHED_V0181_TOKAC),
        "published_v0181_tokac_sha256": sha256_file(PUBLISHED_V0181_TOKAC),
        "identity_summary": identity_summary,
        "tki05_summary": tki05_summary,
        "tki06_summary": tki06_summary,
        "command_count": len(recorder.commands),
        "commands": recorder.commands,
    }
    receipt_path = ARTIFACT_DIR / "receipts.json"
    receipt_path.write_text(
        json.dumps(receipt_payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"0.19 Stage 1 encap foundation audit: PASSED ({len(recorder.commands)} commands recorded)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
