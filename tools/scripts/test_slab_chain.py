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
        print(f"[FAIL] {message}", file=sys.stderr)
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


def verify_option_reset_layout_and_protocol(ll_path):
    require(ll_path.exists(), f"missing LLVM IR file: {ll_path}")
    content = ll_path.read_text()

    # 1. Verify SlabEntry has Option as its first struct field (offset 0)
    slab_entry_matches = re.findall(r'%(SlabEntry_M_[^\s=]+)\s*=\s*type\s*\{\s*(%Option_M_[^\s,}]+)', content)
    require(len(slab_entry_matches) > 0, "SlabEntry struct type with Option field 0 not found in LLVM IR")

    # 2. Verify Option has i8 discriminant tag as its first field (offset 0)
    opt_matches = re.findall(r'%(Option_M_[^\s=]+)\s*=\s*type\s*\{\s*i8', content)
    require(len(opt_matches) > 0, "Option struct type with i8 tag at offset 0 not found in LLVM IR")

    # 3. Verify in Slab::remove:
    #    a) slab_take_raw_option is called
    #    b) tag zeroing (store i8 0) occurs after take
    #    c) no intervening user calls / drops between take and zeroing
    match = re.search(r'define [^\n]+@Slab_M_[^\n]+_remove\([^\n]+\)\s*\{', content)
    require(match, "Slab remove function not found in LLVM IR")
    func_start = match.start()
    func_end = content.find('\n}\n', func_start)
    body = content[func_start:func_end]

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


def verify_generic_reference_pattern_ir(ll_path):
    require(ll_path.exists(), f"missing LLVM IR file: {ll_path}")
    content = ll_path.read_text()

    # Find borrow_value function
    hex_name = "borrow_value".encode().hex()
    m = re.search(r'define [^\n]+' + hex_name + r'[^\n]+\([^\n]+\)\s*\{', content)
    if not m:
        m = re.search(r'define [^\n]+borrow_value[^\n]+\([^\n]+\)\s*\{', content)
    require(m, "borrow_value function not found in LLVM IR")
    func_start = m.start()
    func_end = content.find('\n}\n', func_start)
    body = content[func_start:func_end]

    # Verify generic view loading referent directly from source GEP, not stack escape
    require("%generic.outer_view = load ptr, ptr %value" in body,
            "borrow_value must load generic view referent from %value")
    require("ret ptr %generic.outer_view" in body,
            "borrow_value must return %generic.outer_view referent pointer")
    require("\ncase_Some:" in body, "borrow_value must have case_Some pattern branch")

    # In case_Some, verify %value stores the payload address projected from %source
    some_idx = body.rfind("case_Some:")
    require(some_idx != -1, "case_Some must exist")
    next_bb = body.find("br label %arm_body", some_idx)
    require(next_bb != -1, "case_Some must branch to arm_body")
    some_block = body[some_idx:next_bb]
    require("getelementptr" in some_block, "case_Some must project payload address via GEP")
    require("store ptr" in some_block and ", ptr %value" in some_block,
            "case_Some must store projected payload address into %value")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()

    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    fixture_dir = ROOT / "tests/semantics/slab"
    require(fixture_dir.exists(), f"missing checked-in fixture directory: {fixture_dir}")

    # Part 1: Positive test fixtures (lifecycle, ended_loan_positive, generic_borrow_soul, generic_borrow_unique)
    target_positives = [
        "lifecycle.tk",
        "ended_loan_positive.tk",
        "generic_borrow_soul.tk",
        "generic_borrow_unique.tk",
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

    # Part 4: Generic reference pattern IR & referent address verification
    with tempfile.TemporaryDirectory(prefix="toka-slab-refpat-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        ll_file = tmp_dir / "generic_borrow_unique.ll"
        res = run([str(tokac), "--emit-llvm", str(fixture_dir / "generic_borrow_unique.tk"), "-o", str(ll_file)], env=env)
        require(res.returncode == 0, f"failed to emit LLVM IR for generic_borrow_unique.tk:\n{res.stderr}")
        verify_generic_reference_pattern_ir(ll_file)

    # Part 5: Existing repository Slab regression verification
    with tempfile.TemporaryDirectory(prefix="toka-slab-repo-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        check_positive(tokac, ROOT / "tests/pass/g07_slab_test.tk", env, tmp_dir)
        check_positive(tokac, ROOT / "tests/pass/g18_slab_lookup_miss.tk", env, tmp_dir)
        check_negative(tokac, ROOT / "tests/fail/slab_lookup_miss_blocks_remove.tk", env, tmp_dir,
                       "E0441", "slab_lookup_miss_blocks_remove.tk:11")

    print("slab chain: PASS (4 positive fixtures, 4 negative fixtures, Option reset layout/protocol IR assertions, generic reference pattern IR assertions, 3 repo Slab controls)")


if __name__ == "__main__":
    main()
