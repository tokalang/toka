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


AUTHORITATIVE_014_FIXTURES = {
    "g1/fail/dyn_trait_async_param.tk": "14ed468697dcc14ac4839d5acf0d588fb113f3d5ae1a457b01fe7aba7d5dfaca",
    "g1/fail/trait_async_plain_self.tk": "3787c6308e638270a618f1880397856370bd842bd102c6dd2daaddc64a7cb4d9",
    "g1/g14_static_async_trait_lifecycle.tk": "2099a9947e09f0ee0a3981b0e6aed3e543d033564c74e587bc24ca706ba93061",
    "g1/sourceless/main.tk": "59ca0992069c05324823c07e5b8baa670ad74257f0bb4effdbf5dc4d4547722c",
    "g1/sourceless/mod_a.tk": "788d9cb2f8cfb6a0b2acdf1cadc296daeeba40f0895c9864f367fc90d7d21ee9",
    "g1/sourceless/mod_b.tk": "dd3c9e1607ad14bb586338712636acabf8c7b0b9894b17a2bd0c4dc44c2ae4c3",
    "g2/closed_empty.tk": "7271c92f2b4fb4328e0da98c5f44c924490cfb5b08dd53c26c8949bd4f0f86a2",
    "g2/framed_reader_read_short_buffer_1.tk": "0537f2196b96a8e7da43f2666e57aa33ca7e491de17d3a8f61d6c3c057cc1f73",
    "g2/framed_reader_read_short_buffer_2.tk": "6056873ab8f4ff038e21127e47af2abd46deefc4e0b709189ac3e7ad460999cc",
    "g2/framed_stream_cancel.tk": "71e1f756894db747851dd13953dccd726e4f0a31285552f11b7c407ba94d0984",
    "g2/framed_stream_ioerror.tk": "eccdf17a91db0eafe28be6ec75ef92af90439e735e840cfc3beb4dbcfad91681",
    "g2/framed_stream_overreported_count.tk": "ae92c54e646311551af29c1f1bee436843a66466107a08b309fda6de305d1930",
    "g2/framed_stream_partial.tk": "73cb1aa9c20e00c572109c1d48fb7683adfc7a3e3d479d23a3a00e7e3a70c0bc",
    "g2/framed_stream_read_short_buffer_1.tk": "b28d726ad8bef9e69bd0a8a959504c8a98f329689f4adc4f03266a3e9f52297b",
    "g2/framed_stream_read_short_buffer_2.tk": "139f944999e7cdac7fa9bdabce319d8029d32aa6f9cfac38e95aa78c49928ce3",
    "g2/framed_stream_zero.tk": "02840a8c384cba12da590993f1f91944128e4aa46ad4f26ca3784b2e0d53e198",
    "g2/framed_writer_cancel.tk": "75c422867acc5c3cc8a53fff3a763c4107e4c1758e911ffcbf3ae45ebd86dfeb",
    "g2/framed_writer_ioerror.tk": "e9e9b16079d6ad54c1ba5172e532e683801aecbfd037787daa8555c00053a11d",
    "g2/framed_writer_overreported_count.tk": "274b375439f39b42e2abd18d832a421669b2dfe2d1db2a1dbf555e63c6eee448",
    "g2/framed_writer_partial.tk": "1f48b40c31d6a9a7f2b30ccd75dabc5140ed2c0dc05af2dbf1236141b2939f48",
    "g2/framed_writer_zero.tk": "0e55418fbabd36d69dc03c120b6cc48b89cb9699f01cfb5dc0f332037f5a5398",
    "g2/given_11_branch.tk": "63cc151d36caf8049e30f8a7f21c5431b802180535f292a2cfbeea27f123d872",
    "g2/lifecycle_pass_suite.tk": "54ad3b5dc53ef3abef791cc850a324e4e77ad8a2b2a2dbd21b2ce55f42c27630",
    "g2/read_cancel.tk": "dea2f69a216d824c1c467bb85cdd0b1293706e0e5135cb05addd94bcb05f4ed7",
    "g3/fail/unwrap_unsupported_mangled_multi_payload_option.tk": "7998fa366bfbf8dd2a93c90e8f9a318c31403afbc4b5bfd51a9c9dd4841b59b3",
    "g3/fail/unwrap_unsupported_mangled_multi_payload_result.tk": "13988a9822bbf1e9d95d6b2859b2fc10bc73f89341563c0b706f7787e28e83f1",
    "g3/fail/unwrap_unsupported_prefix_extra_tag.tk": "8064ad85a699b12a134b021220da611c9b21789c42c5dad14f0108babf95b85d",
    "g3/fail/unwrap_unsupported_prefix_wrong_tag.tk": "0babc344c184b722af10e1d1c979aad94473b703093367b67612a6ab6640e57a",
    "g3/fail/unwrap_unsupported_result_prefix_wrong_tag.tk": "10b547340486a7553772c7c8844aac33634e33e582f931175b58ed441a20b490",
    "g3/pass/enum_tag_255_valid_test.tk": "19c979ad14fbe59fd3616d35051d162d98bb3dff0b77a7f42312b8bd063a2ab5",
    "g3/pass/g14_unwrap_propagation_lifecycle_test.tk": "2a0393f8cc57c85b21a731ca34eae3ec2b9d7a73c473c911e17988538c53fd5a",
    "g3/pass/unwrap_aggregate_payload_test.tk": "fa28999a9268d00f48d4d1deed9297515d58415092bbd21da6aff598fcd82319",
}
EXPECTED_014_FIXTURES_DIGEST = "e5679cd455cb44e517d05b9c28fafeb403442867faa701841dc43bd6639ddffa"


def main():
    p = argparse.ArgumentParser(description="Toka 0.14 installed candidate verification")
    p.add_argument("--sdk", type=Path, required=True, help="Path to installed SDK root")
    p.add_argument("--revision", required=True, help="Expected candidate git revision SHA")
    p.add_argument("--version", default="v0.14.0", help="Expected version label")
    p.add_argument("--target", default=None, help="Platform target name")
    p.add_argument("--fixtures", type=Path, default=None, help="Path to r014_fixtures directory")
    p.add_argument("--output", type=Path, required=True, help="Output directory for receipts")
    a = p.parse_args()

    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sdk = a.sdk.resolve()

    if not re.fullmatch(r"[0-9a-f]{40}", a.revision) or not re.fullmatch(r"v0\.14\.(0|[1-9][0-9]*)", a.version):
        raise ValueError("invalid candidate identity: revision=%s, version=%s" % (a.revision, a.version))

    target = a.target
    if not target:
        m = re.match(r"toka-v[0-9.]+-([a-z0-9_-]+)", sdk.name)
        target = m.group(1) if m else "unknown"

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

    # 1. Control script self-integrity and fixture manifest verification (PREFLIGHT BEFORE EXECUTION)
    script_bytes = Path(__file__).resolve().read_bytes()
    control_script_sha256 = hashlib.sha256(script_bytes).hexdigest()
    bundle_manifest_file = Path(__file__).resolve().parent / "replay_bundle_manifest.json"
    if bundle_manifest_file.is_file():
        b_manifest = json.loads(bundle_manifest_file.read_text(encoding="utf-8"))
        expected_self_sha = b_manifest.get("modules", {}).get("test_0_14_candidate.py")
        if expected_self_sha and control_script_sha256 != expected_self_sha:
            raise ValueError(f"Control script integrity mismatch: expected {expected_self_sha}, got {control_script_sha256}")

    fixtures_dir = a.fixtures.resolve() if a.fixtures else (Path(__file__).resolve().parent / "r014_fixtures")
    manifest_file = fixtures_dir / "manifest.json"
    if not manifest_file.is_file():
        raise ValueError("Fixture manifest missing: " + str(manifest_file))
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))

    if not isinstance(manifest, dict) or not manifest:
        raise ValueError("Fixture manifest must be a non-empty dict")
    if manifest != AUTHORITATIVE_014_FIXTURES:
        raise ValueError("Fixture manifest does not match authoritative fixtures mapping")

    hasher = hashlib.sha256()
    for rel_path in sorted(manifest.keys()):
        expected_hash = manifest[rel_path]
        fixture_file = fixtures_dir / rel_path
        if not fixture_file.is_file():
            raise ValueError("Fixture file missing: " + str(fixture_file))
        actual_hash = hashlib.sha256(fixture_file.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise ValueError(f"Fixture digest mismatch for {rel_path}: expected {expected_hash}, got {actual_hash}")
        hasher.update(f"{rel_path}:{actual_hash}\n".encode("utf-8"))
    fixtures_digest = hasher.hexdigest()
    if fixtures_digest != EXPECTED_014_FIXTURES_DIGEST:
        raise ValueError(f"Fixtures digest mismatch: expected {EXPECTED_014_FIXTURES_DIGEST}, got {fixtures_digest}")

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

    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)

    # ==========================================
    # Group G1: Static Async Trait & Monomorphization (8 receipts)
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
    # Group G2: Framed Transport & TaskHandle Lifecycle (36 receipts: 18 compile + 18 run)
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
    # Group G3-unwrap: Unwrap Consumption & Layout Bounds (26 receipts: 6 positive + 20 negative)
    # ==========================================
    g3_dir = work / "g3"
    g3_dir.mkdir(parents=True, exist_ok=True)

    # G3 Positive Controls (3 cases x 2 = 6 receipts)
    g3_pos_cases = [
        ("lifecycle", fixtures_dir / "g3/pass/g14_unwrap_propagation_lifecycle_test.tk"),
        ("aggregate", fixtures_dir / "g3/pass/unwrap_aggregate_payload_test.tk"),
        ("tag255", fixtures_dir / "g3/pass/enum_tag_255_valid_test.tk"),
    ]
    for tag, src in g3_pos_cases:
        bin_path = g3_dir / f"g3_{tag}.bin"
        run(f"g3-compile-{tag}", [tokac_bin, src, rt_o, "-o", bin_path], g3_dir)
        run(f"g3-run-{tag}", [bin_path], g3_dir)
        bin_path.unlink(missing_ok=True)

    # G3 Negative Controls (5 cases x 4 modes = 20 receipts)
    g3_neg_cases = [
        ("unwrap_unsupported_prefix_wrong_tag", fixtures_dir / "g3/fail/unwrap_unsupported_prefix_wrong_tag.tk"),
        ("unwrap_unsupported_prefix_extra_tag", fixtures_dir / "g3/fail/unwrap_unsupported_prefix_extra_tag.tk"),
        ("unwrap_unsupported_result_prefix_wrong_tag", fixtures_dir / "g3/fail/unwrap_unsupported_result_prefix_wrong_tag.tk"),
        ("unwrap_unsupported_mangled_multi_payload_option", fixtures_dir / "g3/fail/unwrap_unsupported_mangled_multi_payload_option.tk"),
        ("unwrap_unsupported_mangled_multi_payload_result", fixtures_dir / "g3/fail/unwrap_unsupported_mangled_multi_payload_result.tk"),
    ]

    for name, fix in g3_neg_cases:
        # Mode 1: check
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
        "target": target,
        "build_testing": False,
        "fixtures_digest": fixtures_digest,
        "control_script_name": "test_0_14_candidate.py",
        "control_script_sha256": control_script_sha256,
        "counts": {
            "total_receipts": len(receipts),
            "passed": len(receipts),
            "failed": 0,
            "groups": {
                "version": 4,
                "G1": 8,
                "G2": 36,
                "G3-unwrap": 26,
            },
        },
        "four_tool_versions": versions,
        "groups": ["G1", "G2", "G3-unwrap"],
        "SDK_root": str(sdk),
        "receipts": receipts,
    }
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
