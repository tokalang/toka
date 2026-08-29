#!/usr/bin/env python3
"""Qualify default signature-driven direct-call cede behavior."""

import argparse
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/signature_driven_cede_direct"
FLAG = "--experimental-signature-driven-cede"


def run(command, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    runtime = FIXTURES / "runtime.tk"
    default = run([str(tokac), "--check-only", str(runtime)])
    require(default.returncode == 0 and "E04570" not in default.stderr,
            "default signature-driven call did not type-check")

    enabled = run([str(tokac), FLAG, "--check-only", str(runtime)])
    require(enabled.returncode == 0 and "E04570" not in enabled.stderr,
            "deprecated compatibility flag changed default behavior")

    with tempfile.TemporaryDirectory(prefix="toka-cede-direct-") as temp:
        executable = pathlib.Path(temp) / "runtime"
        compiled = run([str(tokac), str(runtime), "-o", str(executable)])
        require(compiled.returncode == 0, "default path failed CodeGen/link")
        executed = run([str(executable)])
        require(executed.returncode == 0,
                f"bounded slice runtime failed: {executed.returncode}")

    moved = run([str(tokac), FLAG, "--check-only",
                 str(FIXTURES / "use_after_implicit.tk")])
    require(moved.returncode != 0 and "E0438" in moved.stderr and
            "E04570" not in moved.stderr,
            "implicit whole-place move did not invalidate its source")

    legacy_exempt_source = FIXTURES / "legacy_exempt_noncopy_moves.tk"
    signature_exempt = run([str(tokac), "--check-only",
                            str(legacy_exempt_source)])
    require(signature_exempt.returncode != 0 and
            "E0438" in signature_exempt.stderr and
            "E04570" not in signature_exempt.stderr,
            "formal Copy proof did not override the legacy no-drop exemption")

    explicit_copy = run([str(tokac), FLAG, "--check-only",
                         str(FIXTURES / "copy_explicit_invalidates.tk")])
    require(explicit_copy.returncode != 0 and "E0438" in explicit_copy.stderr,
            "explicit cede of a proven-Copy place did not invalidate it")

    borrowed = run([str(tokac), FLAG, "--check-only",
                    str(FIXTURES / "borrow_conflict.tk")])
    require(borrowed.returncode != 0 and "E0440" in borrowed.stderr and
            "E04570" not in borrowed.stderr,
            "implicit transfer bypassed an active PAL loan")

    projection = run([str(tokac), FLAG, "--check-only",
                      str(FIXTURES / "projection_out_of_slice.tk")])
    require(projection.returncode == 0 and "E04570" not in projection.stderr,
            "qualified direct-field projection remained out of slice")

    for fixture, diagnostic in (("borrowed_view_out_of_slice.tk", "E04570"),):
        excluded = run([str(tokac), FLAG, "--check-only",
                        str(FIXTURES / fixture)])
        require(excluded.returncode != 0 and diagnostic in excluded.stderr,
                f"out-of-slice route was activated: {fixture}")

    evidence = run([str(tokac), "--cede-obligations=json",
                    "--check-only", str(runtime)])
    require(evidence.returncode != 0 and "E04570" in evidence.stderr,
            "Evidence v1 did not retain its frozen legacy replay profile")

    print("Signature-driven ordinary direct-call cede tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven ordinary direct-call cede tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
