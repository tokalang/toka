#!/usr/bin/env python3
"""Qualify default signature-driven direct-call cede behavior."""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/signature_driven_cede_direct"
FLAG = "--experimental-signature-driven-cede"
os.environ["TOKA_STAGE1_LEGACY_REPLAY"] = "1"


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

    generic_vec = FIXTURES / "generic_vec_runtime.tk"
    generic_vec_check = run([str(tokac), "--check-only", str(generic_vec)])
    require(generic_vec_check.returncode == 0,
            "generic Vec implicit cede did not type-check")
    with tempfile.TemporaryDirectory(prefix="toka-cede-generic-vec-") as temp:
        executable = pathlib.Path(temp) / "generic-vec"
        compiled = run([str(tokac), str(generic_vec), "-o", str(executable)])
        require(compiled.returncode == 0 and "E0761" not in compiled.stderr,
                "generic Vec implicit cede failed CodeGen/link")
        require(run([str(executable)]).returncode == 0,
                "generic Vec implicit/explicit cede runtime failed")

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

    shared = FIXTURES / "shared_runtime.tk"
    with tempfile.TemporaryDirectory(prefix="toka-cede-shared-") as temp:
        executable = pathlib.Path(temp) / "shared"
        compiled = run([str(tokac), str(shared), "-o", str(executable)])
        require(compiled.returncode == 0,
                "bare shared transfer failed CodeGen/link")
        require(run([str(executable)]).returncode == 0,
                "bare shared transfer failed at runtime")
    shared_moved = run([str(tokac), "--check-only",
                        str(FIXTURES / "shared_use_after_implicit.tk")])
    require(shared_moved.returncode != 0 and
            "E0438" in shared_moved.stderr,
            "bare shared transfer did not invalidate its source")

    projection = run([str(tokac), FLAG, "--check-only",
                      str(FIXTURES / "projection_out_of_slice.tk")])
    require(projection.returncode == 0 and "E04570" not in projection.stderr,
            "qualified direct-field projection remained out of slice")

    borrowed_view = FIXTURES / "borrowed_view_out_of_slice.tk"
    borrowed_check = run([str(tokac), "--check-only", str(borrowed_view)])
    require(borrowed_check.returncode == 0,
            "borrowed CopyIdentity call remained fail-closed")
    with tempfile.TemporaryDirectory(prefix="toka-cede-view-") as temp:
        executable = pathlib.Path(temp) / "borrowed-view"
        compiled = run([str(tokac), str(borrowed_view), "-o", str(executable)])
        require(compiled.returncode == 0,
                "borrowed CopyIdentity call failed CodeGen/link")
        require(run([str(executable)]).returncode == 0,
                "borrowed CopyIdentity call failed at runtime")

    evidence = run([str(tokac), "--cede-obligations=json",
                    "--check-only", str(runtime)])
    require(evidence.returncode != 0 and "E04570" in evidence.stderr,
            "Evidence v1 did not retain its frozen legacy replay profile")

    with tempfile.TemporaryDirectory(prefix="toka-cede-codegen-fault-") as temp:
        executable = pathlib.Path(temp) / "must-not-exist"
        injected = run([
            str(tokac), "--m1b-inject-missing-call-transfer-elaboration",
            str(FIXTURES / "codegen_missing_call_transfer.tk"),
            "-o", str(executable),
        ])
        require(injected.returncode != 0 and "E0761" in injected.stderr,
                "CodeGen did not independently reject missing elaboration")
        require(not executable.exists(),
                "CodeGen fault emitted an executable despite E0761")

    print("Signature-driven ordinary direct-call cede tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven ordinary direct-call cede tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
