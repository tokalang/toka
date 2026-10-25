#!/usr/bin/env python3
"""
Toka 0.20.1 Raw Pointer Pointee Access Requires Unsafe — Regression & Matrix Suite.

Verifies:
  1. Six Source-Level Control Groups (G1..G6, 35 probes) across --check-only, -c,
     --emit-llvm, and real binary compilation + execution for positive controls.
  2. Three-Category LLVM IR Evidence Verification (IR-1, IR-2, IR-3).
  3. Sourceless .tki + .o Consumption Matrix (TKI-1..TKI-4b, 10 cases) with
     physical producer .tk removal before consumer compilation.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def resolve_tokac(build_dir: Path) -> Path:
    candidates = [
        build_dir / "bin" / "tokac",
        build_dir / "bin" / "tokac.exe",
    ]
    for c in candidates:
        if c.exists():
            return c.resolve()
    raise FileNotFoundError(f"tokac not found under {build_dir / 'bin'}")


def run_cmd(argv, cwd: Path, env=None):
    merged_env = dict(os.environ)
    merged_env["TOKA_LIB"] = str(REPO_ROOT / "lib")
    if env:
        merged_env.update(env)
    proc = subprocess.run(
        [str(x) for x in argv],
        cwd=str(cwd),
        env=merged_env,
        text=True,
        capture_output=True,
        timeout=90,
    )
    sig = -proc.returncode if proc.returncode < 0 else 0
    return {
        "argv": [str(x) for x in argv],
        "cwd": str(cwd),
        "exit_code": proc.returncode,
        "signal": sig,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def extract_diagnostics(stderr_text: str):
    clean = re.sub(r"\x1b\[[0-9;]*m", "", stderr_text)
    pattern = re.compile(
        r"(error|warning)\[(E\d+|W\d+)\]: ([^\n]+)\n\s*-->\s*((?:[A-Za-z]:[\\/])?[^\n:]*):(\d+):(\d+)"
    )
    diags = []
    for m in pattern.finditer(clean):
        diags.append({
            "level": m.group(1),
            "code": m.group(2),
            "message": m.group(3),
            "file": m.group(4),
            "line": int(m.group(5)),
            "col": int(m.group(6)),
        })
    return diags


def run_source_probe(
    tokac: Path,
    work_root: Path,
    group_id: str,
    probe_id: str,
    rel_dir: str,
    filename: str,
    source: str,
    expect_pass: bool,
    expected_diag: str = None,
    description: str = "",
):
    probe_dir = work_root / "probes" / rel_dir
    probe_dir.mkdir(parents=True, exist_ok=True)
    src_path = probe_dir / filename
    src_path.write_text(source, encoding="utf-8", newline="\n")

    stem = src_path.stem
    obj_path = probe_dir / f"{stem}.o"
    ll_path = probe_dir / f"{stem}.ll"
    exe_suffix = ".exe" if os.name == "nt" else ".bin"
    bin_path = probe_dir / f"{stem}{exe_suffix}"

    for p in (obj_path, ll_path, bin_path):
        p.unlink(missing_ok=True)

    check_res = run_cmd([tokac, "--check-only", src_path], probe_dir)
    obj_res = run_cmd([tokac, "-c", src_path, "-o", obj_path], probe_dir)
    ir_res = run_cmd([tokac, "--emit-llvm", src_path, "-o", ll_path], probe_dir)

    artifacts_after_modes = {
        "o_exists": obj_path.exists(),
        "ll_exists": ll_path.exists(),
    }

    run_res = None
    build_bin_res = None
    if expect_pass:
        build_bin_res = run_cmd([tokac, src_path, "-o", bin_path], probe_dir)
        if build_bin_res["exit_code"] == 0 and bin_path.exists():
            run_res = run_cmd([bin_path], probe_dir)
        bin_exists = bin_path.exists()
        bin_path.unlink(missing_ok=True)
        obj_path.unlink(missing_ok=True)
        ll_path.unlink(missing_ok=True)
    else:
        bin_exists = False

    check_diags = extract_diagnostics(check_res["stderr"])
    obj_diags = extract_diagnostics(obj_res["stderr"])
    ir_diags = extract_diagnostics(ir_res["stderr"])

    if expect_pass:
        passed = (
            check_res["exit_code"] == 0
            and obj_res["exit_code"] == 0
            and ir_res["exit_code"] == 0
            and check_res["signal"] == 0
            and obj_res["signal"] == 0
            and ir_res["signal"] == 0
            and artifacts_after_modes["o_exists"]
            and artifacts_after_modes["ll_exists"]
            and build_bin_res is not None
            and build_bin_res["exit_code"] == 0
            and bin_exists
            and run_res is not None
            and run_res["exit_code"] == 0
            and run_res["signal"] == 0
        )
    else:
        error_diags_check = [d for d in check_diags if d["level"] == "error"]
        error_diags_obj = [d for d in obj_diags if d["level"] == "error"]
        error_diags_ir = [d for d in ir_diags if d["level"] == "error"]
        passed = (
            check_res["exit_code"] == 1
            and obj_res["exit_code"] == 1
            and ir_res["exit_code"] == 1
            and check_res["signal"] == 0
            and obj_res["signal"] == 0
            and ir_res["signal"] == 0
            and not artifacts_after_modes["o_exists"]
            and not artifacts_after_modes["ll_exists"]
            and len(error_diags_check) >= 1
            and len(error_diags_obj) >= 1
            and len(error_diags_ir) >= 1
            and error_diags_check[0]["code"] == expected_diag
            and error_diags_obj[0]["code"] == expected_diag
            and error_diags_ir[0]["code"] == expected_diag
        )

    return {
        "group_id": group_id,
        "probe_id": probe_id,
        "description": description,
        "source_sha256": sha256_file(src_path),
        "expect_pass": expect_pass,
        "expected_diag": expected_diag,
        "passed": passed,
        "modes": {
            "check_only": check_res,
            "compile_object": obj_res,
            "emit_llvm": ir_res,
            "build_binary": build_bin_res,
            "run_binary": run_res,
        },
        "diagnostics": {
            "check_only": check_diags,
            "compile_object": obj_diags,
            "emit_llvm": ir_diags,
        },
        "artifacts_check": {
            **artifacts_after_modes,
            "bin_built": bin_exists,
        },
    }


def execute_suite(tokac: Path, work_root: Path):
    probes = []

    g1_specs = [
        (
            "G1-01",
            "g1_01_local_bare_read_write_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto v = p
    return v
}
""",
            "E0625",
            "Local bare raw pointer *p 1-layer pointee read outside unsafe",
        ),
        (
            "G1-02",
            "g1_02_param_bare_write_and_narrowed_neg.tk",
            """fn write_out(*out#: i32, ?np: i32) -> i32 {
    if ?np == null { return 1 }
    out = np
    return 0
}

fn main() -> i32 {
    auto x# = 10:i32
    auto y = 20:i32
    auto ?yp = *y : ?i32
    return write_out(*x, ?yp)
}
""",
            "E0625",
            "Parameter bare raw pointer *out# write and narrowed ?np read outside unsafe",
        ),
        (
            "G1-03",
            "g1_03_global_bare_read_neg.tk",
            """auto g_slot = 99:i32
auto *g_ptr = *g_slot

fn main() -> i32 {
    return g_ptr
}
""",
            "E0625",
            "Global bare raw pointer *g_ptr pointee read outside unsafe",
        ),
        (
            "G1-04",
            "g1_04_safe_host_field_hat_off_neg.tk",
            """shape RawBox(*p: i32, *?pq: i32)

fn main() -> i32 {
    auto x = 42:i32
    auto ?q = *x : ?i32
    auto box = RawBox(*p = *x, *?pq = *?q)
    return box.p
}
""",
            "E0625",
            "Safe value host raw field hat-off projection (box.p) outside unsafe",
        ),
        (
            "G1-05",
            "g1_05_call_arg_bare_projection_neg.tk",
            """fn take_val(v: i32) -> i32 {
    return v
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return take_val(p)
}
""",
            "E0625",
            "Call argument bare raw pointer projection take_val(p) outside unsafe",
        ),
        (
            "G1-06",
            "g1_06_return_and_pass_bare_projection_neg.tk",
            """fn pass_proj(*p: i32) -> i32 {
    auto v = if true {
        pass p
    } else {
        pass 0:i32
    }
    return v
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return pass_proj(*p)
}
""",
            "E0625",
            "pass expression bare raw pointer projection (pass p) outside unsafe",
        ),
        (
            "G1-07",
            "g1_07_aggregate_init_bare_projection_neg.tk",
            """shape Pair(a: i32, b: i32)

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto pair = Pair(a = p, b = 1)
    return pair.a
}
""",
            "E0625",
            "Aggregate struct initialization bare raw pointer projection Pair(a = p, b = 1) outside unsafe",
        ),
        (
            "G1-08",
            "g1_08_raw_reference_construction_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto &r = &p
    return r
}
""",
            "E0625",
            "Safe reference construction (&p) from raw pointer outside unsafe",
        ),
        (
            "G1-09",
            "g1_09_safe_generic_body_raw_read_neg.tk",
            """fn read_generic<T>(*p: T) -> T {
    return p
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return read_generic<i32>(*p)
}
""",
            "E0625",
            "Safe generic body raw pointer parameter projection fn read_generic<T>(*p: T) -> T { return p }",
        ),
    ]
    for pid, fname, src, diag, desc in g1_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G1_RAW_PROJECTION_NEGATIVES",
                pid,
                "G1_raw_projection_negatives",
                fname,
                src,
                False,
                diag,
                desc,
            )
        )

    g2_specs = [
        (
            "G2-01",
            "g2_01_unsafe_block_multilayer_read_write_pos.tk",
            """fn main() -> i32 {
    auto a# = 10:i32
    auto b# = 20:i32
    auto sentinel = 77:i32
    auto *p# = *a
    auto ?#qa = *a : ?i32
    auto ?qb = *b : ?i32
    auto *?pq = *?qa
    auto **pp# = **p
    auto ***ppp# = ***pp

    unsafe {
        if p != 10 { return 1 }
        p = 33
        if a != 33 { return 2 }

        auto ?cur_q = pq
        guard ?cur_q {
            if cur_q != 33 { return 4 }
        } else {
            return 3
        }

        ?#qa = ?qb
        auto ?after_q = pq
        guard ?after_q {
            if after_q != 20 { return 6 }
        } else {
            return 5
        }

        auto *mid = pp
        auto leaf = mid
        if leaf != 33 { return 7 }

        auto **top_mid# = ppp
        auto *top_leaf_ptr# = top_mid
        top_leaf_ptr = 55
    }

    if a != 55 { return 8 }
    if b != 20 { return 9 }
    if sentinel != 77 { return 10 }
    return 0
}
""",
            "Explicit unsafe block reading/writing 1-layer, 2-layer, and 3-layer raw pointers with exact slot writeback verification",
        ),
        (
            "G2-02",
            "g2_02_unsafe_expr_single_evaluation_pos.tk",
            """auto g_eval_hits# = 0:i32

shape RawBox(*p: i32)

fn make_box(b: RawBox) -> RawBox {
    g_eval_hits += 1
    return b
}

fn main() -> i32 {
    auto x = 42:i32
    auto box = RawBox(*p = *x)
    auto val = unsafe (make_box(box).p)
    if val != 42 { return 1 }
    if g_eval_hits != 1 { return 2 }
    return 0
}
""",
            "Explicit unsafe (...) expression reading raw field with exact single-evaluation verification (g_eval_hits == 1)",
        ),
        (
            "G2-03",
            "g2_03_unsafe_fn_pointee_read_write_pos.tk",
            """unsafe fn accumulate(*acc#: i32, *delta: i32) -> i32 {
    unsafe {
        acc = acc + delta
        return acc
    }
}

fn main() -> i32 {
    auto total# = 34:i32
    auto step = 8:i32
    auto *p_total# = *total
    auto *p_step = *step
    unsafe {
        auto res = accumulate(*p_total, *p_step)
        if res != 42 { return 1 }
    }
    if total != 42 { return 2 }
    return 0
}
""",
            "Explicit unsafe fn reading and writing raw pointer pointees and verifying writeback at runtime",
        ),
        (
            "G2-04",
            "g2_04_unsafe_raw_borrow_and_method_receiver_pos.tk",
            """shape Counter(v: i32)
impl Counter {
    pub fn get(self) -> i32 {
        return self.v
    }
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto c = Counter(v = 42)
    auto *cp = *c
    unsafe {
        auto &r1 = &p
        auto &r2 = (*p) as &i32
        auto m1 = cp.get()
        auto m2 = (*cp).get()
        if r1 != 42 || r2 != 42 || m1 != 42 || m2 != 42 {
            return 1
        }
    }
    return 0
}
""",
            "Explicit unsafe block constructing safe references (&p, (*p) as &i32) and invoking methods on raw receivers (cp.get(), (*cp).get())",
        ),
    ]
    for pid, fname, src, desc in g2_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G2_EXPLICIT_UNSAFE_POSITIVES",
                pid,
                "G2_explicit_unsafe_positives",
                fname,
                src,
                True,
                None,
                desc,
            )
        )

    g3_specs = [
        (
            "G3-01",
            "g3_01_handle_copy_widening_nullcheck_guard_pos.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto *p_copy = *p
    if *p_copy != *p { return 1 }

    auto ?np = *p : ?i32
    if ?np == null { return 2 }
    guard ?np {
        auto *u1 = (?np).unwrap()
        if *u1 != *p { return 4 }
    } else {
        return 3
    }

    auto ?q = *x : ?i32
    auto *?pq = *?q
    auto *?pq_copy = *?pq
    if *?pq_copy != *?pq { return 5 }

    auto ??qq = *?pq : ??i32
    if ??qq == null { return 6 }
    guard ??qq {
        auto *?u2 = (??qq).unwrap()
        if *?u2 != *?pq { return 8 }
    } else {
        return 7
    }

    return 0
}
""",
            "Safe handle copy, outer nullable widening (*p : ?i32, *?pq : ??i32), ?np/??qq null checks, and guard narrowing outside unsafe",
        ),
        (
            "G3-02",
            "g3_02_builtin_unwrap_and_value_unset_pos.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto ?np = *p : ?i32
    auto *u1 = (?np).unwrap()
    if *u1 != *p { return 1 }

    auto ?q = *x : ?i32
    auto *?pq = *?q
    auto ??qq = *?pq : ??i32
    auto *?u2 = (??qq).unwrap()
    if *?u2 != *?pq { return 2 }

    auto temp# = 99:i32
    temp.unset()
    temp = 0
    return temp
}
""",
            "Confirmed builtin (?np).unwrap(), (??qq).unwrap(), and local value .unset() outside unsafe",
        ),
        (
            "G3-03",
            "g3_03_local_slot_rebind_and_address_of_pos.tk",
            """fn main() -> i32 {
    auto a = 10:i32
    auto b = 20:i32
    auto *#p = *a
    *#p = *b
    if *p != *b { return 1 }

    auto ?#np = *a : ?i32
    ?#np = null
    if ?np != null { return 2 }

    auto ?qa = *a : ?i32
    auto ?qb = *b : ?i32
    auto *#?pq = *?qa
    *#?pq = *?qb
    if *?pq != *?qb { return 3 }

    auto **slot_p = **p
    auto **?slot_pq = **?pq
    if **slot_p != **p { return 4 }
    if **?slot_pq != **?pq { return 5 }
    return 0
}
""",
            "Local rebindable handle slot rebind (*#p = *b, ?#np = null, *#?pq = *?qb) and local slot address-of (**p, **?pq, K == D + 1) outside unsafe",
        ),
        (
            "G3-04",
            "g3_04_safe_host_hatted_field_and_array_element_pos.tk",
            """shape Node(?#next: Node, *?slot: i32)

fn read_elem(arr: [*?i32; 2]) -> *?i32 {
    auto *?h = arr[0]
    return *?h
}

fn main() -> i32 {
    auto x = 42:i32
    auto ?qx = *x : ?i32
    auto tail = Node(?next = null, *?slot = *?qx)
    auto ?tail_ptr = *tail : ?Node
    auto head# = Node(?next = null, *?slot = *?qx)

    head.?#next = ?tail_ptr
    auto ?linked = head.?next
    if ?linked != ?tail_ptr { return 1 }

    auto *?field_slot = head.*?slot
    if *?field_slot != *?qx { return 2 }

    auto *?elem = read_elem([*?qx, *?qx])
    if *?elem != *?qx { return 3 }

    return 0
}
""",
            "Safe host hatted raw field read/rebind (head.?#next = ?tail_ptr, head.?next, head.*?slot) and safe array raw element handle copy (arr[0]) outside unsafe",
        ),
        (
            "G3-05",
            "g3_05_whole_value_generic_relay_pos.tk",
            """fn relay<T>(cede x: T) -> T {
    return cede x
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto *p_ref = *x
    auto ?qx = *x : ?i32
    auto *?pq = *?qx
    auto *?pq_ref = *?qx

    auto *r1 = relay<*i32>(cede *p)
    auto *?r2 = relay<*?i32>(cede *?pq)
    if *r1 != *p_ref { return 1 }
    if *?r2 != *?pq_ref { return 2 }
    return 0
}
""",
            "Whole-value generic relay<T>(cede x: T) -> T passthrough with T = *i32 and T = *?i32 outside unsafe",
        ),
    ]
    for pid, fname, src, desc in g3_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G3_SAFE_HANDLE_POSITIVES",
                pid,
                "G3_safe_handle_positives",
                fname,
                src,
                True,
                None,
                desc,
            )
        )

    g4_specs = [
        (
            "G4-01",
            "g4_01_raw_host_hatted_field_neg.tk",
            """shape Node(?next: Node, v: i32)

fn main() -> i32 {
    auto n = Node(?next = null, v = 1)
    auto *p = *n
    auto ?nx = p.?next
    return 0
}
""",
            "E0625",
            "Raw host hatted field selection (p.?next) outside unsafe rejected with E0625 despite hatted field",
        ),
        (
            "G4-02",
            "g4_02_raw_host_hatted_field_then_unwrap_neg.tk",
            """shape Node(?next: Node, v: i32)

fn main() -> i32 {
    auto n = Node(?next = null, v = 1)
    auto *p = *n
    auto *nx = (p.?next).unwrap()
    return 0
}
""",
            "E0625",
            "Raw host hatted field followed by builtin unwrap ((p.?next).unwrap()) rejected with E0625 on p.?next",
        ),
        (
            "G4-03",
            "g4_03_user_named_unwrap_method_on_raw_receiver_neg.tk",
            """shape Holder(v: i32)
impl Holder {
    pub fn unwrap(self) -> i32 {
        return self.v
    }
}

fn main() -> i32 {
    auto h = Holder(v = 42)
    auto *p = *h
    return (*p).unwrap()
}
""",
            "E0625",
            "User-defined method named .unwrap() on non-nullable raw pointer (*p).unwrap() rejected with E0625",
        ),
        (
            "G4-04",
            "g4_04_raw_return_then_method_call_neg.tk",
            """shape Counter(v: i32)
impl Counter {
    pub fn get(self) -> i32 {
        return self.v
    }
}

fn make_raw(*c: Counter) -> *Counter {
    return *c
}

fn main() -> i32 {
    auto c = Counter(v = 42)
    return make_raw(*c).get()
}
""",
            "E0625",
            "Function returning raw pointer followed by method call make_raw(*c).get() outside unsafe rejected with E0625",
        ),
        (
            "G4-05",
            "g4_05_safe_array_raw_element_then_projection_neg.tk",
            """fn read_and_project(arr: [*?i32; 2]) -> ?i32 {
    auto *?h = arr[0]
    auto ?projected = h
    return ?projected
}

fn main() -> i32 {
    return 0
}
""",
            "E0625",
            "Safe array raw handle element read (auto *?h = arr[0]) followed by 1-layer projection (auto ?projected = h) rejected on step 2 with E0625",
        ),
    ]
    for pid, fname, src, diag, desc in g4_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G4_COMPOSITIONAL_NEGATIVES",
                pid,
                "G4_compositional_negatives",
                fname,
                src,
                False,
                diag,
                desc,
            )
        )

    g5_specs = [
        (
            "G5-01",
            "g5_01_caller_unsafe_does_not_authorize_generic_fn_neg.tk",
            """fn read_generic<T>(*p: T) -> T {
    return p
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    unsafe {
        return read_generic<i32>(*p)
    }
}
""",
            "E0625",
            "Caller unsafe { read_generic<i32>(*p) } does not authorize ordinary generic function body",
        ),
        (
            "G5-02",
            "g5_02_caller_unsafe_does_not_authorize_closure_neg.tk",
            """fn apply(*p: i32, cb: fn(*i32) -> i32) -> i32 {
    return cb(*p)
}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    unsafe {
        return apply(*p, { *q => return q })
    }
}
""",
            "E0625",
            "Caller unsafe { apply(*p, { *q => return q }) } does not authorize ordinary closure body",
        ),
        (
            "G5-03",
            "g5_03_unsafe_subexpr_does_not_leak_to_sibling_neg.tk",
            """fn main() -> i32 {
    auto x = 21:i32
    auto *p = *x
    auto sum = (unsafe (p)) + p
    return sum
}
""",
            "E0625",
            "unsafe (p) subexpression does not leak authorization to sibling + p in the same expression",
        ),
        (
            "G5-04",
            "g5_04_unsafe_return_raw_handle_then_outer_projection_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto *q = unsafe (*p)
    return q
}
""",
            "E0625",
            "Returning raw handle from unsafe (*p) does not authorize outer pointee projection (return q)",
        ),
        (
            "G5-05",
            "g5_05_error_recovery_no_state_leak_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto bad1 = p
    auto *ok_copy = *p
    auto bad2 = ok_copy
    return 0
}
""",
            "E0625",
            "After a rejected raw projection (bad1 = p), subsequent safe handle copy (*ok_copy = *p) and second raw projection (bad2 = ok_copy) preserve clean state",
        ),
    ]
    for pid, fname, src, diag, desc in g5_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G5_CONTEXT_ISOLATION_NEGATIVES",
                pid,
                "G5_context_isolation_negatives",
                fname,
                src,
                False,
                diag,
                desc,
            )
        )

    g6_specs = [
        (
            "G6-01",
            "g6_01_nonnull_outer_null_compare_e0483_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto ?q = *x : ?i32
    auto *?pq = *?q
    if *?pq != null {
        return 0
    }
    return 1
}
""",
            "E0483",
            "Non-null outer raw pointer null check (*?pq != null) continues to hit E0483",
        ),
        (
            "G6-02",
            "g6_02_unnarrowed_nullable_member_e0447_neg.tk",
            """shape Box(val: i32)

fn main() -> i32 {
    auto b = Box(val = 42)
    auto ?np = *b : ?Box
    unsafe {
        return np.val
    }
}
""",
            "E0447",
            "Unnarrowed nullable raw pointer member access (np.val) continues to hit E0447 even inside unsafe",
        ),
        (
            "G6-03",
            "g6_03_unnarrowed_nullable_bare_read_e0448_neg.tk",
            """fn main() -> i32 {
    auto x = 42:i32
    auto ?np = *x : ?i32
    unsafe {
        if np != 42 { return 1 }
    }
    return 0
}
""",
            "E0448",
            "Unnarrowed nullable raw pointer bare value use (if np != 42) continues to hit E0448 even inside unsafe",
        ),
        (
            "G6-04",
            "g6_04_uninitialized_read_e0410_neg.tk",
            """fn main() -> i32 {
    auto x = uninit : i32
    unsafe {
        return x
    }
}
""",
            "E0410",
            "Reading uninitialized storage (ERR_USE_UNSET / E0410) continues to hit E0410 even inside unsafe",
        ),
        (
            "G6-05",
            "g6_05_readonly_payload_write_e04572_neg.tk",
            """fn main() -> i32 {
    auto x# = 42:i32
    auto *#p1 = *x
    unsafe {
        p1 = 99
    }
    return 0
}
""",
            "E04572",
            "Writing pointee through *#p1 (rebindable slot, readonly payload) continues to hit E04572 inside unsafe",
        ),
        (
            "G6-06",
            "g6_06_raw_index_e04485_neg.tk",
            """fn read_idx(*p: [i32]) -> i32 {
    return p[0]
}

fn main() -> i32 {
    return 0
}
""",
            "E04485",
            "Raw pointer indexing p[0] outside unsafe continues to hit E04485",
        ),
        (
            "G6-07",
            "g6_07_mixed_raw_managed_param_e0495_neg.tk",
            """fn bad(cb: fn(&^i32) -> i32) -> i32 {
    return 0
}

fn main() -> i32 {
    return 0
}
""",
            "E0495",
            "Multi-layer handle parameter inside callable type fn(&^i32) -> i32 continues to hit E0495",
        ),
    ]
    for pid, fname, src, diag, desc in g6_specs:
        probes.append(
            run_source_probe(
                tokac,
                work_root,
                "G6_EXISTING_BOUNDARY_PRESERVATION",
                pid,
                "G6_existing_boundary_preservation",
                fname,
                src,
                False,
                diag,
                desc,
            )
        )

    # Part 2: Three-Category LLVM IR Evidence Verification
    def extract_ir_fn(ir_text: str, fn_name: str) -> str:
        m = re.search(rf"define [^\n]*@{re.escape(fn_name)}\b[^\n]*\{{.*?\n\}}", ir_text, flags=re.DOTALL)
        return m.group(0) if m else ""

    ir_dir = work_root / "ir"
    ir_dir.mkdir(parents=True, exist_ok=True)
    exe_suffix = ".exe" if os.name == "nt" else ".bin"
    ir_specs = [
        (
            "IR-1",
            "ir_cat1_safe_handle_slot_and_field_ops.tk",
            """pub shape RawCarrier(pub *p: i32)

pub fn unsafe_read_arr_elem(arr: [*i32; 2]) -> *i32 {
    auto *from_arr = arr[0]
    return *from_arr
}

pub fn unsafe_safe_handle_ops(*in_p: i32, *alt_p: i32) -> *i32 {
    auto *local_copy = *in_p
    auto *#rebind_slot = *local_copy
    *#rebind_slot = *alt_p
    auto box = RawCarrier(*p = *rebind_slot)
    auto *from_field = box.*p
    auto *from_arr = unsafe_read_arr_elem([*from_field, *in_p])
    return *from_arr
}

fn main() -> i32 {
    auto a = 10:i32
    auto b = 20:i32
    auto *res = unsafe_safe_handle_ops(*a, *b)
    if *res != *b { return 1 }
    return 0
}
""",
            "Category 1: Safe handle slot load/store, safe host hatted field selection, and safe array raw element copy outside unsafe",
            lambda ir_text: (
                bool(extract_ir_fn(ir_text, "unsafe_safe_handle_ops"))
                and ".peel_soul" not in extract_ir_fn(ir_text, "unsafe_safe_handle_ops")
                and "%raw.ident = load ptr" in extract_ir_fn(ir_text, "unsafe_safe_handle_ops")
            ),
        ),
        (
            "IR-2",
            "ir_cat2_unsafe_authorized_pointee_read_write.tk",
            """pub fn unsafe_pointee_rw(*in_p: i32, *out_p#: i32) -> i32 {
    unsafe {
        auto v = in_p
        out_p = v + 8
        return out_p
    }
}

fn main() -> i32 {
    auto a = 34:i32
    auto b# = 0:i32
    auto r = unsafe_pointee_rw(*a, *b)
    if r != 42 || b != 42 { return 1 }
    return 0
}
""",
            "Category 2: Authorized raw pointee indirect load and store inside unsafe",
            lambda ir_text: (
                ".peel_soul" in extract_ir_fn(ir_text, "unsafe_pointee_rw")
                and "load i32, ptr" in extract_ir_fn(ir_text, "unsafe_pointee_rw")
                and "store i32" in extract_ir_fn(ir_text, "unsafe_pointee_rw")
            ),
        ),
        (
            "IR-3",
            "ir_cat3_unsafe_authorized_ref_and_receiver.tk",
            """pub shape Counter(pub v: i32)
impl Counter {
    pub fn get(self) -> i32 {
        return self.v
    }
}

pub fn unsafe_ref_and_receiver(*p: i32, *cp: Counter) -> i32 {
    unsafe {
        auto &r = &p
        auto m = cp.get()
        return r + m
    }
}

fn main() -> i32 {
    auto x = 20:i32
    auto c = Counter(v = 22)
    auto res = unsafe_ref_and_receiver(*x, *c)
    if res != 42 { return 1 }
    return 0
}
""",
            "Category 3: Authorized safe reference construction (&p) and method receiver projection (cp.get()) from raw pointers inside unsafe",
            lambda ir_text: (
                ".peel_soul" in extract_ir_fn(ir_text, "unsafe_ref_and_receiver")
                and "load %Counter, ptr" in extract_ir_fn(ir_text, "unsafe_ref_and_receiver")
            ),
        ),
    ]
    ir_receipts = []
    for ir_id, fname, src, desc, pred in ir_specs:
        src_path = ir_dir / fname
        ll_path = ir_dir / f"{Path(fname).stem}.ll"
        bin_path = ir_dir / f"{Path(fname).stem}{exe_suffix}"
        src_path.write_text(src, encoding="utf-8", newline="\n")
        emit_res = run_cmd([tokac, "--emit-llvm", src_path, "-o", ll_path], ir_dir)
        build_res = run_cmd([tokac, src_path, "-o", bin_path], ir_dir)
        run_res = run_cmd([bin_path], ir_dir) if build_res["exit_code"] == 0 else None
        bin_path.unlink(missing_ok=True)
        ir_text = ll_path.read_text(encoding="utf-8") if ll_path.exists() else ""
        ir_receipts.append({
            "ir_id": ir_id,
            "description": desc,
            "ll_sha256": sha256_file(ll_path) if ll_path.exists() else None,
            "emit_llvm": emit_res,
            "build_binary": build_res,
            "run_binary": run_res,
            "passed": (
                emit_res["exit_code"] == 0
                and ll_path.exists()
                and pred(ir_text)
                and build_res["exit_code"] == 0
                and run_res is not None
                and run_res["exit_code"] == 0
            ),
        })

    # Part 3: Sourceless .tki + .o Consumption Matrix
    tki_root = work_root / "tki_matrix"
    tki_root.mkdir(parents=True, exist_ok=True)
    tki_receipts = []

    def run_consumer_suite(
        case_id: str,
        case_dir: Path,
        producer_stem: str,
        consumer_name: str,
        consumer_src: str,
        expect_pass: bool,
        expected_diag: str,
        forbidden_diag: str,
        provenance: str,
    ):
        producer_tk = case_dir / f"{producer_stem}.tk"
        producer_tki = case_dir / f"{producer_stem}.tki"
        producer_o = case_dir / f"{producer_stem}.o"
        assert not producer_tk.exists(), f"Producer source {producer_tk} must be physically deleted before consumer runs!"
        assert producer_tki.exists(), f"Missing {producer_tki}"

        c_path = case_dir / consumer_name
        c_path.write_text(consumer_src, encoding="utf-8", newline="\n")
        c_stem = c_path.stem
        c_obj = case_dir / f"{c_stem}.o"
        c_ll = case_dir / f"{c_stem}.ll"
        c_bin = case_dir / f"{c_stem}{exe_suffix}"
        for p in (c_obj, c_ll, c_bin):
            p.unlink(missing_ok=True)

        check_res = run_cmd([tokac, "--check-only", c_path], case_dir)
        obj_res = run_cmd([tokac, "-c", c_path, "-o", c_obj], case_dir)
        ir_res = run_cmd([tokac, "--emit-llvm", c_path, "-o", c_ll], case_dir)

        artifacts_state = {
            "producer_tk_exists": producer_tk.exists(),
            "producer_tki_exists": producer_tki.exists(),
            "producer_o_exists": producer_o.exists(),
            "consumer_o_exists": c_obj.exists(),
            "consumer_ll_exists": c_ll.exists(),
        }

        build_bin_res = None
        run_bin_res = None
        if expect_pass:
            build_bin_res = run_cmd([tokac, c_path, producer_o, "-o", c_bin], case_dir)
            if build_bin_res["exit_code"] == 0 and c_bin.exists():
                run_bin_res = run_cmd([c_bin], case_dir)
            c_bin.unlink(missing_ok=True)
            c_obj.unlink(missing_ok=True)
            c_ll.unlink(missing_ok=True)

        check_diags = extract_diagnostics(check_res["stderr"])
        obj_diags = extract_diagnostics(obj_res["stderr"])
        ir_diags = extract_diagnostics(ir_res["stderr"])

        if expect_pass:
            passed = (
                not artifacts_state["producer_tk_exists"]
                and check_res["exit_code"] == 0
                and obj_res["exit_code"] == 0
                and ir_res["exit_code"] == 0
                and artifacts_state["consumer_o_exists"]
                and artifacts_state["consumer_ll_exists"]
                and build_bin_res is not None
                and build_bin_res["exit_code"] == 0
                and run_bin_res is not None
                and run_bin_res["exit_code"] == 0
            )
        else:
            err_check = [d["code"] for d in check_diags if d["level"] == "error"]
            err_obj = [d["code"] for d in obj_diags if d["level"] == "error"]
            err_ir = [d["code"] for d in ir_diags if d["level"] == "error"]
            passed = (
                not artifacts_state["producer_tk_exists"]
                and check_res["exit_code"] == 1
                and obj_res["exit_code"] == 1
                and ir_res["exit_code"] == 1
                and not artifacts_state["consumer_o_exists"]
                and not artifacts_state["consumer_ll_exists"]
                and expected_diag in err_check
                and expected_diag in err_obj
                and expected_diag in err_ir
                and (forbidden_diag is None or (
                    forbidden_diag not in err_check
                    and forbidden_diag not in err_obj
                    and forbidden_diag not in err_ir
                ))
            )

        return {
            "case_id": case_id,
            "provenance": provenance,
            "producer_tki_sha256": sha256_file(producer_tki),
            "expect_pass": expect_pass,
            "expected_diag": expected_diag,
            "forbidden_diag": forbidden_diag,
            "passed": passed,
            "artifacts_state": artifacts_state,
            "modes": {
                "check_only": check_res,
                "compile_object": obj_res,
                "emit_llvm": ir_res,
                "build_binary": build_bin_res,
                "run_binary": run_bin_res,
            },
            "diagnostics": {
                "check_only": check_diags,
                "compile_object": obj_diags,
                "emit_llvm": ir_diags,
            },
        }

    # TKI-1
    tki1_dir = tki_root / "TKI1_new_producer_consumer"
    tki1_dir.mkdir(parents=True, exist_ok=True)
    tki1_prod_tk = tki1_dir / "raw_api.tk"
    tki1_prod_tk.write_text(
        """pub shape RawCarrier(pub *p: i32, pub *?pq: i32)

pub fn unsafe_passthrough(*p: i32) -> *i32 {
    return *p
}

pub fn unsafe_read_via_unsafe(*p: i32) -> i32 {
    auto *keep = *p
    unsafe {
        return keep
    }
}
""",
        encoding="utf-8",
        newline="\n",
    )
    run_cmd([tokac, "-c", "--emit-interface", tki1_prod_tk, "-o", tki1_dir / "raw_api.o"], tki1_dir)
    tki1_prod_tk.unlink()

    tki_receipts.append(
        run_consumer_suite(
            "TKI-1-POS",
            tki1_dir,
            "raw_api",
            "consumer_pos.tk",
            """import ./raw_api::{RawCarrier, unsafe_passthrough, unsafe_read_via_unsafe}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto ?q = *x : ?i32
    auto *h = unsafe_passthrough(*p)
    auto carrier = RawCarrier(*p = *h, *?pq = *?q)
    auto *fh = carrier.*p
    auto v1 = unsafe_read_via_unsafe(*fh)
    unsafe {
        auto v2 = h
        auto v3 = carrier.p
        if v1 != 42 || v2 != 42 || v3 != 42 { return 1 }
    }
    return 0
}
""",
            True,
            None,
            None,
            "0.20.1 sourceless producer + consumer positive",
        )
    )
    tki_receipts.append(
        run_consumer_suite(
            "TKI-1-NEG-RETURN",
            tki1_dir,
            "raw_api",
            "consumer_neg_return.tk",
            """import ./raw_api::{unsafe_passthrough}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto *h = unsafe_passthrough(*p)
    return h
}
""",
            False,
            "E0625",
            "E0901",
            "0.20.1 sourceless consumer bare read of imported raw pointer return outside unsafe",
        )
    )
    tki_receipts.append(
        run_consumer_suite(
            "TKI-1-NEG-FIELD",
            tki1_dir,
            "raw_api",
            "consumer_neg_field.tk",
            """import ./raw_api::{RawCarrier}

fn main() -> i32 {
    auto x = 42:i32
    auto ?q = *x : ?i32
    auto carrier = RawCarrier(*p = *x, *?pq = *?q)
    return carrier.p
}
""",
            False,
            "E0625",
            "E0901",
            "0.20.1 sourceless consumer hat-off read of imported RawCarrier.*p field outside unsafe",
        )
    )

    # TKI-2
    tki2_dir = tki_root / "TKI2_unsafe_fn_roundtrip"
    tki2_dir.mkdir(parents=True, exist_ok=True)
    tki2_prod_tk = tki2_dir / "unsafe_api.tk"
    tki2_prod_tk.write_text(
        """pub unsafe fn write_and_read(*out#: i32, val: i32) -> i32 {
    unsafe {
        out = val
        return out
    }
}
""",
        encoding="utf-8",
        newline="\n",
    )
    run_cmd([tokac, "-c", "--emit-interface", tki2_prod_tk, "-o", tki2_dir / "unsafe_api.o"], tki2_dir)
    tki2_prod_tk.unlink()

    tki_receipts.append(
        run_consumer_suite(
            "TKI-2-NEG-SAFE-CALL",
            tki2_dir,
            "unsafe_api",
            "consumer_safe_call_neg.tk",
            """import ./unsafe_api::{write_and_read}

fn main() -> i32 {
    auto x# = 10:i32
    auto *p# = *x
    return write_and_read(*p, 42)
}
""",
            False,
            "E0623",
            "E0901",
            "0.20.1 sourceless consumer calling imported pub unsafe fn outside unsafe -> E0623",
        )
    )
    tki_receipts.append(
        run_consumer_suite(
            "TKI-2-POS-UNSAFE-CALL",
            tki2_dir,
            "unsafe_api",
            "consumer_unsafe_call_pos.tk",
            """import ./unsafe_api::{write_and_read}

fn main() -> i32 {
    auto x# = 10:i32
    auto *p# = *x
    unsafe {
        auto r = write_and_read(*p, 42)
        if r != 42 { return 1 }
    }
    if x != 42 { return 2 }
    return 0
}
""",
            True,
            None,
            None,
            "0.20.1 sourceless consumer calling imported pub unsafe fn inside unsafe -> rc=0",
        )
    )

    # TKI-3
    tki3_dir = tki_root / "TKI3_generic_and_local_body_replay"
    tki3_dir.mkdir(parents=True, exist_ok=True)
    tki3_prod_tk = tki3_dir / "replay_ok.tk"
    tki3_prod_tk.write_text(
        """pub fn relay<T>(cede x: T) -> T {
    return cede x
}

pub fn unsafe_safe_read_generic<T>(*p: T) -> T {
    auto *keep = *p
    unsafe {
        return keep
    }
}

fn helper_read(v: i32) -> i32 {
    auto *p = *v
    unsafe {
        return p
    }
}

pub fn make_safe_reader() -> fn(i32) -> i32 {
    return { q => helper_read(q) }:fn(i32) -> i32
}
""",
        encoding="utf-8",
        newline="\n",
    )
    run_cmd([tokac, "-c", "--emit-interface", tki3_prod_tk, "-o", tki3_dir / "replay_ok.o"], tki3_dir)
    tki3_prod_tk.unlink()

    tki_receipts.append(
        run_consumer_suite(
            "TKI-3-POS-REPLAY",
            tki3_dir,
            "replay_ok",
            "consumer_replay_pos.tk",
            """import ./replay_ok::{relay, unsafe_safe_read_generic, make_safe_reader}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    auto *p_ref = *x
    auto *r = relay<*i32>(cede *p)
    if *r != *p_ref { return 1 }
    auto v1 = unsafe_safe_read_generic<i32>(*r)
    auto reader = make_safe_reader()
    auto v2 = reader(42)
    if v1 != 42 || v2 != 42 { return 2 }
    return 0
}
""",
            True,
            None,
            None,
            "0.20.1 sourceless consumer replaying whole-value relay<*i32> and unsafe-authorized generic/local bodies",
        )
    )

    # TKI-4a & TKI-4b: First verify 0.20.1 tokac rejects direct export of unchecked generic body
    tki4_dir = tki_root / "TKI4_cache_and_body_isolation"
    tki4_dir.mkdir(parents=True, exist_ok=True)
    refuse_dir = tki4_dir / "step0_0201_refuses_export"
    refuse_dir.mkdir(parents=True, exist_ok=True)
    (refuse_dir / "legacy_leak.tk").write_text(
        """pub fn unsafe_leak_generic<T>(*p: T) -> T {
    return p
}

pub fn trigger_in_producer() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return unsafe_leak_generic<i32>(*p)
}
""",
        encoding="utf-8",
        newline="\n",
    )
    res_refuse = run_cmd([tokac, "-c", "--emit-interface", "legacy_leak.tk", "-o", "legacy_leak.o"], refuse_dir)
    (refuse_dir / "legacy_leak.tk").unlink()
    if res_refuse["exit_code"] != 1 or "E0625" not in res_refuse["stderr"]:
        raise RuntimeError(f"Expected 0.20.1 tokac to refuse exporting legacy_leak.tk with E0625, got: {res_refuse}")

    # Export a clean seed .tki + .o using current tokac so we get exact header format & valid .o
    seed_dir = tki4_dir / "seed"
    seed_dir.mkdir(parents=True, exist_ok=True)
    (seed_dir / "legacy_leak.tk").write_text(
        """pub fn unsafe_leak_generic<T>(*p: T) -> T {
    auto *keep = *p
    unsafe {
        return keep
    }
}
""",
        encoding="utf-8",
        newline="\n",
    )
    run_cmd([tokac, "-c", "--emit-interface", "legacy_leak.tk", "-o", "legacy_leak.o"], seed_dir)
    (seed_dir / "legacy_leak.tk").unlink()
    seed_tki_text = (seed_dir / "legacy_leak.tki").read_text(encoding="utf-8")
    m_triple = re.search(r"// @meta target_triple: ([^\n]+)", seed_tki_text)
    host_triple = m_triple.group(1).strip() if m_triple else "arm64-apple-darwin25.5.0"

    # Exact v0.20.0-exported .tki with genuine replay_surface_hash beb59b92660738cd
    v0200_legacy_leak_tki = f"""// @meta compiler_version: 0.20.0-1
// @meta local_body_policy: checked-local-v1
// @meta local_body_definitions: f/0
// @meta format_version: 7
// @meta language_epoch: 0.20
// @meta encap_epoch: 0.19
// @meta replay_surface_hash: beb59b92660738cd
// @meta target_triple: {host_triple}
// @meta source_hash: 9412392f6e9b6e99
// @meta source_path: legacy_leak.tk
// @meta identity_schema_version: 2
// @meta place_yield_abi_schema: 1
// @meta logical_module_path: unbound
// @meta resolver_binding_digest: unbound


pub fn unsafe_leak_generic<T>(*p: T) -> T {{
  return p
}}
pub fn trigger_in_producer() -> i32

"""

    # TKI-4a-01: Stale compiler_version: 0.20.0-1 + stale local_body_policy: checked-local-v1 -> E0901
    tki4a1_dir = tki4_dir / "TKI4a_01_v0200_identity_rejected_e0901"
    tki4a1_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(seed_dir / "legacy_leak.o", tki4a1_dir / "legacy_leak.o")
    (tki4a1_dir / "legacy_leak.tki").write_text(v0200_legacy_leak_tki, encoding="utf-8", newline="\n")

    tki_receipts.append(
        run_consumer_suite(
            "TKI-4a-01-STALE-VERSION-E0901",
            tki4a1_dir,
            "legacy_leak",
            "consumer_4a1.tk",
            """import ./legacy_leak::{unsafe_leak_generic}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return unsafe_leak_generic<i32>(*p)
}
""",
            False,
            "E0901",
            None,
            "Stale 0.20.0-1 compiler_version in .tki rejected at header check with E0901",
        )
    )

    # TKI-4a-02: compiler_version: any + stale local_body_policy: checked-local-v1 -> E0901
    tki4a2_dir = tki4_dir / "TKI4a_02_any_version_stale_policy_rejected_e0901"
    tki4a2_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(seed_dir / "legacy_leak.o", tki4a2_dir / "legacy_leak.o")
    tki4a2_text = v0200_legacy_leak_tki.replace(
        "// @meta compiler_version: 0.20.0-1",
        "// @meta compiler_version: any",
    )
    (tki4a2_dir / "legacy_leak.tki").write_text(tki4a2_text, encoding="utf-8", newline="\n")

    tki_receipts.append(
        run_consumer_suite(
            "TKI-4a-02-STALE-POLICY-E0901",
            tki4a2_dir,
            "legacy_leak",
            "consumer_4a2.tk",
            """import ./legacy_leak::{unsafe_leak_generic}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    return unsafe_leak_generic<i32>(*p)
}
""",
            False,
            "E0901",
            None,
            "compiler_version: any + stale local_body_policy: checked-local-v1 rejected at policy check with E0901",
        )
    )

    # TKI-4b-01: Current identity + unchecked generic body -> passes header check (zero E0901) and rejected by Sema replay with E0625
    tki4b1_dir = tki4_dir / "TKI4b_01_new_identity_generic_body_rejected_e0625"
    tki4b1_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(seed_dir / "legacy_leak.o", tki4b1_dir / "legacy_leak.o")
    tki4b1_text = (
        v0200_legacy_leak_tki
        .replace("// @meta compiler_version: 0.20.0-1", "// @meta compiler_version: 0.20.1-1")
        .replace("// @meta local_body_policy: checked-local-v1", "// @meta local_body_policy: checked-local-v1-0201")
    )
    (tki4b1_dir / "legacy_leak.tki").write_text(tki4b1_text, encoding="utf-8", newline="\n")

    tki_receipts.append(
        run_consumer_suite(
            "TKI-4b-01-NEW-IDENTITY-GENERIC-BODY-E0625",
            tki4b1_dir,
            "legacy_leak",
            "consumer_4b1.tk",
            """import ./legacy_leak::{unsafe_leak_generic}

fn main() -> i32 {
    auto x = 42:i32
    auto *p = *x
    unsafe {
        return unsafe_leak_generic<i32>(*p)
    }
}
""",
            False,
            "E0625",
            "E0901",
            "Current 0.20.1 identity + unchecked generic body passes header check (no E0901) and rejected by Sema replay with E0625 even when caller is in unsafe",
        )
    )

    # TKI-4b-02: Current identity + unchecked InterfaceLocalBody helper -> passes header check (zero E0901) and rejected by Sema local body replay with E0625
    tki4b2_dir = tki4_dir / "TKI4b_02_new_identity_local_body_rejected_e0625"
    tki4b2_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(seed_dir / "legacy_leak.o", tki4b2_dir / "legacy_local.o")
    v0200_legacy_local_tki = f"""// @meta compiler_version: 0.20.1-1
// @meta local_body_policy: checked-local-v1-0201
// @meta local_body_definitions: f/0,f/1
// @meta format_version: 7
// @meta language_epoch: 0.20
// @meta encap_epoch: 0.19
// @meta replay_surface_hash: 2edbf5311ad2dafb
// @meta target_triple: {host_triple}
// @meta source_hash: 20f77d2e9d376974
// @meta source_path: legacy_local.tk
// @meta identity_schema_version: 2
// @meta place_yield_abi_schema: 1
// @meta logical_module_path: unbound
// @meta resolver_binding_digest: unbound



fn raw_helper(v: i32) -> i32 {{
  auto *p = *v
  return p
}}
pub fn make_reader() -> fn(i32)->i32 {{
  return {{
    q=>
    return raw_helper(q)
  }}:fn(i32)->i32
}}

"""
    (tki4b2_dir / "legacy_local.tki").write_text(v0200_legacy_local_tki, encoding="utf-8", newline="\n")

    tki_receipts.append(
        run_consumer_suite(
            "TKI-4b-02-NEW-IDENTITY-LOCAL-BODY-E0625",
            tki4b2_dir,
            "legacy_local",
            "consumer_4b2.tk",
            """import ./legacy_local::{make_reader}

fn main() -> i32 {
    auto reader = make_reader()
    return reader(42)
}
""",
            False,
            "E0625",
            "E0901",
            "Current 0.20.1 identity + unchecked InterfaceLocalBody helper passes header check (no E0901) and rejected by Sema local body replay with E0625",
        )
    )

    all_probes_passed = all(p["passed"] for p in probes)
    all_ir_passed = all(r["passed"] for r in ir_receipts)
    all_tki_passed = all(t["passed"] for t in tki_receipts)
    all_passed = all_probes_passed and all_ir_passed and all_tki_passed

    return {
        "tokac_binary": str(tokac),
        "tokac_sha256": sha256_file(tokac),
        "summary": {
            "source_probes_total": len(probes),
            "source_probes_passed": sum(1 for p in probes if p["passed"]),
            "ir_categories_total": len(ir_receipts),
            "ir_categories_passed": sum(1 for r in ir_receipts if r["passed"]),
            "tki_cases_total": len(tki_receipts),
            "tki_cases_passed": sum(1 for t in tki_receipts if t["passed"]),
            "all_passed": all_passed,
        },
        "failed_items": (
            [p["probe_id"] for p in probes if not p["passed"]]
            + [r["ir_id"] for r in ir_receipts if not r["passed"]]
            + [t["case_id"] for t in tki_receipts if not t["passed"]]
        ),
        "source_control_groups": probes,
        "ir_comparison": ir_receipts,
        "sourceless_tki_matrix": tki_receipts,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=str(REPO_ROOT / "build"))
    parser.add_argument("--output-receipt", default=None)
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    tokac = resolve_tokac(build_dir)

    with tempfile.TemporaryDirectory(prefix="toka-raw-ptr-unsafe-0201-") as tmp:
        payload = execute_suite(tokac, Path(tmp))

    if args.output_receipt:
        out_path = Path(args.output_receipt).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8", newline="\n")

    summary = payload["summary"]
    print(
        f"toka_raw_pointer_unsafe_0201: "
        f"source_probes={summary['source_probes_passed']}/{summary['source_probes_total']}, "
        f"ir_categories={summary['ir_categories_passed']}/{summary['ir_categories_total']}, "
        f"tki_cases={summary['tki_cases_passed']}/{summary['tki_cases_total']}, "
        f"all_passed={summary['all_passed']}"
    )
    if not summary["all_passed"]:
        print(f"FAILED items: {payload['failed_items']}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
