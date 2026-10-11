#!/usr/bin/env python3
"""Installed standard SDK candidate controls for 0.14. No source compiler or private inputs."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description="Toka 0.14 installed candidate verification")
    p.add_argument("--sdk", type=Path, required=True, help="Path to installed SDK root")
    p.add_argument("--revision", required=True, help="Expected candidate git revision SHA")
    p.add_argument("--version", default="v0.14.0", help="Expected version label")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r014_fixtures directory")
    p.add_argument("--output", type=Path, required=True, help="Output directory for receipts")
    a = p.parse_args()

    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sdk = a.sdk.resolve()

    if not re.fullmatch(r"[0-9a-f]{40}", a.revision) or not re.fullmatch(r"v0\.14\.(0|[1-9][0-9]*)", a.version):
        raise ValueError("invalid candidate identity: revision=%s, version=%s" % (a.revision, a.version))

    identity_file = sdk / "sdk.json"
    if not identity_file.is_file():
        raise ValueError("SDK metadata missing: " + str(identity_file))
    identity = json.loads(identity_file.read_text(encoding="utf-8"))

    if (identity.get("schema") != "toka.sdk-identity" or
            type(identity.get("version")) is not int or identity["version"] != 1 or
            identity.get("version_label") != a.version or
            identity.get("candidate_revision") != a.revision or
            identity.get("source_dirty") is not False or
            identity.get("build_testing") is not False):
        raise ValueError("SDK metadata does not bind a standard frozen candidate for " + a.version)

    env = dict(os.environ, PATH=str(sdk / "bin") + ":" + os.environ.get("PATH", "/usr/bin:/bin"), PYTHONDONTWRITEBYTECODE="1")
    env.pop("TOKA_LIB", None)
    env.pop("TOKAC", None)

    receipts = []

    def run(name, argv, cwd, allow_fail=False):
        d = out / ("command-" + name)
        d.mkdir(parents=True, exist_ok=True)
        t = time.monotonic_ns()
        try:
            r = subprocess.run(list(map(str, argv)), cwd=str(cwd), env=env, capture_output=True, timeout=600)
        except (OSError, subprocess.TimeoutExpired) as error:
            record = {
                "name": name,
                "argv": list(map(str, argv)),
                "cwd": str(cwd),
                "exit_code": None,
                "termination": "not_started" if isinstance(error, OSError) else "protection_timeout",
                "error": str(error),
            }
            for key in ("stdout", "stderr"):
                raw = getattr(error, key, None)
                record[key + "_available"] = raw is not None
                if raw is not None:
                    (d / key).write_bytes(raw)
            (d / "receipt.json").write_text(json.dumps(record, indent=2))
            raise

        (d / "stdout").write_bytes(r.stdout)
        (d / "stderr").write_bytes(r.stderr)
        record = {
            "name": name,
            "argv": list(map(str, argv)),
            "cwd": str(cwd),
            "exit_code": r.returncode if r.returncode >= 0 else None,
            "signal": -r.returncode if r.returncode < 0 else None,
            "execution_ms": (time.monotonic_ns() - t) / 1e6,
        }
        (d / "receipt.json").write_text(json.dumps(record, indent=2))
        receipts.append(record)
        if not allow_fail and r.returncode != 0:
            raise RuntimeError(f"{name} failed with exit {r.returncode}; inspect {d}")
        return r

    versions = {}
    tokac_bin = sdk / "bin" / ("tokac.exe" if sys.platform == "win32" else "tokac")
    rt_o = sdk / "lib/sys/toka_rt.o"
    if not rt_o.is_file():
        raise ValueError("SDK runtime object missing: " + str(rt_o))

    for name in ("tokac", "toka", "tokafmt", "tokalsp"):
        binary = sdk / "bin" / (name + (".exe" if sys.platform == "win32" else ""))
        if not binary.is_file():
            raise ValueError("Tool binary missing: " + str(binary))
        if hashlib.sha256(binary.read_bytes()).hexdigest() != identity["tools"][name]["sha256"]:
            raise ValueError("binary identity changed: " + name)
        r = run("version-" + name, [binary, "--version"], out)
        text = (r.stdout + r.stderr).decode("utf-8", errors="replace")
        if not re.search(r"(?<![0-9.])" + re.escape(a.version[1:]) + r"(?![0-9.])", text):
            raise ValueError("version mismatch for " + name + ": " + text)
        versions[name] = text

    # Verify fixtures manifest
    fixtures_dir = a.fixtures.resolve() if a.fixtures else (Path(__file__).resolve().parent / "r014_fixtures")
    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise ValueError("Fixture manifest missing: " + str(manifest_file))
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    for rel_path, exp_hash in manifest.items():
        f = fixtures_dir / rel_path
        if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest() != exp_hash:
            raise ValueError(f"Fixture corrupt or missing: {rel_path}")

    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)

    # ==========================================
    # Group G1: Static Async Trait & Monomorphization
    # ==========================================
    g1_dir = work / "g1"
    g1_dir.mkdir(parents=True, exist_ok=True)

    # G1.1: Lifecycle positive control
    g1_life_tk = fixtures_dir / "g1/g14_static_async_trait_lifecycle.tk"
    g1_life_bin = g1_dir / "g1_lifecycle.bin"
    run("g1-lifecycle-compile", [tokac_bin, g1_life_tk, rt_o, "-o", g1_life_bin], g1_dir)
    run("g1-lifecycle-run", [g1_life_bin], g1_dir)

    # G1.2: Sourceless 3-module replay
    g1_src_dir = fixtures_dir / "g1/sourceless"
    shutil.copy(g1_src_dir / "mod_a.tk", g1_dir / "mod_a.tk")
    shutil.copy(g1_src_dir / "mod_b.tk", g1_dir / "mod_b.tk")
    shutil.copy(g1_src_dir / "main.tk", g1_dir / "main.tk")

    run("g1-sourceless-compile-mod-a", [tokac_bin, "-c", g1_dir / "mod_a.tk", "-o", g1_dir / "mod_a.o"], g1_dir)
    run("g1-sourceless-compile-mod-b", [tokac_bin, "-c", g1_dir / "mod_b.tk", "-o", g1_dir / "mod_b.o"], g1_dir)
    if not (g1_dir / "mod_a.o").is_file() or not (g1_dir / "mod_a.tki").is_file():
        raise RuntimeError("mod_a compilation did not produce .o and .tki")
    if not (g1_dir / "mod_b.o").is_file() or not (g1_dir / "mod_b.tki").is_file():
        raise RuntimeError("mod_b compilation did not produce .o and .tki")

    # Remove mod_a.tk and mod_b.tk to guarantee sourceless consumption
    (g1_dir / "mod_a.tk").unlink()
    (g1_dir / "mod_b.tk").unlink()

    g1_main_bin = g1_dir / "sourceless_main.bin"
    run("g1-sourceless-compile-main", [tokac_bin, g1_dir / "main.tk", g1_dir / "mod_a.o", g1_dir / "mod_b.o", rt_o, "-o", g1_main_bin], g1_dir)
    run("g1-sourceless-run-main", [g1_main_bin], g1_dir)

    # G1.3: Negative rejection controls
    r_dyn = run("g1-reject-dyn-trait", [tokac_bin, "--check-only", fixtures_dir / "g1/fail/dyn_trait_async_param.tk"], g1_dir, allow_fail=True)
    if r_dyn.returncode != 1 or b"E0617" not in r_dyn.stderr:
        raise RuntimeError("G1 dyn trait parameter did not reject with E0617")

    r_plain = run("g1-reject-plain-self", [tokac_bin, "--check-only", fixtures_dir / "g1/fail/trait_async_plain_self.tk"], g1_dir, allow_fail=True)
    if r_plain.returncode != 1 or b"E0618" not in r_plain.stderr:
        raise RuntimeError("G1 trait async plain self did not reject with E0618")

    # ==========================================
    # Group G2: Framed Transport & TaskHandle Lifecycle
    # ==========================================
    g2_dir = work / "g2"
    g2_dir.mkdir(parents=True, exist_ok=True)

    g2_fixtures = sorted((fixtures_dir / "g2").glob("*.tk"))
    if len(g2_fixtures) != 18:
        raise RuntimeError(f"Expected 18 G2 fixtures, found {len(g2_fixtures)}")

    for fix in g2_fixtures:
        case_name = fix.stem
        case_bin = g2_dir / (case_name + ".bin")
        run(f"g2-compile-{case_name}", [tokac_bin, fix, rt_o, "-o", case_bin], g2_dir)
        run(f"g2-run-{case_name}", [case_bin], g2_dir)
        case_bin.unlink(missing_ok=True)

    # ==========================================
    # Group G3-unwrap: Unwrap Consumption & Layout Bounds
    # ==========================================
    g3_dir = work / "g3"
    g3_dir.mkdir(parents=True, exist_ok=True)

    # G3.1: Positive lifecycle and payload controls
    g3_life_tk = fixtures_dir / "g3/pass/g14_unwrap_propagation_lifecycle_test.tk"
    g3_life_bin = g3_dir / "g3_lifecycle.bin"
    run("g3-compile-lifecycle", [tokac_bin, g3_life_tk, rt_o, "-o", g3_life_bin], g3_dir)
    run("g3-run-lifecycle", [g3_life_bin], g3_dir)

    g3_agg_tk = fixtures_dir / "g3/pass/unwrap_aggregate_payload_test.tk"
    g3_agg_bin = g3_dir / "g3_aggregate.bin"
    run("g3-compile-aggregate", [tokac_bin, g3_agg_tk, rt_o, "-o", g3_agg_bin], g3_dir)
    r_agg = run("g3-run-aggregate", [g3_agg_bin], g3_dir)
    if b"PASS" not in r_agg.stdout:
        raise RuntimeError("G3 aggregate payload did not output PASS")

    g3_tag_tk = fixtures_dir / "g3/pass/enum_tag_255_valid_test.tk"
    g3_tag_bin = g3_dir / "g3_tag255.bin"
    run("g3-compile-tag255", [tokac_bin, g3_tag_tk, rt_o, "-o", g3_tag_bin], g3_dir)
    run("g3-run-tag255", [g3_tag_bin], g3_dir)

    # G3.2: 5 Negative layout & cardinality controls across 4 modes
    g3_fail_names = [
        "unwrap_unsupported_prefix_wrong_tag",
        "unwrap_unsupported_prefix_extra_tag",
        "unwrap_unsupported_result_prefix_wrong_tag",
        "unwrap_unsupported_mangled_multi_payload_option",
        "unwrap_unsupported_mangled_multi_payload_result",
    ]

    for name in g3_fail_names:
        fix = fixtures_dir / "g3/fail" / (name + ".tk")
        if not fix.is_file():
            raise RuntimeError(f"Missing negative fixture: {fix}")

        # Mode 1: check-only
        r = run(f"g3-neg-{name}-check", [tokac_bin, "--check-only", fix], g3_dir, allow_fail=True)
        if r.returncode != 1 or b"E04512" not in r.stderr:
            raise RuntimeError(f"G3 negative check {name} did not reject with E04512/exit 1")

        # Mode 2: object
        obj_target = g3_dir / f"{name}.o"
        obj_target.unlink(missing_ok=True)
        r = run(f"g3-neg-{name}-object", [tokac_bin, "-c", fix, "-o", obj_target], g3_dir, allow_fail=True)
        if r.returncode != 1 or b"E04512" not in r.stderr or obj_target.exists():
            raise RuntimeError(f"G3 negative object {name} did not reject with E04512 or produced artifact")

        # Mode 3: ir
        ir_target = g3_dir / f"{name}.ll"
        ir_target.unlink(missing_ok=True)
        r = run(f"g3-neg-{name}-ir", [tokac_bin, "--emit-llvm", fix, "-o", ir_target], g3_dir, allow_fail=True)
        if r.returncode != 1 or b"E04512" not in r.stderr or ir_target.exists():
            raise RuntimeError(f"G3 negative IR {name} did not reject with E04512 or produced artifact")

        # Mode 4: compile
        bin_target = g3_dir / f"{name}.bin"
        bin_target.unlink(missing_ok=True)
        r = run(f"g3-neg-{name}-compile", [tokac_bin, fix, rt_o, "-o", bin_target], g3_dir, allow_fail=True)
        if r.returncode != 1 or b"E04512" not in r.stderr or bin_target.exists():
            raise RuntimeError(f"G3 negative compile {name} did not reject with E04512 or produced artifact")

    # Clean temporary work directory
    shutil.rmtree(work, ignore_errors=True)

    report = {
        "schema": "toka.0.14-candidate-controls",
        "version": 1,
        "result": "pass",
        "candidate_revision": a.revision,
        "version_label": a.version,
        "build_testing": False,
        "four_tool_versions": versions,
        "groups": ["G1", "G2", "G3-unwrap"],
        "SDK_root": str(sdk),
        "receipts": receipts,
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
