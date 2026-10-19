#!/usr/bin/env python3
"""v0.18.1 regression suite: str-valued match expression provenance, borrow safety, 26 baseline probes, and TKI replay."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE_DIR = ROOT / "tests/semantics/str_match_expr_0181"
BASELINE_CASES = FIXTURE_DIR / "baseline_cases.json"
BASELINE_PROBES_DIR = FIXTURE_DIR / "baseline_probes"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    build_dir = Path(args.build_dir).resolve()
    compiler = build_dir / "bin/tokac"
    if not compiler.exists() and (build_dir / "bin/tokac.exe").exists():
        compiler = build_dir / "bin/tokac.exe"
    elif not compiler.exists() and (build_dir / "bin/Release/tokac.exe").exists():
        compiler = build_dir / "bin/Release/tokac.exe"
    assert compiler.exists(), f"missing compiler: {compiler}"

    toka_lib = os.environ.get("TOKA_LIB")
    if not toka_lib:
        toka_lib = str(build_dir / "lib") if (build_dir / "lib").is_dir() else str(ROOT / "lib")
    env = dict(os.environ, TOKA_LIB=toka_lib)
    exe_suffix = ".exe" if (sys.platform == "win32" or os.name == "nt") else ".bin"

    with tempfile.TemporaryDirectory(prefix="toka-str-match-0181-") as directory:
        work = Path(directory)

        def run(source, *flags):
            return subprocess.run(
                [str(compiler), str(source), *map(str, flags)],
                cwd=ROOT,
                env=env,
                capture_output=True,
                text=True,
                timeout=60,
            )

        def verify_positive(name, source_path):
            check = run(source_path, "--check-only")
            shadow = run(source_path, "--check-only", "--non-call-transfer-shadow=json")
            assert check.returncode == 0 and shadow.returncode == 0, (
                name,
                check.returncode,
                shadow.returncode,
                check.stderr,
                shadow.stderr,
            )
            assert check.stderr == shadow.stderr, (name, check.stderr, shadow.stderr)

            obj_out = work / f"{name}.o"
            obj_res = run(source_path, "-c", "-o", obj_out)
            if not obj_out.exists() and (work / f"{name}.obj").exists():
                obj_out = work / f"{name}.obj"
            assert obj_res.returncode == 0 and obj_out.exists() and obj_out.stat().st_size > 0, (
                name,
                obj_res.stderr,
            )

            ll_out = work / f"{name}.ll"
            ll_res = run(source_path, "--emit-llvm", "-o", ll_out)
            assert ll_res.returncode == 0 and ll_out.exists() and ll_out.stat().st_size > 0, (
                name,
                ll_res.stderr,
            )

            bin_out = work / f"{name}{exe_suffix}"
            bin_res = run(source_path, "-o", bin_out)
            if not bin_out.exists() and (work / f"{name}.exe").exists():
                bin_out = work / f"{name}.exe"
            assert bin_res.returncode == 0 and bin_out.exists() and bin_out.stat().st_size > 0, (
                name,
                bin_res.stderr,
            )
            exec_res = subprocess.run([str(bin_out)], env=env, capture_output=True, text=True, timeout=15)
            assert exec_res.returncode == 0, (name, exec_res.returncode, exec_res.stdout, exec_res.stderr)
            print(f"PASS positive (3-mode + shadow + runtime): {name}", flush=True)

        def verify_negative_probe(name, source_path, expected_codes, min_line=None):
            check = run(source_path, "--check-only")
            shadow = run(source_path, "--check-only", "--non-call-transfer-shadow=json")
            assert check.returncode == 1 and shadow.returncode == 1, (
                name,
                check.returncode,
                shadow.returncode,
                check.stderr,
            )
            assert check.stderr == shadow.stderr, (name, check.stderr, shadow.stderr)
            diag_codes = re.findall(r"error\[(E\d+)\]", check.stderr)
            assert diag_codes and diag_codes[0] in expected_codes, (
                name,
                expected_codes,
                diag_codes,
                check.stderr,
            )
            assert not re.search(r"error\[E01", check.stderr), (name, check.stderr)
            if min_line is not None:
                line_hits = [int(m) for m in re.findall(r"\.tk:(\d+):\d+", check.stderr)]
                assert line_hits and all(line >= min_line for line in line_hits), (
                    name,
                    min_line,
                    line_hits,
                    check.stderr,
                )

            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                out_file = work / f"{name}{suffix}"
                if out_file.exists():
                    out_file.unlink()
                rejected = run(source_path, flag, "-o", out_file)
                rej_codes = re.findall(r"error\[(E\d+)\]", rejected.stderr)
                assert rejected.returncode == 1 and not out_file.exists(), (name, flag, rejected.stderr)
                if suffix == ".o":
                    assert not (work / f"{name}.obj").exists(), (name, flag)
                assert rej_codes and rej_codes[0] in expected_codes, (
                    name,
                    flag,
                    expected_codes,
                    rej_codes,
                    rejected.stderr,
                )
            matched = ",".join(sorted(set(diag_codes)))
            print(
                f"PASS negative (3-mode + shadow + zero-artifact): {name} [primary={diag_codes[0]}, all={matched}]",
                flush=True,
            )

        # 1. Verify all 26 baseline evaluation probes (P01..P18 excluding history-only P13, plus N01..N09)
        baseline_cases = [
            c for c in json.loads(BASELINE_CASES.read_text(encoding="utf-8"))
            if not c.get("history_only")
        ]
        assert len(baseline_cases) == 26, len(baseline_cases)
        for case in baseline_cases:
            case_id = case["id"]
            probe_name = Path(case["source"]).name
            probe_path = BASELINE_PROBES_DIR / probe_name
            assert probe_path.is_file(), f"missing baseline probe: {probe_path}"
            if case["expected"] == "pass":
                verify_positive(f"baseline_{case_id}", probe_path)
            else:
                assert case["expected"] == "reject", case
                verify_negative_probe(
                    f"baseline_{case_id}",
                    probe_path,
                    tuple(case["expected_diagnostics"]),
                    min_line=case.get("diagnostic_min_line"),
                )

        # 2. P-A through P-F main fixture
        verify_positive("positive_controls", FIXTURE_DIR / "positive_controls.tk")

        # 3. P-F: Source-hidden TKI export & consumption for match-expression str provider
        tki_dir = work / "tki_pf"
        tki_dir.mkdir(exist_ok=True)
        provider_tk = tki_dir / "lease_provider.tk"
        provider_tk.write_text(
            """import core/string::{string}
pub shape LeaseBox(StaticTag(i32) = 0 | Owned(string) = 1)
impl LeaseBox {
    pub fn as_str(self) -> str <- self {
        auto result = match self {
            auto LeaseBox::StaticTag(_tag) => { pass "from_tki_static" }
            auto LeaseBox::Owned(&text) => { pass text.as_str() }
        }
        return result
    }
}
pub fn pick_pair(a: string, b: string, flag: bool) -> str <- a | b {
    return match flag {
        true => { pass a.as_str() }
        _ => { pass b.as_str() }
    }
}
""",
            encoding="utf-8",
        )
        provider_o = tki_dir / "lease_provider.o"
        emit_res = run(provider_tk, "-c", "--emit-interface", "-o", provider_o)
        if not provider_o.exists() and (tki_dir / "lease_provider.obj").exists():
            provider_o = tki_dir / "lease_provider.obj"
        assert emit_res.returncode == 0 and provider_o.exists() and (tki_dir / "lease_provider.tki").exists(), (
            emit_res.stderr
        )
        provider_tk.unlink()

        consumer_tk = tki_dir / "consumer.tk"
        consumer_tk.write_text(
            """import core/string::{string}
import lease_provider::{LeaseBox, pick_pair}
fn main() -> i32 {
    auto b = LeaseBox::StaticTag(0:i32)
    auto o = LeaseBox::Owned(string::from("from_tki_owned"))
    assert(b.as_str().equals("from_tki_static"), "tki static")
    assert(o.as_str().equals("from_tki_owned"), "tki owned")
    auto sa = string::from("first_owner")
    auto sb = string::from("second_owner")
    assert(pick_pair(sa, sb, true).equals("first_owner"), "tki pair true")
    assert(pick_pair(sa, sb, false).equals("second_owner"), "tki pair false")
    return 0:i32
}
""",
            encoding="utf-8",
        )
        consumer_check = run(consumer_tk, "-I", tki_dir, "--check-only")
        assert consumer_check.returncode == 0, consumer_check.stderr
        consumer_o = tki_dir / "consumer.o"
        consumer_compile = run(consumer_tk, "-I", tki_dir, "-c", "-o", consumer_o)
        if not consumer_o.exists() and (tki_dir / "consumer.obj").exists():
            consumer_o = tki_dir / "consumer.obj"
        assert consumer_compile.returncode == 0 and consumer_o.exists(), consumer_compile.stderr
        consumer_bin = tki_dir / f"consumer{exe_suffix}"
        link_res = run(consumer_tk, provider_o, "-I", tki_dir, "-o", consumer_bin)
        if not consumer_bin.exists() and (tki_dir / "consumer.exe").exists():
            consumer_bin = tki_dir / "consumer.exe"
        assert link_res.returncode == 0 and consumer_bin.exists(), link_res.stderr
        run_res = subprocess.run([str(consumer_bin)], env=env, capture_output=True, text=True, timeout=15)
        assert run_res.returncode == 0, (run_res.returncode, run_res.stderr)
        print("PASS P-F source-hidden TKI consumption + runtime execution", flush=True)

        # 5. Negative controls N-A through N-F on the match-expression path
        negatives = {
            # N-A: arm-local string escape via binding, direct return, or shadowed parameter name
            "na_arm_local_binding_escape": (
                ("E0456",),
                """import core/string::{string}
fn main() -> i32 {
    auto v = match true {
        true => {
            auto local_s = string::from("temp")
            pass local_s.as_str()
        }
        _ => {
            pass "static"
        }
    }
    return v.len() as i32
}
""",
            ),
            "na_arm_local_direct_return_escape": (
                ("E0455", "E0456"),
                """import core/string::{string}
fn bad(flag: bool) -> str {
    return match flag {
        true => {
            auto local_s = string::from("temp")
            pass local_s.as_str()
        }
        _ => {
            pass "static"
        }
    }
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            "na_arm_local_shadowed_param_escape": (
                ("E0456", "E0455"),
                """import core/string::{string}
fn bad(s: string, flag: bool) -> str <- s {
    auto v = match flag {
        true => {
            auto s = string::from("shadowed_local")
            pass s.as_str()
        }
        _ => {
            pass s.as_str()
        }
    }
    return v
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            # N-B: static safe arm + local owner dangerous arm in both orders
            "nb_static_then_local_escape": (
                ("E0456", "E0455"),
                """import core/string::{string}
fn bad(flag: bool) -> str {
    auto result = match flag {
        true => { pass "safe_static" }
        _ => {
            auto local_s = string::from("boom")
            pass local_s.as_str()
        }
    }
    return result
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            "nb_local_then_static_escape": (
                ("E0456", "E0455"),
                """import core/string::{string}
fn bad(flag: bool) -> str {
    auto result = match flag {
        true => {
            auto local_s = string::from("boom")
            pass local_s.as_str()
        }
        _ => { pass "safe_static" }
    }
    return result
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            # N-C: match cede consuming payload then returning view into consumed payload
            "nc_match_cede_owned_payload_view_escape": (
                ("E0455", "E0456"),
                """import core/string::{string}
pub shape ValueLease(StaticTag(i32) = 0 | Owned(string) = 1)
pub fn bad_consume(cede v: ValueLease) -> str {
    auto result = match cede v {
        auto ValueLease::StaticTag(_tag) => { pass "static" }
        auto ValueLease::Owned(text) => { pass text.as_str() }
    }
    return result
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            "nc_match_cede_residual_ref_escape": (
                ("E0455", "E0456", "E0440"),
                """import core/string::{string}
pub shape ValueLease(StaticTag(i32) = 0 | Owned(string) = 1)
pub fn bad_consume_ref(cede v: ValueLease) -> str {
    return match cede v {
        auto ValueLease::StaticTag(_tag) => { pass "static" }
        auto ValueLease::Owned(&text) => { pass text.as_str() }
    }
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            # N-D: move or replace owner while match-expression view is active (single & dual owners)
            "nd_move_owner_while_match_view_active": (
                ("E0440",),
                """import core/string::{string}
pub shape ValueLease(StaticTag(i32) = 0 | Owned(string) = 1)
impl ValueLease {
    pub fn as_str(self) -> str <- self {
        auto result = match self {
            auto ValueLease::StaticTag(_tag) => { pass "static" }
            auto ValueLease::Owned(&text) => { pass text.as_str() }
        }
        return result
    }
}
fn consume(cede value: ValueLease) { auto held = cede value }
fn main() -> i32 {
    auto v = ValueLease::Owned(string::from("hello"))
    auto view = v.as_str()
    consume(cede v)
    assert(view.equals("hello"), "use after move")
    return 0:i32
}
""",
            ),
            "nd_replace_owner_while_match_view_active": (
                ("E0442",),
                """import core/string::{string}
pub shape ValueLease(StaticTag(i32) = 0 | Owned(string) = 1)
impl ValueLease {
    pub fn as_str(self) -> str <- self {
        return match self {
            auto ValueLease::StaticTag(_tag) => { pass "static" }
            auto ValueLease::Owned(&text) => { pass text.as_str() }
        }
    }
}
fn main() -> i32 {
    auto v# = ValueLease::Owned(string::from("hello"))
    auto view = v.as_str()
    v = ValueLease::StaticTag(1:i32)
    assert(view.equals("hello"), "use after replace")
    return 0:i32
}
""",
            ),
            "nd_replace_second_owner_while_inline_match_view_active": (
                ("E0442",),
                """import core/string::{string}
fn main() -> i32 {
    auto a# = string::from("alpha")
    auto b# = string::from("beta")
    auto view = match false {
        true => { pass a.as_str() }
        _ => { pass b.as_str() }
    }
    b = string::from("gamma")
    assert(view.equals("beta"), "use after replace b")
    return 0:i32
}
""",
            ),
            # N-E: missing return dependency annotation (single owner or second owner in dual-owner match)
            "ne_missing_single_return_dep": (
                ("E0454",),
                """import core/string::{string}
fn bad_pick(s: string, flag: bool) -> str {
    auto result = match flag {
        true => { pass s.as_str() }
        _ => { pass "static" }
    }
    return result
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            "ne_missing_second_owner_return_dep": (
                ("E0454",),
                """import core/string::{string}
fn bad_two(a: string, b: string, flag: bool) -> str <- a {
    return match flag {
        true => { pass a.as_str() }
        _ => { pass b.as_str() }
    }
}
fn main() -> i32 { return 0:i32 }
""",
            ),
            # N-F: incomplete/unknown facts remain fail-closed (source-hidden unannotated TKI & cede str temporary)
            "nf_cede_str_temporary_from_match": (
                ("E04661",),
                """fn main() -> i32 {
    auto v = cede match true {
        true => { pass "a" }
        _ => { pass "b" }
    }
    return 0:i32
}
""",
            ),
        }

        for name, (expected_codes, body) in negatives.items():
            src = work / f"{name}.tk"
            src.write_text(body, encoding="utf-8")
            verify_negative_probe(name, src, expected_codes)

        # N-F: source-hidden TKI without <- dependency must still reject with E04661
        nf_tki_dir = work / "tki_nf"
        nf_tki_dir.mkdir(exist_ok=True)
        nf_prov = nf_tki_dir / "hidden_static.tk"
        nf_prov.write_text(
            """pub fn hidden_match(flag: bool) -> str {
    auto result = match flag {
        true => { pass "a" }
        _ => { pass "b" }
    }
    return result
}
""",
            encoding="utf-8",
        )
        nf_prov_o = nf_tki_dir / "hidden_static.o"
        assert run(nf_prov, "-c", "--emit-interface", "-o", nf_prov_o).returncode == 0
        nf_prov.unlink()

        nf_cons = nf_tki_dir / "consumer_bad.tk"
        nf_cons.write_text(
            """import hidden_static::{hidden_match}
fn main() -> i32 {
    auto s = hidden_match(true)
    return s.len() as i32
}
""",
            encoding="utf-8",
        )
        for flag, suffix in (("--check-only", None), ("-c", ".o"), ("--emit-llvm", ".ll")):
            args = ["-I", nf_tki_dir, flag]
            out_path = nf_tki_dir / f"consumer_bad{suffix}" if suffix else None
            if out_path:
                args.extend(["-o", out_path])
            res = run(nf_cons, *args)
            assert res.returncode == 1 and "E04661" in res.stderr, (flag, res.stderr)
            if out_path:
                assert not out_path.exists()
        print("PASS N-F source-hidden unannotated TKI match str rejected [E04661]", flush=True)


if __name__ == "__main__":
    main()
