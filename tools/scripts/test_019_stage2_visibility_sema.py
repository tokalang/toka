#!/usr/bin/env python3
"""Stage 2 & Stage 4.1 verification suite for Toka 0.19 encapsulation & visibility contract.

Runs via the default compiler entry (TOKA_DEFAULT_ENCAP_EPOCH = "0.19", no --encap-epoch=0.19 flag)
and verifies:
  - SP-01 .. SP-08 (same-module, same-package, cross-package field & decl visibility,
    construct/destructure/elision, ref/mutability orthogonality, outer gating,
    PLAN-1 Fail-Closed, PLAN-2 effective visibility closure)
  - XP-01 .. XP-09 (@Encap grant removal, drop capsule, no private/prv keyword,
    cross-package pkg rejection, pub import re-export ban, R2 enum/tuple/array boundaries)
  - PG-01 .. PG-04 (generic definition-module authority, whole-value pass/copy/drop)
  - TKI-01 .. TKI-04 (.tki export & sourceless round-trip consumption for pub/pkg/private fields & decls)
All negative controls verify 3-mode rejection (--check-only, -c, --emit-llvm) with exit 1,
expected diagnostic, and zero leftover artifacts.
All positive controls verify 3-mode compilation (--check-only, -c, --emit-llvm), linking,
and native binary execution with exit 0.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TOKAC = REPO_ROOT / "build" / "bin" / "tokac"


def run_cmd(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd or REPO_ROOT),
        text=True,
        capture_output=True,
    )


def expect_pass(
    name: str,
    cmd: list[str],
    cwd: Path | None = None,
    extra_link_objs: list[Path] | None = None,
) -> dict:
    """If cmd is a --check-only invocation on a .tk file, upgrade to full 3-mode + link + native run."""
    if "--check-only" in cmd and cmd[-1].endswith(".tk"):
        src_path = Path(cmd[-1])
        base_args = [arg for arg in cmd[1:-1] if arg != "--check-only"]
        tokac_bin = cmd[0]
        out_stem = src_path.parent / f"__ stage4_{name}"
        out_stem = src_path.parent / f"__stage4_{name}"
        obj_out = out_stem.with_suffix(".o")
        ll_out = out_stem.with_suffix(".ll")
        exe_out = out_stem.with_suffix(".bin")

        mode_records = []
        for mode_label, mode_flags, prod in (
            ("check_only", ["--check-only"], None),
            ("compile_obj", ["-c", "-o", str(obj_out)], obj_out),
            ("emit_llvm", ["-c", "--emit-llvm", "-o", str(ll_out)], ll_out),
        ):
            if prod and prod.exists():
                prod.unlink()
            mcmd = [tokac_bin, *base_args, *mode_flags, str(src_path)]
            res = run_cmd(mcmd, cwd)
            if res.returncode != 0:
                raise AssertionError(
                    f"[FAIL] {name} ({mode_label}) expected PASS (exit 0), got {res.returncode}\n"
                    f"CMD: {' '.join(mcmd)}\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}"
                )
            if prod and not prod.exists():
                raise AssertionError(
                    f"[FAIL] {name} ({mode_label}) did not produce expected artifact {prod}"
                )
            mode_records.append(
                {
                    "mode": mode_label,
                    "exit_code": res.returncode,
                    "cmd": mcmd,
                }
            )
            if prod and prod.exists():
                prod.unlink()

        # Link and execute native binary when not an explicit 0.18 check without main execution need
        if exe_out.exists():
            exe_out.unlink()
        link_inputs = [str(src_path)]
        if extra_link_objs:
            link_inputs.extend(str(p) for p in extra_link_objs)
        link_cmd = [tokac_bin, *base_args, *link_inputs, "-o", str(exe_out)]
        link_res = run_cmd(link_cmd, cwd)
        if link_res.returncode != 0 or not exe_out.exists():
            raise AssertionError(
                f"[FAIL] {name} (link) expected PASS (exit 0), got {link_res.returncode}\n"
                f"CMD: {' '.join(link_cmd)}\nSTDOUT:\n{link_res.stdout}\nSTDERR:\n{link_res.stderr}"
            )
        exec_res = run_cmd([str(exe_out)], cwd)
        if exe_out.exists():
            exe_out.unlink()
        if exec_res.returncode != 0:
            raise AssertionError(
                f"[FAIL] {name} (native_exec) expected exit 0, got {exec_res.returncode}\n"
                f"STDOUT:\n{exec_res.stdout}\nSTDERR:\n{exec_res.stderr}"
            )
        mode_records.append(
            {
                "mode": "link_and_native_exec",
                "link_exit_code": link_res.returncode,
                "exec_exit_code": exec_res.returncode,
                "cmd": link_cmd,
            }
        )
        print(f"[PASS] {name} (3-mode + link + native exec rc=0)")
        return {
            "case": name,
            "expected": "pass",
            "exit_code": 0,
            "modes": mode_records,
        }

    res = run_cmd(cmd, cwd)
    if res.returncode != 0:
        raise AssertionError(
            f"[FAIL] {name} expected PASS (exit 0), got {res.returncode}\n"
            f"CMD: {' '.join(cmd)}\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}"
        )
    print(f"[PASS] {name}")
    return {
        "case": name,
        "expected": "pass",
        "exit_code": res.returncode,
        "cmd": cmd,
        "stdout": res.stdout,
        "stderr": res.stderr,
    }


def expect_fail(
    name: str,
    cmd: list[str],
    expected_substr: str | None = None,
    cwd: Path | None = None,
) -> dict:
    """Run negative control across all 3 compiler modes (--check-only, -c, --emit-llvm)
    and verify exit_code == 1, expected diagnostic substring, and zero leftover artifacts."""
    if "--check-only" in cmd and cmd[-1].endswith(".tk"):
        src_path = Path(cmd[-1])
        base_args = [arg for arg in cmd[1:-1] if arg != "--check-only"]
        tokac_bin = cmd[0]
        out_stem = src_path.parent / f"__stage4_neg_{name}"
        obj_out = out_stem.with_suffix(".o")
        ll_out = out_stem.with_suffix(".ll")

        mode_records = []
        last_stderr = ""
        for mode_label, mode_flags, prod in (
            ("check_only", ["--check-only"], None),
            ("compile_obj", ["-c", "-o", str(obj_out)], obj_out),
            ("emit_llvm", ["-c", "--emit-llvm", "-o", str(ll_out)], ll_out),
        ):
            if prod and prod.exists():
                prod.unlink()
            mcmd = [tokac_bin, *base_args, *mode_flags, str(src_path)]
            res = run_cmd(mcmd, cwd)
            combined = (res.stdout or "") + "\n" + (res.stderr or "")
            if res.returncode != 1:
                raise AssertionError(
                    f"[FAIL] {name} ({mode_label}) expected FAIL (exit 1), got {res.returncode}\n"
                    f"CMD: {' '.join(mcmd)}\nOUTPUT:\n{combined}"
                )
            if expected_substr and expected_substr not in combined:
                raise AssertionError(
                    f"[FAIL] {name} ({mode_label}) failed with {res.returncode}, "
                    f"but missing expected diagnostic '{expected_substr}'\n"
                    f"CMD: {' '.join(mcmd)}\nOUTPUT:\n{combined}"
                )
            if prod and prod.exists():
                raise AssertionError(
                    f"[FAIL] {name} ({mode_label}) left residual artifact {prod} after failure"
                )
            last_stderr = res.stderr
            mode_records.append(
                {
                    "mode": mode_label,
                    "exit_code": res.returncode,
                    "zero_residual_artifact": True,
                    "cmd": mcmd,
                }
            )
        print(f"[PASS] {name} (3-mode rejected as expected, zero artifacts)")
        return {
            "case": name,
            "expected": "fail",
            "exit_code": 1,
            "expected_substr": expected_substr or "",
            "modes": mode_records,
            "stderr_sample": last_stderr,
        }

    res = run_cmd(cmd, cwd)
    combined = (res.stdout or "") + "\n" + (res.stderr or "")
    if res.returncode == 0:
        raise AssertionError(
            f"[FAIL] {name} expected FAIL (non-zero), got 0\n"
            f"CMD: {' '.join(cmd)}\nOUTPUT:\n{combined}"
        )
    if expected_substr and expected_substr not in combined:
        raise AssertionError(
            f"[FAIL] {name} failed with {res.returncode}, but missing expected diagnostic '{expected_substr}'\n"
            f"CMD: {' '.join(cmd)}\nOUTPUT:\n{combined}"
        )
    print(f"[PASS] {name} (rejected as expected)")
    return {
        "case": name,
        "expected": "fail",
        "exit_code": res.returncode,
        "expected_substr": expected_substr or "",
        "cmd": cmd,
        "stdout": res.stdout,
        "stderr": res.stderr,
    }


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.strip() + "\n", encoding="utf-8")


def main() -> int:
    tokac = Path(os.environ.get("TOKAC", str(DEFAULT_TOKAC))).resolve()
    if not tokac.exists():
        print(f"tokac not found at {tokac}", file=sys.stderr)
        return 1

    receipts_out: Path | None = None
    if len(sys.argv) >= 3 and sys.argv[1] == "--receipts-out":
        receipts_out = Path(sys.argv[2]).resolve()

    receipts: list[dict] = []
    tmp = Path(tempfile.mkdtemp(prefix="toka_019_stage2_"))
    try:
        # ------------------------------------------------------------------
        # 1. SP-01: Same-module access (pub / pkg / private fields & methods)
        # ------------------------------------------------------------------
        sp01_dir = tmp / "sp01_same_mod"
        sp01 = sp01_dir / "main.tk"
        write_text(
            sp01,
            """
pub shape Account(pub id: i32, pkg branch: i32, balance: i32 = 100)

impl Account {
    pub fn make(id: i32, branch: i32, bal: i32) -> Account {
        return Account(id = id, branch = branch, balance = bal)
    }
    pkg fn adjust(self#, delta: i32) {
        self.balance = self.balance + delta
    }
    fn secret_peek(self) -> i32 {
        return self.balance
    }
    pub fn total(self) -> i32 {
        return self.id + self.branch + self.secret_peek()
    }
}

shape Helper(tag: i32)
impl Helper {
    fn inspect(self, acc: Account) -> i32 {
        return self.tag + acc.id + acc.branch + acc.balance
    }
}

fn main() -> i32 {
    auto acc# = Account::make(1, 2, 10)
    acc#.adjust(5)
    auto &r = &(acc.balance)
    auto h = Helper(tag = r)
    auto Account(a = .id, b = .branch, c = .balance) = acc
    if a + b + c + h.inspect(acc) != 51 {
        return 1
    }
    return 0
}
""",
        )
        receipts.append(
            expect_pass(
                "SP-01_same_module_full_access",
                [
                    str(tokac),
                    "--check-only",
                    "--workspace-node",
                    "pkg-sp01-v1",
                    "--workspace-root",
                    str(sp01_dir),
                    str(sp01),
                ],
            )
        )
        # PLAN-1 check: without authoritative workspace coordinate, pkg access fails closed even in same module!
        receipts.append(
            expect_fail(
                "PLAN-1_missing_coordinate_pkg_access_fail_closed",
                [str(tokac), "--check-only", str(sp01)],
                "private",
            )
        )

        # ------------------------------------------------------------------
        # 2. SP-02, SP-04, SP-05, SP-06, SP-07: Same-package multi-module
        # ------------------------------------------------------------------
        pkg_alpha = tmp / "pkg_alpha"
        write_text(
            pkg_alpha / "provider.tk",
            """
pub shape MixedRecord(pub a: i32, pkg b: i32, c: i32 = 9)

impl MixedRecord {
    pub fn make(a: i32, b: i32, c: i32) -> MixedRecord {
        return MixedRecord(a = a, b = b, c = c)
    }
    pkg fn pkg_sum(self) -> i32 {
        return self.a + self.b + self.c
    }
    fn priv_only(self) -> i32 {
        return self.c
    }
}

pub shape AllPkgVisible(pub x: i32, pkg y: i32 = 20)
pkg shape PkgOnlyShape(pub x: i32, pkg y: i32)
shape PrivateShape(pub x: i32)

pkg const PKG_MAGIC: i32 = 42
const PRIV_MAGIC: i32 = 7

pkg fn pkg_helper(v: i32) -> i32 {
    return v + PKG_MAGIC
}

fn priv_helper(v: i32) -> i32 {
    return v + PRIV_MAGIC
}
""",
        )

        write_text(
            pkg_alpha / "same_pkg_ok.tk",
            """
import ./provider::{MixedRecord, AllPkgVisible, PkgOnlyShape, PKG_MAGIC, pkg_helper}

fn main() -> i32 {
    auto m# = MixedRecord::make(1, 2, 3)
    m.a = m.a + 0
    m.b = m.b + 0
    auto &rb = &(m.b)
    auto sum1 = m.a + rb + m.pkg_sum()
    auto v1 = AllPkgVisible(x = 10, y = 20)
    auto v2 = AllPkgVisible(x = 10, ..)
    auto p = PkgOnlyShape(x = 3, y = 4)
    auto AllPkgVisible(dx = .x, dy = .y) = v1
    if sum1 + v2.y + p.x + p.y + dx + dy + pkg_helper(PKG_MAGIC) != 150 {
        return 1
    }
    return 0
}
""",
        )
        alpha_ws_args = [
            "--workspace-node",
            "pkg-alpha-v1",
            "--workspace-root",
            str(pkg_alpha),
        ]
        receipts.append(
            expect_pass(
                "SP-02_04_05_06_07_same_pkg_allowed",
                [str(tokac), "--check-only", *alpha_ws_args, str(pkg_alpha / "same_pkg_ok.tk")],
            )
        )

        write_text(
            pkg_alpha / "same_pkg_read_priv_field_bad.tk",
            """
import ./provider::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord::make(1, 2, 3)
    return m.c
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-02_same_pkg_read_priv_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_read_priv_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_alpha / "same_pkg_ref_priv_field_bad.tk",
            """
import ./provider::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord::make(1, 2, 3)
    auto &rc = &(m.c)
    return rc
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-06_same_pkg_borrow_ref_priv_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_ref_priv_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_alpha / "same_pkg_construct_with_priv_field_bad.tk",
            """
import ./provider::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord(a = 1, b = 2, c = 3)
    return m.a
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-04_same_pkg_direct_construct_priv_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_construct_with_priv_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_alpha / "same_pkg_elision_bypass_priv_field_bad.tk",
            """
import ./provider::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord(a = 1, b = 2, ..)
    return m.a
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-05_same_pkg_elision_construct_priv_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_elision_bypass_priv_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_alpha / "same_pkg_destructure_priv_field_bad.tk",
            """
import ./provider::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord::make(1, 2, 3)
    auto MixedRecord(x = .a, y = .b, z = .c) = m
    return x + y + z
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-05_same_pkg_destructure_priv_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_destructure_priv_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_alpha / "same_pkg_import_priv_shape_bad.tk",
            """
import ./provider::{PrivateShape}
fn main() -> i32 {
    return 0
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-07_same_pkg_import_priv_shape_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *alpha_ws_args,
                    str(pkg_alpha / "same_pkg_import_priv_shape_bad.tk"),
                ],
                "not visible",
            )
        )

        # ------------------------------------------------------------------
        # 3. SP-03, XP-06, SP-07: Cross-package visibility isolation
        # ------------------------------------------------------------------
        pkg_beta = tmp / "pkg_beta"
        write_text(
            pkg_alpha / "mod.tk",
            """
pub import ./provider::{MixedRecord, AllPkgVisible}
pub shape PublicAllPub(pub x: i32, pub y: i32 = 5)
""",
        )

        write_text(
            pkg_beta / "ext_ok.tk",
            """
import alpha::{MixedRecord, PublicAllPub}
fn main() -> i32 {
    auto m = MixedRecord::make(10, 20, 30)
    auto p1 = PublicAllPub(x = 1, y = 2)
    auto p2 = PublicAllPub(x = 3, ..)
    auto PublicAllPub(dx = .x, dy = .y) = p1
    if m.a + p2.y + dx + dy != 18 {
        return 1
    }
    return 0
}
""",
        )
        beta_cross_args = [
            "--workspace-node",
            "pkg-beta-v1",
            "--workspace-root",
            str(pkg_beta),
            "--pkg",
            f"alpha={pkg_alpha / 'mod.tk'}",
            "--pkg-node",
            "alpha=pkg-alpha-v1",
            "--pkg",
            f"alpha_prov={pkg_alpha / 'provider.tk'}",
            "--pkg-node",
            "alpha_prov=pkg-alpha-v1",
            "--pkg",
            f"alpha_gen={pkg_alpha / 'generic_api.tk'}",
            "--pkg-node",
            "alpha_gen=pkg-alpha-v1",
        ]
        receipts.append(
            expect_pass(
                "SP-03_cross_pkg_pub_only_allowed",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "ext_ok.tk"),
                ],
            )
        )

        write_text(
            pkg_beta / "ext_read_pkg_field_bad.tk",
            """
import alpha::{MixedRecord}
fn main() -> i32 {
    auto m = MixedRecord::make(10, 20, 30)
    return m.b
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-03_cross_pkg_read_pkg_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "ext_read_pkg_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_beta / "ext_construct_pkg_field_bad.tk",
            """
import alpha::{AllPkgVisible}
fn main() -> i32 {
    auto v = AllPkgVisible(x = 1, ..)
    return v.x
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-04_05_cross_pkg_elision_construct_pkg_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "ext_construct_pkg_field_bad.tk"),
                ],
                "private",
            )
        )

        write_text(
            pkg_beta / "ext_import_pkg_decl_bad.tk",
            """
import alpha_prov::{pkg_helper}
fn main() -> i32 {
    return pkg_helper(1)
}
""",
        )
        receipts.append(
            expect_fail(
                "XP-06_cross_pkg_import_pkg_fn_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "ext_import_pkg_decl_bad.tk"),
                ],
                "not visible",
            )
        )

        write_text(
            pkg_beta / "ext_import_pkg_shape_bad.tk",
            """
import alpha_prov::{PkgOnlyShape}
fn main() -> i32 {
    return 0
}
""",
        )
        receipts.append(
            expect_fail(
                "SP-07_XP-06_cross_pkg_import_pkg_shape_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "ext_import_pkg_shape_bad.tk"),
                ],
                "not visible",
            )
        )

        # ------------------------------------------------------------------
        # 4. SP-08 & PLAN-2: Effective Visibility Closure (outer x inner)
        # ------------------------------------------------------------------
        plan2_ok = tmp / "plan2" / "closure_ok.tk"
        write_text(
            plan2_ok,
            """
shape Secret(value: i32)
pkg shape PkgData(pkg code: i32)
pub shape PubData(pub code: i32)

// Case 1: private shape + pub field holding private Secret -> effVis = Private -> OK
shape LocalBox(pub value: Secret)

// Case 2: private shape + pkg field holding private Secret -> effVis = Private -> OK
shape LocalPkgBox(pkg value: Secret)

// Case 3: pkg shape + pub field holding pkg PkgData -> effVis = Package -> OK
pkg shape PkgBox(pub value: PkgData)

// Case 5: pub shape + private field holding private Secret & pkg PkgData -> effVis = Private -> OK
pub shape PublicBox(value: Secret, internal_pkg: PkgData, pub exposed: PubData)

fn main() -> i32 {
    return 0
}
""",
        )
        receipts.append(
            expect_pass(
                "SP-08_PLAN-2_allowed_combinations_1_2_3_5",
                [str(tokac), "--check-only", str(plan2_ok)],
            )
        )

        plan2_case4_bad = tmp / "plan2" / "case4_pkg_shape_pub_secret_bad.tk"
        write_text(
            plan2_case4_bad,
            """
shape Secret(value: i32)
pkg shape PkgBoxBad(pub value: Secret)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "SP-08_PLAN-2_case4_pkg_shape_pub_field_secret_rejected",
                [str(tokac), "--check-only", str(plan2_case4_bad)],
                "narrower visibility",
            )
        )

        plan2_case6a_bad = tmp / "plan2" / "case6a_pub_shape_pkg_secret_bad.tk"
        write_text(
            plan2_case6a_bad,
            """
shape Secret(value: i32)
pub shape PublicBoxPkgBad(pkg value: Secret)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "SP-08_PLAN-2_case6a_pub_shape_pkg_field_secret_rejected",
                [str(tokac), "--check-only", str(plan2_case6a_bad)],
                "narrower visibility",
            )
        )

        plan2_case6b_bad = tmp / "plan2" / "case6b_pub_shape_pub_pkgdata_bad.tk"
        write_text(
            plan2_case6b_bad,
            """
pkg shape PkgData(pkg code: i32)
pub shape PublicBoxPubBad(pub value: PkgData)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "SP-08_PLAN-2_case6b_pub_shape_pub_field_pkgdata_rejected",
                [str(tokac), "--check-only", str(plan2_case6b_bad)],
                "narrower visibility",
            )
        )

        plan2_generic_tree_bad = tmp / "plan2" / "generic_tree_bad.tk"
        write_text(
            plan2_generic_tree_bad,
            """
shape Secret(value: i32)
pub shape Wrapper<T>(pub item: T)
pub shape BadNested(pkg wrapped: Wrapper<Secret>)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "SP-08_PLAN-2_recursive_generic_type_tree_rejected",
                [str(tokac), "--check-only", str(plan2_generic_tree_bad)],
                "narrower visibility",
            )
        )

        plan2_pub_fn_pkg_arg_bad = tmp / "plan2" / "pub_fn_pkg_arg_bad.tk"
        write_text(
            plan2_pub_fn_pkg_arg_bad,
            """
pkg shape PkgData(pkg code: i32)
pub fn leaked(x: PkgData) -> i32 { return x.code }
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "SP-08_PLAN-2_pub_fn_exposing_pkg_type_rejected",
                [str(tokac), "--check-only", str(plan2_pub_fn_pkg_arg_bad)],
                "narrower visibility",
            )
        )

        plan2_pub_alias_pkg_bad = tmp / "plan2" / "pub_alias_pkg_bad.tk"
        write_text(
            plan2_pub_alias_pkg_bad,
            """
pkg shape PkgData(pkg code: i32)
pub type LeakedAlias = PkgData
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-07_PLAN-2_pub_type_alias_to_pkg_rejected",
                [str(tokac), "--check-only", str(plan2_pub_alias_pkg_bad)],
                "narrower visibility",
            )
        )

        # ------------------------------------------------------------------
        # 5. XP-01, XP-02, XP-03, XP-04, XP-05, XP-07, XP-08, XP-09, R2
        # ------------------------------------------------------------------
        xp01_bad = tmp / "xp" / "xp01_encap_pub_field_list.tk"
        write_text(
            xp01_bad,
            """
pub shape S(a: i32, b: i32)
impl S@Encap {
    pub a, b
}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-01_encap_pub_field_list_rejected_in_019",
                [str(tokac), "--check-only", str(xp01_bad)],
                "removed in 0.19",
            )
        )
        receipts.append(
            expect_pass(
                "XP-01_encap_pub_field_list_still_accepted_in_018",
                [str(tokac), "--encap-epoch=0.18", "--check-only", str(xp01_bad)],
            )
        )

        xp01_mix_bad = tmp / "xp" / "xp01_mix_inline_and_encap_bad.tk"
        write_text(
            xp01_mix_bad,
            """
pub shape S(pub a: i32, b: i32)
impl S@Encap {
    pub b
}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-01b_mix_inline_vis_and_encap_field_list_rejected_018",
                [str(tokac), "--encap-epoch=0.18", "--check-only", str(xp01_mix_bad)],
                "Cannot mix inline shape field visibility",
            )
        )
        receipts.append(
            expect_fail(
                "XP-01b_mix_inline_vis_and_encap_field_list_rejected_019",
                [str(tokac), "--check-only", str(xp01_mix_bad)],
                "Cannot mix inline shape field visibility",
            )
        )

        xp02_pkg_bad = tmp / "xp" / "xp02_encap_pkg_field.tk"
        write_text(
            xp02_pkg_bad,
            """
pub shape S(a: i32)
impl S@Encap {
    pkg a
}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-02_encap_pkg_grant_rejected",
                [str(tokac), "--check-only", str(xp02_pkg_bad)],
                "removed in 0.19",
            )
        )

        xp03_drop_ok = tmp / "xp" / "xp03_encap_drop_ok.tk"
        write_text(
            xp03_drop_ok,
            """
pub shape S(pub a: i32, secret: i32)
impl S@Encap {
    fn drop(self#) {}
}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_pass(
                "XP-03_encap_private_drop_allowed",
                [str(tokac), "--check-only", str(xp03_drop_ok)],
            )
        )

        xp03_pkg_drop_bad = tmp / "xp" / "xp03_encap_pkg_drop_bad.tk"
        write_text(
            xp03_pkg_drop_bad,
            """
pub shape S(pub a: i32)
impl S@Encap {
    pkg fn drop(self#) {}
}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-03_encap_pkg_drop_rejected",
                [str(tokac), "--check-only", str(xp03_pkg_drop_bad)],
                "drop hook must be private",
            )
        )

        xp04_private_kw_bad = tmp / "xp" / "xp04_private_modifier_bad.tk"
        write_text(
            xp04_private_kw_bad,
            """
shape S(private x: i32)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-04_private_modifier_rejected",
                [str(tokac), "--check-only", str(xp04_private_kw_bad)],
            )
        )

        xp05_dir = tmp / "xp" / "xp05"
        xp05_ident_ok = xp05_dir / "xp05_private_prv_as_identifiers_ok.tk"
        write_text(
            xp05_ident_ok,
            """
shape S(pub private: i32, pkg prv: i32)
fn main() -> i32 {
    auto s = S(private = 10, prv = 20)
    auto private = s.private
    auto prv = s.prv
    return private + prv - 30
}
""",
        )
        receipts.append(
            expect_pass(
                "XP-05_private_and_prv_remain_normal_identifiers",
                [
                    str(tokac),
                    "--check-only",
                    "--workspace-node",
                    "pkg-xp05-v1",
                    "--workspace-root",
                    str(xp05_dir),
                    str(xp05_ident_ok),
                ],
            )
        )

        xp07_pub_reexport_pkg_bad = pkg_alpha / "pub_reexport_pkg_bad.tk"
        write_text(
            xp07_pub_reexport_pkg_bad,
            """
pub import ./provider::{PkgOnlyShape}
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-07_pub_import_reexport_pkg_symbol_rejected",
                [str(tokac), "--check-only", *alpha_ws_args, str(xp07_pub_reexport_pkg_bad)],
                "Cannot publicly re-export non-public symbol",
            )
        )

        xp08_enum_vis_bad = tmp / "xp" / "xp08_enum_inline_vis_bad.tk"
        write_text(
            xp08_enum_vis_bad,
            """
pub shape E(pub A(i32) | B)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-08_R2_enum_variant_inline_visibility_rejected",
                [str(tokac), "--check-only", str(xp08_enum_vis_bad)],
                "enum variants inherit the visibility",
            )
        )

        xp09_tuple_bad = tmp / "xp" / "xp09_tuple_shape_bad.tk"
        write_text(
            xp09_tuple_bad,
            """
pub shape Pair(pub i32, pkg i32)
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_fail(
                "XP-09_R2_tuple_shape_still_hits_E0472",
                [str(tokac), "--check-only", str(xp09_tuple_bad)],
                "E0472",
            )
        )

        xp09_array_ok = tmp / "xp" / "xp09_array_shape_ok.tk"
        write_text(
            xp09_array_ok,
            """
pkg shape Buf = [u8; 64]
fn main() -> i32 { return 0 }
""",
        )
        receipts.append(
            expect_pass(
                "XP-09_R2_canonical_array_shape_with_pkg_allowed",
                [str(tokac), "--check-only", str(xp09_array_ok)],
            )
        )

        # ------------------------------------------------------------------
        # 6. PG-01 .. PG-04: Generics & Whole-Value Operations
        # ------------------------------------------------------------------
        write_text(
            pkg_alpha / "generic_api.tk",
            """
import ./provider::{MixedRecord}

pub shape ResourceWithDrop(pub id: i32, secret: i32)

impl ResourceWithDrop@Encap {
    fn drop(self#) {}
}

impl ResourceWithDrop {
    pub fn make(id: i32, s: i32) -> ResourceWithDrop {
        return ResourceWithDrop(id = id, secret = s)
    }
}

pub fn read_via_generic<T>(rec: MixedRecord, tag: T) -> i32 {
    return rec.a + rec.b
}

pub fn copy_pair<T: @Copy>(val: T) -> T {
    return val
}

pub fn forward_whole<T>(cede val: T) -> T {
    return cede val
}
""",
        )

        write_text(
            pkg_beta / "pg_generic_cross_pkg_ok.tk",
            """
import alpha_prov::{MixedRecord}
import alpha_gen::{ResourceWithDrop, read_via_generic, copy_pair, forward_whole}

fn main() -> i32 {
    auto m = MixedRecord::make(4, 5, 6)
    auto v = read_via_generic<i32>(m, 1)
    auto m_copied = copy_pair<MixedRecord>(m)
    auto m2 = forward_whole<MixedRecord>(cede m)
    auto res = ResourceWithDrop::make(7, 99)
    auto res2 = forward_whole<ResourceWithDrop>(cede res)
    if v + m_copied.a + m2.a + res2.id != 24 {
        return 1
    }
    return 0
}
""",
        )
        receipts.append(
            expect_pass(
                "PG-01_02_04_generic_def_module_authority_and_whole_value_move",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "pg_generic_cross_pkg_ok.tk"),
                ],
            )
        )

        write_text(
            pkg_beta / "pg_external_generic_cannot_access_pkg_bad.tk",
            """
import alpha_prov::{MixedRecord}

fn ext_generic<T>(rec: MixedRecord, x: T) -> i32 {
    return rec.b
}

fn main() -> i32 {
    auto m = MixedRecord::make(1, 2, 3)
    return ext_generic<i32>(m, 0)
}
""",
        )
        receipts.append(
            expect_fail(
                "PG-03_external_generic_cannot_access_pkg_field_rejected",
                [
                    str(tokac),
                    "--check-only",
                    *beta_cross_args,
                    str(pkg_beta / "pg_external_generic_cannot_access_pkg_bad.tk"),
                ],
                "private",
            )
        )

        # ------------------------------------------------------------------
        # 7. TKI-01 .. TKI-04: .tki Export & Sourceless Consumption
        # ------------------------------------------------------------------
        tki_pkg_a = tmp / "tki_pkg_a"
        tki_src = tki_pkg_a / "lib_mod.tk"
        tki_out = tki_pkg_a / "lib_mod.tki"
        tki_obj = tki_pkg_a / "lib_mod.o"
        write_text(
            tki_src,
            """
pub shape ExportedRecord(pub pub_f: i32, pkg pkg_f: i32, priv_f: i32 = 11)

impl ExportedRecord {
    pub fn create(a: i32, b: i32) -> ExportedRecord {
        return ExportedRecord(pub_f = a, pkg_f = b, priv_f = 11)
    }
    pkg fn pkg_read(self) -> i32 {
        return self.pkg_f + self.priv_f
    }
}

pkg shape PkgExported(pub x: i32, pkg y: i32)
pkg const PKG_EXPORT_CONST: i32 = 99
""",
        )
        tki_a_ws_args = [
            "--workspace-node",
            "pkg-tki-a-v1",
            "--workspace-root",
            str(tki_pkg_a),
        ]
        receipts.append(
            expect_pass(
                "TKI-01_02_emit_tki_for_019_visibility",
                [
                    str(tokac),
                    *tki_a_ws_args,
                    "-c",
                    "-o",
                    str(tki_obj),
                    str(tki_src),
                ],
            )
        )
        tki_text = tki_out.read_text(encoding="utf-8")
        assert "// @meta encap_epoch: 0.19" in tki_text, f"Missing encap_epoch: 0.19 in:\n{tki_text}"
        assert "pub shape ExportedRecord(pub pub_f: i32, pkg pkg_f: i32, priv_f: i32 = 11)" in tki_text, (
            f"Unexpected .tki shape export:\n{tki_text}"
        )
        assert "pkg shape PkgExported(pub x: i32, pkg y: i32)" in tki_text, (
            f"Unexpected .tki pkg shape export:\n{tki_text}"
        )
        assert "pkg const PKG_EXPORT_CONST: i32 = 99" in tki_text, (
            f"Unexpected .tki pkg const export:\n{tki_text}"
        )
        assert "// @tki v2 field_graph: ExportedRecord.pub_f = i32 [pub]" in tki_text, (
            f"Missing [pub] field_graph fact:\n{tki_text}"
        )
        assert "// @tki v2 field_graph: ExportedRecord.pkg_f = i32 [pkg]" in tki_text, (
            f"Missing [pkg] field_graph fact:\n{tki_text}"
        )
        assert "// @tki v2 field_graph: ExportedRecord.priv_f = i32 [private]" in tki_text, (
            f"Missing [private] field_graph fact:\n{tki_text}"
        )
        # Physically remove .tk source so consumer strictly loads sourceless .tki
        tki_src.unlink()
        assert not tki_src.exists()

        write_text(
            tki_pkg_a / "same_pkg_tki_consumer_ok.tk",
            """
import ./lib_mod::{ExportedRecord, PkgExported, PKG_EXPORT_CONST}

fn main() -> i32 {
    auto r = ExportedRecord::create(3, 4)
    auto p = PkgExported(x = 1, y = 2)
    if r.pub_f + r.pkg_f + r.pkg_read() + p.x + p.y + PKG_EXPORT_CONST != 124 {
        return 1
    }
    return 0
}
""",
        )
        receipts.append(
            expect_pass(
                "TKI-03_same_pkg_tki_consumer_allows_pub_and_pkg",
                [
                    str(tokac),
                    "--check-only",
                    *tki_a_ws_args,
                    str(tki_pkg_a / "same_pkg_tki_consumer_ok.tk"),
                ],
                extra_link_objs=[tki_obj],
            )
        )

        write_text(
            tki_pkg_a / "same_pkg_tki_consumer_priv_bad.tk",
            """
import ./lib_mod::{ExportedRecord}

fn main() -> i32 {
    auto r = ExportedRecord::create(3, 4)
    return r.priv_f
}
""",
        )
        receipts.append(
            expect_fail(
                "TKI-03_same_pkg_tki_consumer_rejects_priv_field",
                [
                    str(tokac),
                    "--check-only",
                    *tki_a_ws_args,
                    str(tki_pkg_a / "same_pkg_tki_consumer_priv_bad.tk"),
                ],
                "private",
            )
        )

        tki_pkg_b = tmp / "tki_pkg_b"
        tki_b_cross_args = [
            "--workspace-node",
            "pkg-tki-b-v1",
            "--workspace-root",
            str(tki_pkg_b),
            "--pkg",
            f"tki_a={tki_out}",
            "--pkg-node",
            "tki_a=pkg-tki-a-v1",
        ]
        write_text(
            tki_pkg_b / "cross_pkg_tki_consumer_ok.tk",
            """
import tki_a::{ExportedRecord}

fn main() -> i32 {
    auto r = ExportedRecord::create(3, 4)
    if r.pub_f != 3 {
        return 1
    }
    return 0
}
""",
        )
        receipts.append(
            expect_pass(
                "TKI-04_cross_pkg_tki_consumer_allows_pub_field",
                [
                    str(tokac),
                    "--check-only",
                    *tki_b_cross_args,
                    str(tki_pkg_b / "cross_pkg_tki_consumer_ok.tk"),
                ],
                extra_link_objs=[tki_obj],
            )
        )

        write_text(
            tki_pkg_b / "cross_pkg_tki_consumer_pkg_bad.tk",
            """
import tki_a::{ExportedRecord}

fn main() -> i32 {
    auto r = ExportedRecord::create(3, 4)
    return r.pkg_f
}
""",
        )
        receipts.append(
            expect_fail(
                "TKI-04_cross_pkg_tki_consumer_rejects_pkg_field",
                [
                    str(tokac),
                    "--check-only",
                    *tki_b_cross_args,
                    str(tki_pkg_b / "cross_pkg_tki_consumer_pkg_bad.tk"),
                ],
                "private",
            )
        )

        if receipts_out is not None:
            receipts_out.parent.mkdir(parents=True, exist_ok=True)
            receipts_out.write_text(
                json.dumps(
                    {
                        "stage": "4.1",
                        "default_encap_epoch": "0.19",
                        "tokac": str(tokac),
                        "total_checks": len(receipts),
                        "receipts": receipts,
                    },
                    indent=2,
                    ensure_ascii=False,
                )
                + "\n",
                encoding="utf-8",
            )

        print(f"\nAll {len(receipts)} Stage 2/4.1 verification checks PASSED.")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
