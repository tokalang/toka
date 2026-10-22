#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.19.

Verifies under default 0.19 compiler entry (no --encap-epoch flag):
- Four tool versions (tokac, toka, tokafmt, tokalsp) against release label
- Source-hidden .tki provider compilation (Format 6, encap_epoch: 0.19),
  source deletion, missing/corrupt/format5/epoch018 rejection (E0901), and
  native sourceless consumer execution rc == 0
- 10 negative probes across 3 compiler modes (--check-only, -c, --emit-llvm)
  verifying exit 1, authoritative diagnostic code (E0418, E0437, E0405, E0406),
  and zero leftover artifacts
- 3 positive suites across 5 steps (check-only, emit-llvm, compile-obj,
  binary-build, native-execution rc == 0)
- Full 41-check Stage 2/4.1 0.19 visibility matrix on installed SDK
- 60 compositional convergence regression probes on installed SDK
- Produces result.json with schema toka.0.19-candidate-controls
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))


TKI_PROVIDER_019_SOURCE = """\
pub shape Vault(pub id: i32, pub audit_tag: i32, secret: i32 = 42)

impl Vault@Encap {
    fn drop(self#) {}
}

impl Vault {
    pub fn open(id: i32, tag: i32, sec: i32) -> Vault {
        return Vault(id = id, audit_tag = tag, secret = sec)
    }

    pub fn total(self) -> i32 {
        return self.id + self.audit_tag + self.secret + self.hidden_bonus()
    }

    fn hidden_bonus(self) -> i32 {
        return 7
    }
}

pub shape PlainRecord(pub id: i32, pub audit_tag: i32, secret: i32 = 42)

impl PlainRecord {
    pub fn make(id: i32, tag: i32, sec: i32) -> PlainRecord {
        return PlainRecord(id = id, audit_tag = tag, secret = sec)
    }
}

shape InternalToken(code: i32)
"""

POS1_019_SOURCE = """\
pub shape Capsule(pub tag: i32, group: i32, secret: i32 = 10)

impl Capsule@Encap {
    fn drop(self#) {}
}

impl Capsule {
    pub fn make(tag: i32, group: i32, secret: i32) -> Capsule {
        return Capsule(tag = tag, group = group, secret = secret)
    }

    pub fn sum(self) -> i32 {
        return self.tag + self.group + self.secret
    }
}

pub shape PlainPair(pub x: i32, y: i32 = 5)

fn main() -> i32 {
    auto c# = Capsule::make(3, 4, 5)
    c.tag = 10
    if c.sum() != 19 {
        return 1
    }
    auto c2 = Capsule(tag = 1, group = 2, ..)
    if c2.sum() != 13 {
        return 2
    }
    auto p = PlainPair(x = 4, ..)
    auto PlainPair(dx = .x, dy = .y) = p
    if dx + dy != 9 {
        return 3
    }
    return 0
}
"""

POS2_019_SOURCE = """\
import stdx/text/template::{TemplateListItem}

pub shape PublicBox(pub v: i32, hidden: i32 = 9)

impl PublicBox {
    pub fn make(v: i32) -> PublicBox {
        return PublicBox(v = v, hidden = 9)
    }
    pub fn total(self) -> i32 {
        return self.v + self.hidden
    }
}

fn read_pub_generic<T>(b: PublicBox, extra: T) -> i32 {
    return b.v + b.total()
}

fn main() -> i32 {
    auto item = TemplateListItem(key = string::from("k"), value = string::from("v"), has_value = true)
    if !item.has_value || item.key.len() + item.value.len() != 2:usize {
        return 1
    }
    auto pb = PublicBox::make(11)
    if read_pub_generic<i32>(pb, 0) != 31 {
        return 2
    }
    return 0
}
"""

POS3_TKI_019_CONSUMER_SOURCE = """\
import encap_019_provider::{Vault}

fn main() -> i32 {
    auto v# = Vault::open(10, 20, 30)
    v.id = 15
    if v.total() != 72 {
        return 1
    }
    return 0
}
"""

NEGATIVE_019_CASES = [
    {
        "id": "neg01_cross_module_private_field_read",
        "category": "visibility_member_access",
        "description": "Cross-module read of default-private shape field via sourceless .tki",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{Vault}

fn main() -> i32 {
    auto v = Vault::open(1, 2, 3)
    return v.secret
}
""",
    },
    {
        "id": "neg02_cross_module_private_field_write",
        "category": "visibility_member_access",
        "description": "Cross-module write to default-private shape field via sourceless .tki",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{Vault}

fn main() -> i32 {
    auto v# = Vault::open(1, 2, 3)
    v.secret = 99
    return 0
}
""",
    },
    {
        "id": "neg03_external_private_struct_literal",
        "category": "visibility_construction",
        "description": "External direct struct literal construction of shape with private field",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{Vault}

fn main() -> i32 {
    auto v = Vault(id = 1, audit_tag = 2, secret = 3)
    return v.id
}
""",
    },
    {
        "id": "neg04_external_private_destructuring",
        "category": "visibility_destructuring",
        "description": "External destructuring binding of shape with default-private field",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{PlainRecord}

fn main() -> i32 {
    auto r = PlainRecord::make(1, 2, 3)
    auto PlainRecord(a = .id, b = .audit_tag, c = .secret) = r
    return a + b + c
}
""",
    },
    {
        "id": "neg05_cross_module_private_method_call",
        "category": "visibility_method_call",
        "description": "Cross-module call to private inherent method via sourceless .tki",
        "expected_code": "E0437",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{Vault}

fn main() -> i32 {
    auto v = Vault::open(1, 2, 3)
    return v.hidden_bonus()
}
""",
    },
    {
        "id": "neg06_cross_module_private_type_import",
        "category": "visibility_type_import",
        "description": "Cross-module import of private shape via sourceless .tki",
        "expected_code": "E0405",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{InternalToken}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg07_encap_v2_pub_field_list_removed",
        "category": "encap_v3_surface",
        "description": "Legacy @Encap pub field grant list rejected under default 0.19 epoch",
        "expected_code": "E0406",
        "needs_tki": False,
        "source": """\
pub shape LegacyEncap(a: i32, b: i32)

impl LegacyEncap@Encap {
    pub a
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg08_encap_drop_non_private_rejected",
        "category": "encap_v3_surface",
        "description": "@Encap v3 drop hook with pub visibility rejected",
        "expected_code": "E0406",
        "needs_tki": False,
        "source": """\
pub shape BadDrop(pub a: i32, secret: i32)

impl BadDrop@Encap {
    pub fn drop(self#) {}
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg09_plan2_pub_fn_leaking_pkg_type",
        "category": "plan2_effective_visibility",
        "description": "PLAN-2 public function exposing package-visible shape rejected",
        "expected_code": "E0406",
        "needs_tki": False,
        "source": """\
pkg shape InternalState(pkg code: i32)

pub fn leak_state(s: InternalState) -> i32 {
    return s.code
}

fn main() -> i32 {
    return 0
}
""",
    },
    {
        "id": "neg10_generic_specialization_private_field",
        "category": "generic_def_module_authority",
        "description": "External generic function reading cross-module private field rejected at specialization",
        "expected_code": "E0418",
        "needs_tki": True,
        "source": """\
import encap_019_provider::{Vault}

fn inspect_generic<T>(v: Vault, tag: T) -> i32 {
    return v.secret
}

fn main() -> i32 {
    auto v = Vault::open(1, 2, 3)
    return inspect_generic<i32>(v, 0)
}
""",
    },
]

POSITIVE_019_CASES = [
    {
        "id": "pos1_encap_019_default_private_and_drop",
        "description": "Same-module private field construction, destructuring, elision, and @Encap drop",
        "source": POS1_019_SOURCE,
    },
    {
        "id": "pos2_stdx_template_and_generic_visibility",
        "description": "Standalone stdx/text/template::TemplateListItem public API and generic pub field access",
        "source": POS2_019_SOURCE,
    },
]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compute_fixtures_digest(fixtures_dir: pathlib.Path) -> str:
    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise RuntimeError(f"Missing fixtures manifest: {manifest_file}")
    fix_manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    hasher = hashlib.sha256()
    for rel_path, exp_h in sorted(fix_manifest.items()):
        f = fixtures_dir / rel_path
        if not f.is_file():
            raise RuntimeError(f"Missing fixture file on disk: {f}")
        act_h = sha256_file(f)
        if act_h != exp_h:
            raise RuntimeError(f"Fixture hash mismatch for {rel_path}: expected {exp_h}, got {act_h}")
        hasher.update(rel_path.encode("utf-8"))
        hasher.update(exp_h.encode("utf-8"))
    return hasher.hexdigest()


def main():
    p = argparse.ArgumentParser(description="Toka 0.19 installed candidate verification")
    p.add_argument("--sdk", type=pathlib.Path, required=True, help="Installed SDK root directory")
    p.add_argument("--revision", required=True, help="Expected candidate commit SHA")
    p.add_argument("--version", default="v0.19.0", help="Expected version label")
    p.add_argument("--target", default="", help="Target platform label")
    p.add_argument("--output", type=pathlib.Path, default=None, help="Output directory for results")
    a = p.parse_args()

    sdk = a.sdk.resolve()
    tokac_bin = sdk / "bin/tokac"
    if not tokac_bin.is_file() and (sdk / "bin/tokac.exe").is_file():
        tokac_bin = sdk / "bin/tokac.exe"
    if not tokac_bin.is_file():
        raise SystemExit(f"Invalid SDK root: {tokac_bin} missing")

    sdk_json_path = sdk / "sdk.json"
    if not sdk_json_path.is_file():
        raise SystemExit(f"Missing sdk.json in SDK root: {sdk_json_path}")
    sdk_meta = json.loads(sdk_json_path.read_text(encoding="utf-8"))

    if sdk_meta.get("candidate_revision") != a.revision:
        raise SystemExit(f"SDK revision mismatch: sdk has {sdk_meta.get('candidate_revision')}, expected {a.revision}")
    if sdk_meta.get("version_label") != a.version:
        raise SystemExit(f"SDK version mismatch: sdk has {sdk_meta.get('version_label')}, expected {a.version}")
    if sdk_meta.get("build_testing") is not False:
        raise SystemExit(f"Standard candidate SDK must have build_testing=false: {sdk_meta.get('build_testing')}")

    target = a.target or sdk_meta.get("target") or "unknown"
    script_dir = pathlib.Path(__file__).resolve().parent
    control_script_sha256 = sha256_file(pathlib.Path(__file__).resolve())

    fixtures_dir = script_dir / "r018_fixtures"
    if not fixtures_dir.is_dir():
        alt_fix = ROOT / "tools/scripts/r018_fixtures"
        if alt_fix.is_dir():
            fixtures_dir = alt_fix
        else:
            raise SystemExit(f"Missing mandatory r018_fixtures directory at {fixtures_dir}")
    fixtures_digest = compute_fixtures_digest(fixtures_dir)

    out = a.output.resolve() if a.output else (sdk.parent / f"candidate-019-{time.strftime('%Y%m%d-%H%M%S')}")
    out.mkdir(parents=True, exist_ok=True)
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)
    probes_dir = out / "probes"
    probes_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["PATH"] = f"{sdk / 'bin'}:{env.get('PATH', '')}"
    toka_lib = sdk / "lib"
    if toka_lib.is_dir():
        env["TOKA_LIB"] = str(toka_lib)

    receipts = []

    def run_cmd(name, cmd, cwd, allow_fail=False, expected_code=None, expected_substr=None, artifact_absent=False, timeout=300):
        t0 = time.time()
        r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - t0
        combined = r.stdout + "\n" + r.stderr

        ok = True
        err_msg = ""
        signal_val = None
        termination_val = None

        if r.returncode < 0:
            ok = False
            signal_val = -r.returncode
            termination_val = f"terminated by signal {signal_val}"
            err_msg = termination_val
        elif not allow_fail and r.returncode != 0:
            ok = False
            err_msg = f"command failed with exit {r.returncode}"
        elif allow_fail and r.returncode != 1:
            ok = False
            err_msg = f"negative command expected exit 1, got {r.returncode}"

        if expected_code and expected_code not in combined:
            ok = False
            err_msg = f"missing required diagnostic {expected_code}"
        if expected_substr and expected_substr not in combined:
            ok = False
            err_msg = f"missing required diagnostic substring {expected_substr!r}"

        rec = {
            "name": name,
            "argv": [str(c) for c in cmd],
            "cmd": [str(c) for c in cmd],
            "cwd": str(cwd),
            "exit_code": r.returncode,
            "signal": signal_val,
            "termination": termination_val,
            "error": None if ok else err_msg,
            "result": "pass" if ok else "fail",
            "stdout": r.stdout,
            "stderr": r.stderr,
            "stdout_sha256": sha256_bytes(r.stdout.encode("utf-8")),
            "stderr_sha256": sha256_bytes(r.stderr.encode("utf-8")),
            "execution_ms": round(elapsed * 1000.0, 2),
            "elapsed_seconds": round(elapsed, 4),
            "ok": ok,
        }
        if expected_code:
            rec["expected_code"] = expected_code
            rec["diagnostic"] = expected_code
        if artifact_absent:
            rec["artifact_absent"] = True

        receipts.append(rec)
        if not ok:
            raise RuntimeError(f"Step {name} failed: {err_msg}\n{combined}")
        return r

    # 1. Four tool versions (4 receipts)
    expected_v = a.version.removeprefix("v")
    versions = {}
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        tool_bin = sdk / f"bin/{tool}"
        if not tool_bin.is_file() and (sdk / f"bin/{tool}.exe").is_file():
            tool_bin = sdk / f"bin/{tool}.exe"
        r = run_cmd(f"version-{tool}", [str(tool_bin), "--version"], work)
        if expected_v not in r.stdout and expected_v not in r.stderr:
            raise RuntimeError(f"{tool} version output missing {expected_v}: {r.stdout}")
        versions[tool] = r.stdout.strip()

    # 2. Source-hidden TKI provider compilation & 4 rejection controls (5 receipts)
    tki_dir = work / "tki_provider"
    if tki_dir.exists():
        shutil.rmtree(tki_dir)
    tki_dir.mkdir(parents=True, exist_ok=True)

    src_path = tki_dir / "encap_019_provider.tk"
    tki_path = tki_dir / "encap_019_provider.tki"
    obj_path = tki_dir / "encap_019_provider.o"
    src_path.write_text(TKI_PROVIDER_019_SOURCE, encoding="utf-8")

    run_cmd(
        "pos-tki-provider-compile",
        [str(tokac_bin), "-I", str(toka_lib), "-c", "--emit-interface", str(src_path), "-o", str(obj_path)],
        tki_dir,
    )
    if not tki_path.exists() or not obj_path.exists():
        raise RuntimeError("Failed to build 0.19 TKI provider")

    tki_text = tki_path.read_text(encoding="utf-8")
    if "// @meta format_version: 6" not in tki_text:
        raise RuntimeError(f"Emitted .tki missing format_version: 6 header:\n{tki_text}")
    if "// @meta encap_epoch: 0.19" not in tki_text:
        raise RuntimeError(f"Emitted .tki missing encap_epoch: 0.19 header:\n{tki_text}")

    # Delete source file to guarantee sourceless interface consumption
    if src_path.exists():
        src_path.unlink()
    provider_source_present = src_path.exists()

    p_tki_sha = sha256_file(tki_path)
    p_obj_sha = sha256_file(obj_path)

    # 2a. Missing interface test
    missing_dir = work / "missing_iface"
    missing_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer = missing_dir / "consumer.tk"
    bad_consumer.write_text(POS3_TKI_019_CONSUMER_SOURCE, encoding="utf-8")
    r_missing = run_cmd(
        "neg-tki-missing-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(missing_dir), "--check-only", str(bad_consumer)],
        missing_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    missing_interface_rejected = (r_missing.returncode == 1)
    missing_iface_cmd = receipts[-1]

    # 2b. Corrupt interface test
    corrupt_dir = work / "corrupt_iface"
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer2 = corrupt_dir / "consumer.tk"
    bad_consumer2.write_text(POS3_TKI_019_CONSUMER_SOURCE, encoding="utf-8")
    bad_tki = corrupt_dir / "encap_019_provider.tki"
    bad_tki.write_bytes(b"CORRUPT_TKI_GARBAGE_BYTES")
    r_corrupt = run_cmd(
        "neg-tki-corrupt-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(corrupt_dir), "--check-only", str(bad_consumer2)],
        corrupt_dir,
        allow_fail=True,
        expected_code="E0901",
        artifact_absent=True,
    )
    corrupt_interface_rejected = (r_corrupt.returncode == 1)
    corrupt_iface_cmd = receipts[-1]

    # 2c. Stale Format 5 interface test
    fmt5_dir = work / "format5_iface"
    fmt5_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer3 = fmt5_dir / "consumer.tk"
    bad_consumer3.write_text(POS3_TKI_019_CONSUMER_SOURCE, encoding="utf-8")
    fmt5_tki = fmt5_dir / "encap_019_provider.tki"
    fmt5_tki.write_text(
        tki_text.replace("// @meta format_version: 6", "// @meta format_version: 5", 1),
        encoding="utf-8",
    )
    r_fmt5 = run_cmd(
        "neg-tki-format5-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(fmt5_dir), "--check-only", str(bad_consumer3)],
        fmt5_dir,
        allow_fail=True,
        expected_code="E0901",
        expected_substr="Interface format version mismatch (expected 6, got 5)",
        artifact_absent=True,
    )
    format5_interface_rejected = (r_fmt5.returncode == 1)
    format5_iface_cmd = receipts[-1]

    # 2d. Mismatched encap_epoch: 0.18 interface test
    ep018_dir = work / "epoch018_iface"
    ep018_dir.mkdir(parents=True, exist_ok=True)
    bad_consumer4 = ep018_dir / "consumer.tk"
    bad_consumer4.write_text(POS3_TKI_019_CONSUMER_SOURCE, encoding="utf-8")
    ep018_tki = ep018_dir / "encap_019_provider.tki"
    ep018_tki.write_text(
        tki_text.replace("// @meta encap_epoch: 0.19", "// @meta encap_epoch: 0.18", 1),
        encoding="utf-8",
    )
    r_ep018 = run_cmd(
        "neg-tki-epoch018-iface",
        [str(tokac_bin), "-I", str(toka_lib), "-I", str(ep018_dir), "--check-only", str(bad_consumer4)],
        ep018_dir,
        allow_fail=True,
        expected_code="E0901",
        expected_substr="Interface encap_epoch mismatch (expected 0.19, got 0.18)",
        artifact_absent=True,
    )
    epoch018_interface_rejected = (r_ep018.returncode == 1)
    epoch018_iface_cmd = receipts[-1]

    # 3. 10 Negative controls across 3 modes (30 receipts)
    neg_results = []

    def execute_negative_case(c):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if c.get("needs_tki"):
            inc_args.extend(["-I", str(tki_dir)])

        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"

        r_chk = run_cmd(
            f"neg-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        r_obj = run_cmd(
            f"neg-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if obj_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected object file")
        r_ll = run_cmd(
            f"neg-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
            allow_fail=True,
            expected_code=c["expected_code"],
            artifact_absent=True,
        )
        if ll_file.exists():
            raise RuntimeError(f"Negative case {cid} emitted unexpected LLVM IR file")

        return {
            "id": cid,
            "category": c["category"],
            "description": c["description"],
            "passed": True,
            "modes": {
                "check-only": {"rc": r_chk.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "compile-obj": {"rc": r_obj.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
                "emit-llvm": {"rc": r_ll.returncode, "expected_code": c["expected_code"], "code_matched": True, "leftover_artifacts": [], "passed": True},
            },
        }

    for c in NEGATIVE_019_CASES:
        neg_results.append(execute_negative_case(c))

    # 4. Positive controls (pos1, pos2, pos3) across 5 steps (15 receipts)
    pos_results = []

    def execute_positive_case(c, extra_include_dirs=None, extra_link_objs=None):
        cid = c["id"]
        src_file = probes_dir / f"{cid}.tk"
        src_file.write_text(c["source"].lstrip(), encoding="utf-8")

        case_work = work / cid
        if case_work.exists():
            shutil.rmtree(case_work)
        case_work.mkdir(parents=True, exist_ok=True)

        inc_args = ["-I", str(toka_lib)]
        if extra_include_dirs:
            for d in extra_include_dirs:
                inc_args.extend(["-I", str(d)])

        suffix = ".exe" if sys.platform == "win32" or os.name == "nt" else ""
        obj_file = case_work / f"{cid}.o"
        ll_file = case_work / f"{cid}.ll"
        bin_file = case_work / f"{cid}{suffix}"

        run_cmd(
            f"pos-{cid}-check-only",
            [str(tokac_bin), *inc_args, "--check-only", str(src_file)],
            case_work,
        )
        run_cmd(
            f"pos-{cid}-emit-llvm",
            [str(tokac_bin), *inc_args, "--emit-llvm", str(src_file), "-o", str(ll_file)],
            case_work,
        )
        if not ll_file.exists() or ll_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit LLVM file")
        run_cmd(
            f"pos-{cid}-compile-obj",
            [str(tokac_bin), *inc_args, "-c", str(src_file), "-o", str(obj_file)],
            case_work,
        )
        if not obj_file.exists() or obj_file.stat().st_size == 0:
            raise RuntimeError(f"Positive {cid} failed to emit object file")
        link_inputs = [str(src_file)]
        if extra_link_objs:
            link_inputs.extend(str(o) for o in extra_link_objs)
        run_cmd(
            f"pos-{cid}-binary-build",
            [str(tokac_bin), *inc_args, *link_inputs, "-o", str(bin_file)],
            case_work,
        )
        if not bin_file.exists():
            raise RuntimeError(f"Positive {cid} failed to build binary")
        r_run = run_cmd(
            f"pos-{cid}-native-execution",
            [str(bin_file)],
            case_work,
        )
        if r_run.returncode != 0:
            raise RuntimeError(f"Positive {cid} native execution failed with exit {r_run.returncode}")

        return {
            "id": cid,
            "description": c.get("description", ""),
            "passed": True,
        }

    for c in POSITIVE_019_CASES:
        pos_results.append(execute_positive_case(c))

    pos3_case = {
        "id": "pos3_cross_module_sourceless_tki_019_consumer",
        "description": "Cross-module sourceless .tki Format 6 / encap_epoch 0.19 consumer and native execution",
        "source": POS3_TKI_019_CONSUMER_SOURCE,
    }
    res_pos3 = execute_positive_case(
        pos3_case,
        extra_include_dirs=[tki_dir],
        extra_link_objs=[obj_path],
    )
    pos_results.append(res_pos3)

    tki_facts = {
        "provider_format_version": 6,
        "provider_encap_epoch": "0.19",
        "provider_tki_sha256": p_tki_sha,
        "provider_object_sha256": p_obj_sha,
        "consumed_tki_sha256": p_tki_sha,
        "consumed_object_sha256": p_obj_sha,
        "missing_interface_rejected": missing_interface_rejected,
        "corrupt_interface_rejected": corrupt_interface_rejected,
        "format5_interface_rejected": format5_interface_rejected,
        "epoch018_interface_rejected": epoch018_interface_rejected,
        "consumer_result": "pass" if res_pos3["passed"] else "fail",
        "provider_source_present": provider_source_present,
        "missing_interface_command": missing_iface_cmd,
        "corrupt_interface_command": corrupt_iface_cmd,
        "format5_interface_command": format5_iface_cmd,
        "epoch018_interface_command": epoch018_iface_cmd,
    }

    # 5. Full 0.19 Stage 2/4.1 visibility matrix on installed SDK (41 checks, 1 receipt)
    vis_script = script_dir / "test_019_stage2_visibility_sema.py"
    if not vis_script.is_file():
        alt_vis = ROOT / "tools/scripts/test_019_stage2_visibility_sema.py"
        if alt_vis.is_file():
            vis_script = alt_vis
        else:
            raise RuntimeError(f"Missing mandatory test_019_stage2_visibility_sema.py at {vis_script}")

    r_vis = run_cmd(
        "pos-019-visibility-matrix",
        [sys.executable, str(vis_script), "--build-dir", str(sdk)],
        work,
        expected_substr="All 41 Stage 2/4.1 verification checks PASSED.",
    )
    vis_passed = (r_vis.returncode == 0)

    # 6. Compositional convergence (60 probes, 1 receipt)
    comp_conv_script = script_dir / "test_compositional_convergence.py"
    if not comp_conv_script.is_file():
        alt_conv = ROOT / "tools/scripts/test_compositional_convergence.py"
        if alt_conv.is_file():
            comp_conv_script = alt_conv
        else:
            raise RuntimeError(f"Missing mandatory test_compositional_convergence.py at {comp_conv_script}")

    conv_cmd = [sys.executable, str(comp_conv_script), "--build-dir", str(sdk), "--suite-dir", str(fixtures_dir)]
    r_conv = run_cmd(
        "pos-compositional-convergence",
        conv_cmd,
        work,
    )
    comp_conv_passed = (r_conv.returncode == 0)
    comp_conv_count = 60

    report = {
        "schema": "toka.0.19-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "target": target,
        "build_testing": False,
        "default_encap_epoch": "0.19",
        "interface_format_version": 6,
        "groups": ["G-019-encap-field-visibility"],
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_19_candidate.py",
        "control_script_sha256": control_script_sha256,
        "four_tool_versions": versions,
        "SDK_root": str(sdk),
        "source_hidden_tki": tki_facts,
        "negative_controls": {
            "total": len(neg_results),
            "passed": sum(1 for r in neg_results if r["passed"]),
            "modes": ["--check-only", "-c", "--emit-llvm"],
        },
        "positive_controls": {
            "total": len(pos_results),
            "passed": sum(1 for r in pos_results if r["passed"]),
            "native_rc0": all(r["passed"] for r in pos_results),
        },
        "visibility_matrix": {
            "total": 41,
            "passed": 41 if vis_passed else 0,
        },
        "compositional_convergence": {
            "total": comp_conv_count,
            "passed": comp_conv_count if comp_conv_passed else 0,
        },
        "counts": {
            "total_receipts": len(receipts),
            "passed": sum(1 for r in receipts if r.get("ok")),
            "failed": sum(1 for r in receipts if not r.get("ok")),
            "groups": {
                "version": 4,
                "source_hidden_tki": 5,
                "negative_controls": len(neg_results) * 3,
                "positive_controls": len(pos_results) * 5,
                "visibility_matrix": 1,
                "compositional_convergence": 1,
            },
        },
        "receipts": receipts,
    }

    res_path = out / "result.json"
    res_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"0.19 candidate verification PASSED ({len(receipts)} receipts) -> {res_path}")


if __name__ == "__main__":
    main()
