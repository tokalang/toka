#!/usr/bin/env python3
"""Comprehensive gate test runner for Slab operations, borrow constraints, Option reset protocol, and reference patterns."""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command, env=None, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_negative(tokac, source, env, tmp_dir, expected_code, expected_loc=None):
    require(source.exists(), f"missing test source: {source}")
    # 1. --check-only: exact returncode 1, error code, location
    res = run([str(tokac), "--check-only", str(source)], env=env)
    require(res.returncode == 1, f"{source.name} returncode {res.returncode} != 1 in check-only:\n{res.stderr}")
    require(expected_code in res.stderr, f"{source.name} missing expected {expected_code} in stderr:\n{res.stderr}")
    if expected_loc:
        require(expected_loc in res.stderr, f"{source.name} missing expected location '{expected_loc}' in stderr:\n{res.stderr}")

    # 2. Shadow comparison: exact returncode 1 and strict stderr parity
    shadow_res = run([str(tokac), "--non-call-transfer-shadow=json", "--check-only", str(source)], env=env)
    require(shadow_res.returncode == 1,
            f"{source.name} shadow returncode {shadow_res.returncode} != 1:\n{shadow_res.stderr}")
    require(shadow_res.stderr == res.stderr,
            f"{source.name} shadow stderr mismatch with normal:\nShadow:\n{shadow_res.stderr}\nNormal:\n{res.stderr}")

    # 3. Object file emission failure: returncode 1, target error code, target location, no .o
    obj = tmp_dir / f"{source.stem}_fail.o"
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj)], env=env)
    require(obj_res.returncode == 1, f"{source.name} -c returncode {obj_res.returncode} != 1:\n{obj_res.stderr}")
    require(not obj.exists(), f"{source.name} must not emit .o on error")
    require(expected_code in obj_res.stderr, f"{source.name} -c missing expected {expected_code} in stderr:\n{obj_res.stderr}")
    if expected_loc:
        require(expected_loc in obj_res.stderr, f"{source.name} -c missing expected location '{expected_loc}' in stderr:\n{obj_res.stderr}")

    # 4. LLVM IR emission failure: returncode 1, target error code, target location, no .ll
    ll = tmp_dir / f"{source.stem}_fail.ll"
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll)], env=env)
    require(ll_res.returncode == 1, f"{source.name} --emit-llvm returncode {ll_res.returncode} != 1:\n{ll_res.stderr}")
    require(not ll.exists(), f"{source.name} must not emit .ll on error")
    require(expected_code in ll_res.stderr, f"{source.name} --emit-llvm missing expected {expected_code} in stderr:\n{ll_res.stderr}")
    if expected_loc:
        require(expected_loc in ll_res.stderr, f"{source.name} --emit-llvm missing expected location '{expected_loc}' in stderr:\n{ll_res.stderr}")


def check_positive(tokac, source, env, tmp_dir, expected_output=None):
    require(source.exists(), f"missing test source: {source}")
    # 1. Normal check-only and shadow check-only: returncode 0 and strict stderr parity
    normal_res = run([str(tokac), "--check-only", str(source)], env=env)
    require(normal_res.returncode == 0, f"failed to check {source.name}:\n{normal_res.stderr}")
    shadow_res = run([str(tokac), "--non-call-transfer-shadow=json", "--check-only", str(source)], env=env)
    require(shadow_res.returncode == 0, f"{source.name} failed shadow check:\n{shadow_res.stderr}")
    require(shadow_res.stderr == normal_res.stderr,
            f"{source.name} positive shadow stderr mismatch:\nShadow:\n{shadow_res.stderr}\nNormal:\n{normal_res.stderr}")

    # 2. Compile & run executable
    exe = tmp_dir / source.stem
    compiled = run([str(tokac), str(source), "-o", str(exe)], env=env)
    require(compiled.returncode == 0, f"failed to compile {source.name}:\n{compiled.stderr}")
    executed = run([str(exe)], env=env)
    require(executed.returncode == 0, f"failed to execute {source.name} (code {executed.returncode}):\n{executed.stderr}")
    if expected_output:
        require(expected_output in executed.stdout,
                f"{source.name} output mismatch:\nExpected substring:\n{expected_output}\nActual stdout:\n{executed.stdout}")

    # 3. Object file emission
    obj_file = tmp_dir / f"{source.stem}.o"
    obj_res = run([str(tokac), "-c", str(source), "-o", str(obj_file)], env=env)
    require(obj_res.returncode == 0 and obj_file.exists() and obj_file.stat().st_size > 0,
            f"Object file emission failed for {source.name}:\n{obj_res.stderr}")

    # 4. LLVM IR emission
    ll_file = tmp_dir / f"{source.stem}.ll"
    ll_res = run([str(tokac), "--emit-llvm", str(source), "-o", str(ll_file)], env=env)
    require(ll_res.returncode == 0 and ll_file.exists() and ll_file.stat().st_size > 0,
            f"LLVM-IR emission failed for {source.name}:\n{ll_res.stderr}")


def trace_root(val, lines, params, visited=None, ops=None, line_idx=None):
    if visited is None:
        visited = set()
    if ops is None:
        ops = []
    if val in visited:
        return ('cycle', val, float('inf'))
    visited.add(val)
    if val in params:
        return ('param', val, 0)

    start_i = len(lines) - 1 if line_idx is None else line_idx - 1
    for i in range(start_i, -1, -1):
        line = lines[i].strip()
        m_def = re.match(rf'^{re.escape(val)}\s*=\s*(.+)$', line)
        if m_def:
            rhs = m_def.group(1)
            if rhs.startswith('alloca'):
                return ('alloca', val, 0)
            m_load = re.search(r'load\s+[^,]+,\s*ptr\s+([%][a-zA-Z0-9_.]+)', rhs)
            if m_load:
                ops.append('load')
                ptr_var = m_load.group(1)
                found_store = False
                for j in range(i - 1, -1, -1):
                    s_line = lines[j].strip()
                    m_store = re.search(r'store\s+[^,]+\s+([%@][a-zA-Z0-9_.]+),\s*ptr\s+' + re.escape(ptr_var), s_line)
                    if m_store:
                        stored_val = m_store.group(1)
                        found_store = True
                        return trace_root(stored_val, lines, params, visited, ops, line_idx=j)
                if not found_store:
                    return trace_root(ptr_var, lines, params, visited, ops, line_idx=i)
            m_gep = re.search(r'getelementptr\s+(?:inbounds\s+)?(?:nuw\s+)?(.*),\s*ptr\s+([%][a-zA-Z0-9_.]+)(.*)', rhs)
            if m_gep:
                ops.append('gep')
                gep_type = m_gep.group(1).strip()
                base = m_gep.group(2)
                indices_str = m_gep.group(3)
                idx_tokens = re.findall(r'(?:i32|i64)\s+(-?\d+)', indices_str)
                if all(int(x) == 0 for x in idx_tokens):
                    add_offset = 0
                elif gep_type == 'i8' and len(idx_tokens) == 1:
                    add_offset = int(idx_tokens[0])
                else:
                    add_offset = float('inf')
                base_root = trace_root(base, lines, params, visited, ops, line_idx=i)
                return (base_root[0], base_root[1], base_root[2] + add_offset)
            m_cast = re.search(r'(?:inttoptr|ptrtoint|bitcast)\s+.*([%][a-zA-Z0-9_.]+)', rhs)
            if m_cast:
                ops.append('cast')
                base = m_cast.group(1)
                return trace_root(base, lines, params, visited, ops, line_idx=i)
            m_call = re.search(r'call\s+.*@([a-zA-Z0-9_.]+)', rhs)
            if m_call:
                return ('call', val, 0)
            return ('unknown_def', val, float('inf'))
    return ('unknown', val, float('inf'))


def verify_option_reset_layout_and_protocol(ll_path):
    ll_path = pathlib.Path(ll_path)
    require(ll_path.exists(), f"missing LLVM IR file: {ll_path}")
    content = ll_path.read_text()

    # 1. Verify SlabEntry has Option as its first struct field (offset 0)
    slab_entry_matches = re.findall(r'%(SlabEntry_M_[^\s=]+)\s*=\s*type\s*\{\s*(%Option_M_[^\s,}]+)', content)
    require(len(slab_entry_matches) > 0, "SlabEntry struct type with Option field 0 not found in LLVM IR")

    # 2. Verify each Option referenced by SlabEntry has i8 discriminant tag as its first field (offset 0)
    for entry_name, opt_name in slab_entry_matches:
        opt_pattern = rf'{re.escape(opt_name)}\s*=\s*type\s*\{{\s*i8'
        require(re.search(opt_pattern, content) is not None,
                f"Option struct type {opt_name} in {entry_name} does not have an i8 tag at offset 0")

    # 3. Verify in Slab::remove:
    #    a) slab_take_raw_option is called
    #    b) tag zeroing (store i8 0) occurs after take
    #    c) no intervening user calls / drops between take and zeroing
    #    d) store target points to the exact same slot being taken (offset 0)
    match = re.search(r'define [^\n]+@Slab_M_[^\n]+_remove\(([^)]+)\)(?: comdat)?\s*\{', content)
    require(match is not None, "Slab remove function not found in LLVM IR")
    params = re.findall(r'[%][a-zA-Z0-9_.]+', match.group(1))
    func_start = match.start()
    func_end = content.find('\n}\n', func_start)
    body = content[func_start:func_end]
    body_lines = body.splitlines()

    take_hex = "slab_take_raw_option".encode().hex()
    take_pos = body.find(take_hex)
    if take_pos == -1:
        take_pos = body.find("slab_take_raw_option")
    zero_pos = body.find("store i8 0")

    require(take_pos != -1, "slab_take_raw_option call not found in Slab::remove")
    require(zero_pos != -1, "store i8 0 not found in Slab::remove")
    require(take_pos < zero_pos, "slab_take_raw_option must precede store i8 0 tag reset")

    # Check intervening instructions between end of take call line and store i8 0
    call_line_end = body.find("\n", take_pos)
    between = body[call_line_end:zero_pos]
    intervening_calls = [line.strip() for line in between.splitlines() if "call " in line]
    require(len(intervening_calls) == 0,
            f"No intervening calls permitted between Option payload take and tag reset: {intervening_calls}")

    take_idx = None
    take_arg = None
    for idx, l in enumerate(body_lines):
        if take_hex in l or "slab_take_raw_option" in l:
            m = re.search(r'call\s+.*@.*\(\s*ptr\s+([%][a-zA-Z0-9_.]+)', l)
            if m:
                take_idx = idx
                take_arg = m.group(1)
                break
    require(take_arg is not None, "could not extract argument to slab_take_raw_option")

    zero_idx = None
    zero_dest = None
    for idx, l in enumerate(body_lines):
        if "store i8 0" in l:
            m = re.search(r'store\s+i8\s+0,\s*ptr\s+([%][a-zA-Z0-9_.]+)', l)
            if m:
                zero_idx = idx
                zero_dest = m.group(1)
                break
    require(zero_dest is not None, "could not extract target of store i8 0")

    root_take = trace_root(take_arg, body_lines, params, line_idx=take_idx)
    root_zero = trace_root(zero_dest, body_lines, params, line_idx=zero_idx)
    require(root_take == root_zero and root_take[2] == 0,
            f"zero store target ({root_zero}) must trace to the exact same slot address (offset 0) as taken Option ({root_take})")


def verify_generic_reference_pattern_ir(ll_path):
    ll_path = pathlib.Path(ll_path)
    require(ll_path.exists(), f"missing LLVM IR file: {ll_path}")
    content = ll_path.read_text()

    # Find borrow_value function
    hex_name = "borrow_value".encode().hex()
    m = re.search(r'define [^\n]+(?:' + hex_name + r'|borrow_value)[^\n]*\(([^)]+)\)\s*\{', content)
    require(m is not None, "borrow_value function not found in LLVM IR")
    params = re.findall(r'[%][a-zA-Z0-9_.]+', m.group(1))
    func_start = m.start()
    func_end = content.find('\n}\n', func_start)
    body = content[func_start:func_end]
    body_lines = body.splitlines()

    # Verify generic view loading referent directly from source GEP, not stack escape
    require("%generic.outer_view = load ptr, ptr %value" in body,
            "borrow_value must load generic view referent from %value")
    require("ret ptr %generic.outer_view" in body,
            "borrow_value must return %generic.outer_view referent pointer")

    # In case_Some, verify %value stores the payload address projected from %source,
    # with exactly one store to %value in the function (checked unique-store / no-overwrite pattern)
    stores_to_value = []
    for idx, l in enumerate(body_lines):
        m_s = re.search(r'store\s+ptr\s+([%][a-zA-Z0-9_.]+),\s*ptr\s+%value\b', l)
        if m_s:
            stores_to_value.append((idx, m_s.group(1), l.strip()))
    require(len(stores_to_value) == 1,
            f"expected exactly one store to %value without overwrites, found {len(stores_to_value)}: {stores_to_value}")

    store_idx, stored_val, store_line = stores_to_value[0]

    curr_label = None
    for j in range(store_idx, -1, -1):
        lbl_m = re.match(r'^([a-zA-Z0-9_.]+):', body_lines[j].strip())
        if lbl_m:
            curr_label = lbl_m.group(1)
            break
    require(curr_label == "case_Some", f"store to %value must be located in case_Some, found in {curr_label}")

    ops = []
    root = trace_root(stored_val, body_lines, params, ops=ops, line_idx=store_idx)
    require(root[0] == "param", f"stored payload address must trace to function parameter, got {root}")
    require("gep" in ops, "payload address must be projected from parameter via getelementptr")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()

    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    fixture_dir = ROOT / "tests/semantics/slab"
    require(fixture_dir.exists(), f"missing checked-in fixture directory: {fixture_dir}")

    # Part 1: Positive test fixtures (lifecycle, ended_loan_positive, generic borrows for all quadrants)
    target_positives = [
        "lifecycle.tk",
        "ended_loan_positive.tk",
        "generic_borrow_soul.tk",
        "generic_borrow_unique.tk",
        "generic_borrow_shared.tk",
        "generic_borrow_reference.tk",
    ]

    with tempfile.TemporaryDirectory(prefix="toka-slab-positives-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        for rel_name in target_positives:
            source = fixture_dir / rel_name
            check_positive(tokac, source, env, tmp_dir)

    # Part 2: Negative test fixtures with exact diagnostic matching and artifact check
    target_negatives = [
        ("clear_live_loan.tk", "E0441", "clear_live_loan.tk:10"),
        ("remove_live_loan.tk", "E0441", "remove_live_loan.tk:10"),
        ("local_escape.tk", "E0455", "local_escape.tk:9"),
        ("readonly_reject.tk", "E0443", "readonly_reject.tk:6"),
        ("readonly_nested_write.tk", "E04573", "readonly_nested_write.tk:12"),
    ]

    with tempfile.TemporaryDirectory(prefix="toka-slab-negatives-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        for rel_name, code, loc in target_negatives:
            source = fixture_dir / rel_name
            check_negative(tokac, source, env, tmp_dir, code, loc)

    # Part 3: Option reset layout & protocol verification on emitted LLVM IR
    with tempfile.TemporaryDirectory(prefix="toka-slab-protocol-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        ll_file = tmp_dir / "lifecycle.ll"
        res = run([str(tokac), "--emit-llvm", str(fixture_dir / "lifecycle.tk"), "-o", str(ll_file)], env=env)
        require(res.returncode == 0, f"failed to emit LLVM IR for lifecycle.tk:\n{res.stderr}")
        verify_option_reset_layout_and_protocol(ll_file)

    # Part 3b: Negative controls for Option reset layout & protocol
    with tempfile.TemporaryDirectory(prefix="toka-slab-oracle-neg1-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        wrong_tag_ll = tmp_dir / "wrong_tag.ll"
        wrong_tag_ll.write_text("""
%Option_M_actual = type { i64 }
%Option_M_unrelated = type { i8 }
%SlabEntry_M_test = type { %Option_M_actual, i32 }
declare void @slab_take_raw_option(ptr)
define void @Slab_M_test_remove(ptr %entry, ptr %unrelated) {
start:
  call void @slab_take_raw_option(ptr %entry)
  store i8 0, ptr %entry
  ret void
}
""")
        neg_tag_passed = False
        try:
            verify_option_reset_layout_and_protocol(wrong_tag_ll)
        except RuntimeError:
            neg_tag_passed = True
        require(neg_tag_passed, "verify_option_reset_layout_and_protocol must reject invalid Option tag type")

        wrong_target_ll = tmp_dir / "wrong_target.ll"
        wrong_target_ll.write_text("""
%Option_M_actual = type { i8 }
%SlabEntry_M_test = type { %Option_M_actual, i32 }
declare void @slab_take_raw_option(ptr)
define void @Slab_M_test_remove(ptr %entry, ptr %unrelated) {
start:
  call void @slab_take_raw_option(ptr %entry)
  store i8 0, ptr %unrelated
  ret void
}
""")
        neg_target_passed = False
        try:
            verify_option_reset_layout_and_protocol(wrong_target_ll)
        except RuntimeError:
            neg_target_passed = True
        require(neg_target_passed, "verify_option_reset_layout_and_protocol must reject unrelated zero store target")

        wrong_offset_ll = tmp_dir / "same_root_wrong_offset.ll"
        wrong_offset_ll.write_text("""
%Option_M_actual = type { i8 }
%SlabEntry_M_test = type { %Option_M_actual, i32, i64 }
declare void @slab_take_raw_option(ptr)
define void @Slab_M_test_remove(ptr %entry) {
start:
  %wrong.field = getelementptr i8, ptr %entry, i64 8
  call void @slab_take_raw_option(ptr %entry)
  store i8 0, ptr %wrong.field
  ret void
}
""")
        neg_offset_passed = False
        try:
            verify_option_reset_layout_and_protocol(wrong_offset_ll)
        except RuntimeError:
            neg_offset_passed = True
        require(neg_offset_passed, "verify_option_reset_layout_and_protocol must reject non-zero offset zero store target")

    # Part 4: Generic reference pattern IR & referent address verification
    with tempfile.TemporaryDirectory(prefix="toka-slab-refpat-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        ll_file = tmp_dir / "generic_borrow_unique.ll"
        res = run([str(tokac), "--emit-llvm", str(fixture_dir / "generic_borrow_unique.tk"), "-o", str(ll_file)], env=env)
        require(res.returncode == 0, f"failed to emit LLVM IR for generic_borrow_unique.tk:\n{res.stderr}")
        verify_generic_reference_pattern_ir(ll_file)

    # Part 4b: Negative control for generic reference pattern origin
    with tempfile.TemporaryDirectory(prefix="toka-slab-oracle-neg2-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        wrong_ref_ll = tmp_dir / "wrong_ref.ll"
        wrong_ref_ll.write_text("""
define ptr @borrow_value_test(ptr %source) {
entry:
  %value = alloca ptr
  %local = alloca i32
  br label %case_Some
case_Some:
  %unrelated.gep = getelementptr i32, ptr %local, i64 0
  store ptr %unrelated.gep, ptr %value
  br label %arm_body
arm_body:
  %generic.outer_view = load ptr, ptr %value
  ret ptr %generic.outer_view
}
""")
        neg_ref_passed = False
        try:
            verify_generic_reference_pattern_ir(wrong_ref_ll)
        except RuntimeError:
            neg_ref_passed = True
        require(neg_ref_passed, "verify_generic_reference_pattern_ir must reject local alloca origin")

        overwritten_ref_ll = tmp_dir / "overwritten_reference.ll"
        overwritten_ref_ll.write_text("""
define ptr @borrow_value_test(ptr %source) {
entry:
  %value = alloca ptr
  %local = alloca i32
  br label %case_Some
case_Some:
  %source.gep = getelementptr i32, ptr %source, i64 0
  store ptr %source.gep, ptr %value
  br label %arm_body
arm_body:
  store ptr %local, ptr %value
  %generic.outer_view = load ptr, ptr %value
  ret ptr %generic.outer_view
}
""")
        neg_overwritten_passed = False
        try:
            verify_generic_reference_pattern_ir(overwritten_ref_ll)
        except RuntimeError:
            neg_overwritten_passed = True
        require(neg_overwritten_passed, "verify_generic_reference_pattern_ir must reject overwritten reference store")

    # Part 5: Existing repository Slab regression verification
    with tempfile.TemporaryDirectory(prefix="toka-slab-repo-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        check_positive(tokac, ROOT / "tests/pass/g07_slab_test.tk", env, tmp_dir)
        check_positive(tokac, ROOT / "tests/pass/g18_slab_lookup_miss.tk", env, tmp_dir)
        check_negative(tokac, ROOT / "tests/fail/slab_lookup_miss_blocks_remove.tk", env, tmp_dir,
                       "E0441", "slab_lookup_miss_blocks_remove.tk:11")

    print("slab chain: PASS (6 positive fixtures, 5 negative fixtures, strengthened Option reset layout/protocol and generic reference pattern IR assertions with negative controls, 3 repo Slab controls)")


if __name__ == "__main__":
    main()
