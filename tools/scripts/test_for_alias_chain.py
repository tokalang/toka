#!/usr/bin/env python3
"""Comprehensive gate test runner for for-alias legal entry, storage slot invariants, and array borrow dependencies."""

import argparse
import os
import pathlib
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()

    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    # Part 1: Verify the 4 target PASS files compile, link, and execute to 0, plus .o/.ll and shadow
    target_pass_files = [
        "tests/pass/g08_for_alias_handle_array_permissions.tk",
        "tests/pass/g08_for_alias_shared_array_interior_mutability.tk",
        "tests/pass/g08_for_alias_shared_array_permissions.tk",
        "tests/pass/g08_for_alias_writable_vec_handles.tk",
    ]

    with tempfile.TemporaryDirectory(prefix="toka-for-alias-pass-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        for rel_path in target_pass_files:
            source = ROOT / rel_path
            check_positive(tokac, source, env, tmp_dir)

    # Part 2: Negative field ceiling test (for_alias_shape_field_ceiling.tk)
    with tempfile.TemporaryDirectory(prefix="toka-ceiling-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        ceiling_source = ROOT / "tests/fail/for_alias_shape_field_ceiling.tk"
        require(ceiling_source.exists(), f"missing ceiling test source: {ceiling_source}")
        check_negative(tokac, ceiling_source, env, tmp_dir, "E0443", "for_alias_shape_field_ceiling.tk:8")
        # Also check E04573
        res = run([str(tokac), "--check-only", str(ceiling_source)], env=env)
        require("E04573" in res.stderr, f"missing expected E04573 in {ceiling_source}")

    # Part 3: Checked-in audit test fixtures in tests/semantics/for_alias/
    with tempfile.TemporaryDirectory(prefix="toka-audit-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        fixture_dir = ROOT / "tests/semantics/for_alias"
        require(fixture_dir.exists(), f"missing checked-in fixture directory: {fixture_dir}")

        # Probe 1: shared_slot_move_upgrade.tk rejected with E04661
        check_negative(tokac, fixture_dir / "shared_slot_move_upgrade.tk", env, tmp_dir,
                       "E04661", "shared_slot_move_upgrade.tk:10")

        # Probe 2: repeated_ref_escape.tk rejected with E0455
        check_negative(tokac, fixture_dir / "repeated_ref_escape.tk", env, tmp_dir,
                       "E0455", "repeated_ref_escape.tk:3")

        # Probe 3: local_ref_array.tk compiles, links, runs cleanly to exit 0
        check_positive(tokac, fixture_dir / "local_ref_array.tk", env, tmp_dir)

        # Probe 4: shadowed_return.tk rejected with E0455
        check_negative(tokac, fixture_dir / "shadowed_return.tk", env, tmp_dir,
                       "E0455", "shadowed_return.tk:4")

        # Probe 5: renamed_return.tk rejected with E0455
        check_negative(tokac, fixture_dir / "renamed_return.tk", env, tmp_dir,
                       "E0455", "renamed_return.tk:4")

        # Probe 6: unique_cleanup_trace.tk compiles and runs with exit 0, printing 1, 1, 2
        check_positive(tokac, fixture_dir / "unique_cleanup_trace.tk", env, tmp_dir,
                       expected_output="after_rebind=1\nafter_alias=1\nafter_container=2")

        # Probe 7: rebind_cleanup.tk compiles and runs with exit 0 (all assertions pass)
        check_positive(tokac, fixture_dir / "rebind_cleanup.tk", env, tmp_dir)

        # Probe 8: alias_reference_escape.tk rejected with E0456
        check_negative(tokac, fixture_dir / "alias_reference_escape.tk", env, tmp_dir,
                       "E0456", "alias_reference_escape.tk:5")

        # Probe 9: rebind_call_escape.tk rejected with E0441 / PAL borrow conflict
        check_negative(tokac, fixture_dir / "rebind_call_escape.tk", env, tmp_dir,
                       "E0441", "rebind_call_escape.tk:13")

        # Probe 10: saved_parameter_shadow.tk compiles and runs cleanly to exit 0 (review 3)
        check_positive(tokac, fixture_dir / "saved_parameter_shadow.tk", env, tmp_dir)

        # Probe 11: saved_parameter_control.tk compiles and runs cleanly to exit 0 (review 3)
        check_positive(tokac, fixture_dir / "saved_parameter_control.tk", env, tmp_dir)

    # Part 4: Probes and Return Escapes
    with tempfile.TemporaryDirectory(prefix="toka-for-alias-probes-") as tmp:
        tmp_dir = pathlib.Path(tmp)

        # Probe A: H request on immutable array (arr without #) rejected with E04645
        probe_a = tmp_dir / "probe_a.tk"
        probe_a.write_text(
            "shape Cell(value#: i32)\n"
            "fn main() -> i32 {\n"
            "    auto ^first# = new Cell(value = 1)\n"
            "    auto arr = [cede ^first]\n"
            "    for alias ^#x in arr {\n"
            "        auto ^replacement# = new Cell(value = 2)\n"
            "        ^x = ^replacement\n"
            "    }\n"
            "    return 0\n"
            "}\n"
        )
        check_negative(tokac, probe_a, env, tmp_dir, "E04645", "probe_a.tk:5")

        # Probe B: Reuse of source after rebinding into array rejected with E0438
        probe_b = tmp_dir / "probe_b.tk"
        probe_b.write_text(
            "shape Cell(value#: i32)\n"
            "fn main() -> i32 {\n"
            "    auto ^first# = new Cell(value = 1)\n"
            "    auto arr# = [cede ^first]\n"
            "    for alias ^#x in arr {\n"
            "        auto ^replacement# = new Cell(value = 2)\n"
            "        ^x = ^replacement\n"
            "        auto ^reuse = ^replacement\n"
            "    }\n"
            "    return 0\n"
            "}\n"
        )
        check_negative(tokac, probe_b, env, tmp_dir, "E0438", "probe_b.tk:8")

        # Probe C: Ceding a place alias rejected with E04646
        probe_c = tmp_dir / "probe_c.tk"
        probe_c.write_text(
            "shape Cell(value#: i32)\n"
            "fn main() -> i32 {\n"
            "    auto ^first# = new Cell(value = 1)\n"
            "    auto arr# = [cede ^first]\n"
            "    for alias ^x in arr {\n"
            "        auto ^y = cede ^x\n"
            "    }\n"
            "    return 0\n"
            "}\n"
        )
        check_negative(tokac, probe_c, env, tmp_dir, "E04646", "probe_c.tk:6")

        # Mixed return escape 1: [&param, &local] rejected with E0455
        mix1 = tmp_dir / "mix1.tk"
        mix1.write_text(
            "fn escape(param: i32) -> [&i32; 2] <- param {\n"
            "    auto local = 7:i32\n"
            "    return [&param, &local]\n"
            "}\n"
            "fn main() -> i32 { return 0 }\n"
        )
        check_negative(tokac, mix1, env, tmp_dir, "E0455", "mix1.tk:3")

        # Mixed return escape 2: [&local, &param] rejected with E0455
        mix2 = tmp_dir / "mix2.tk"
        mix2.write_text(
            "fn escape(param: i32) -> [&i32; 2] <- param {\n"
            "    auto local = 7:i32\n"
            "    return [&local, &param]\n"
            "}\n"
            "fn main() -> i32 { return 0 }\n"
        )
        check_negative(tokac, mix2, env, tmp_dir, "E0455", "mix2.tk:3")

        # Legal parameter returns: [&param, &param], [&param; 2], [&a, &b] compile and run to 0
        legal = tmp_dir / "legal.tk"
        legal.write_text(
            "fn ret_pair(param: i32) -> [&i32; 2] <- param {\n"
            "    return [&param, &param]\n"
            "}\n"
            "fn ret_repeat(param: i32) -> [&i32; 2] <- param {\n"
            "    return [&param; 2]\n"
            "}\n"
            "fn ret_multi(a: i32, b: i32) -> [&i32; 2] <- a | b {\n"
            "    return [&a, &b]\n"
            "}\n"
            "fn main() -> i32 {\n"
            "    auto x = 10:i32\n"
            "    auto y = 20:i32\n"
            "    auto p = ret_pair(x)\n"
            "    for alias &item in p { assert(item == 10, \"pair\") }\n"
            "    auto r = ret_repeat(x)\n"
            "    for alias &item in r { assert(item == 10, \"repeat\") }\n"
            "    auto m = ret_multi(x, y)\n"
            "    for alias &item in m { assert(item == 10 || item == 20, \"multi\") }\n"
            "    return 0\n"
            "}\n"
        )
        check_positive(tokac, legal, env, tmp_dir)

    print("for alias chain: PASS (4 targets + .o/.ll, field ceiling E0443, checked-in audit probes 1-11, shadow parity, probes A/B/C, mixed escapes, legal multi-source returns)")


if __name__ == "__main__":
    main()
