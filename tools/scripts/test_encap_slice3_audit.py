#!/usr/bin/env python3
"""Regression evidence for the gated @encap Slice 3 lifecycle lowering."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def compile_source(source: Path, *, expect_success: bool) -> subprocess.CompletedProcess[str]:
    output = source.with_suffix(".ll")
    command = (str(TOKAC), "--encap-epoch=v3", "--workspace-node",
               "slice3-workspace-v1", "--workspace-root", str(source.parent),
               "-c", "--emit-llvm", "-o", str(output), str(source))
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (completed.returncode == 0) != expect_success:
        outcome = "unexpectedly succeeded" if completed.returncode == 0 else "failed"
        raise RuntimeError("command %s:\n$ %s\n%s" %
                           (outcome, " ".join(command), completed.stderr))
    return completed


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "lifecycle.tk"
        source.write_text(
            "shape Inner(^data: i32)\n"
            "impl Inner@encap { pub data }\n"
            "shape Capsule(inner: Inner)\n"
            "impl Capsule@encap { pub inner fn drop(self#) {} }\n"
            "fn main() -> i32 {\n"
            "  auto value = Capsule(inner = Inner(^data = new i32(7)))\n"
            "  return 0\n"
            "}\n", encoding="utf-8")
        compile_source(source, expect_success=True)
        ir = source.with_suffix(".ll").read_text(encoding="utf-8")
        assert "encap_Capsule_drop" in ir
        assert "encap_Inner_drop" not in ir

        forbidden_operations = root / "forbidden_operations.tk"
        forbidden_operations.write_text(
            "shape Inner(^data: i32)\n"
            "impl Inner@encap { pub data }\n"
            "shape Capsule(inner: Inner)\n"
            "impl Capsule@encap { pub inner fn drop(self#) {} }\n"
            "fn main() -> i32 {\n"
            "  auto value = Capsule(inner = Inner(^data = new i32(7)))\n"
            "  auto moved = cede value.inner\n"
            "  return 0\n"
            "}\n", encoding="utf-8")
        partial_move = compile_source(forbidden_operations, expect_success=False)
        assert "E0439" in partial_move.stderr

        direct_hook = root / "direct_hook.tk"
        direct_hook.write_text(
            "shape Capsule(value: i32)\n"
            "impl Capsule@encap { pub value fn drop(self#) {} }\n"
            "fn main() -> i32 { auto value = Capsule(value = 1); value.drop(); return 0 }\n",
            encoding="utf-8")
        hook_call = compile_source(direct_hook, expect_success=False)
        assert hook_call.returncode != 0

        invalid_hook = root / "invalid_hook.tk"
        invalid_hook.write_text(
            "shape Invalid(value: i32)\n"
            "impl Invalid@encap { pub value pub fn drop(self#) {} }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        rejected = compile_source(invalid_hook, expect_success=False)
        assert "E0406" in rejected.stderr

    print("encap Slice 3 lifecycle audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print("encap Slice 3 lifecycle audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
