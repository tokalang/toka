#!/usr/bin/env python3
"""Unit qualification for native compiler and target cache identity."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("toka_build", ROOT / "tools" / "scripts" / "toka_build.py")
if SPEC is None or SPEC.loader is None:
    raise SystemExit("cannot load toka_build.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


def main() -> int:
    original = os.environ.get("CC")
    try:
        baseline = BUILD.native_toolchain_identity(["host-target"])
        changed_target = BUILD.native_toolchain_identity(["other-target"])
        if baseline == changed_target:
            raise AssertionError("native cache identity ignored the target triple")

        compiler = os.environ.get("CC", "cc")
        os.environ["CC"] = compiler + " --toka-cache-identity-probe"
        changed_compiler = BUILD.native_toolchain_identity(["host-target"])
        if baseline == changed_compiler:
            raise AssertionError("native cache identity ignored CC")

        expected_platforms = {
            "aarch64-apple-darwin": "macos",
            "x86_64-unknown-linux-gnu": "linux",
            "x86_64-pc-windows-msvc": "windows",
        }
        for triple, expected in expected_platforms.items():
            actual = BUILD.native_platform([triple])
            if actual != expected:
                raise AssertionError("native target mapping %s != %s for %s" %
                                     (actual, expected, triple))
        try:
            BUILD.native_platform(["x86_64-unknown-linux-gnu", "aarch64-apple-darwin"])
        except RuntimeError:
            pass
        else:
            raise AssertionError("mixed native target platforms were accepted")
    finally:
        if original is None:
            os.environ.pop("CC", None)
        else:
            os.environ["CC"] = original
    print("PASS: native compiler, target identity, and platform selection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
