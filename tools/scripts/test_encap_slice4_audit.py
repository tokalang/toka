#!/usr/bin/env python3
"""Regression evidence for the gated @encap Slice 4 Copy/Dup rules."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def compile_source(source: Path, *, expect_success: bool) -> subprocess.CompletedProcess[str]:
    output = source.with_suffix(".ll")
    command = (str(TOKAC), "--encap-epoch=v4", "--workspace-node",
               "slice4-workspace-v1", "--workspace-root", str(source.parent),
               "-c", "--emit-llvm", "-o", str(output), str(source))
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (completed.returncode == 0) != expect_success:
        outcome = "unexpectedly succeeded" if completed.returncode == 0 else "failed"
        raise RuntimeError("command %s:\n$ %s\n%s" %
                           (outcome, " ".join(command), completed.stderr))
    return completed


def reject(root: Path, name: str, source: str) -> None:
    path = root / name
    path.write_text(source, encoding="utf-8")
    result = compile_source(path, expect_success=False)
    assert "E0406" in result.stderr, result.stderr


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        valid = root / "valid.tk"
        valid.write_text(
            "shape Point(x: i32)\n"
            "shape NonZero(raw: i32)\n"
            "impl NonZero@encap { pub raw }\n"
            "impl NonZero@Copy {}\n"
            "fn main() -> i32 {\n"
            "  auto point = Point(x = 1)\n"
            "  auto point_copy = Point(point)\n"
            "  auto value = NonZero(raw = 2)\n"
            "  auto value_copy = NonZero(value)\n"
            "  auto capture: fn() -> i32 = { [copy value] => value.raw }\n"
            "  return point_copy.x + value_copy.raw + capture()\n"
            "}\n", encoding="utf-8")
        compile_source(valid, expect_success=True)

        reject(root, "capsule_without_copy.tk",
               "shape Secret(raw: i32)\n"
               "impl Secret@encap { pub raw }\n"
               "fn main() -> i32 {\n"
               "  auto value = Secret(raw = 1)\n"
               "  auto copied = Secret(value)\n"
               "  return copied.raw\n"
               "}\n")

        closure_copy = root / "closure_copy_capsule.tk"
        closure_copy.write_text(
            "shape Secret(raw: i32)\n"
            "impl Secret@encap { pub raw }\n"
            "fn main() -> i32 {\n"
            "  auto secret = Secret(raw = 1)\n"
            "  auto closure: fn() -> i32 = { [copy secret] => secret.raw }\n"
            "  return closure()\n"
            "}\n", encoding="utf-8")
        rejected_closure_copy = compile_source(closure_copy, expect_success=False)
        assert "E04581" in rejected_closure_copy.stderr, rejected_closure_copy.stderr

        dup_capture = root / "dup_capture.tk"
        dup_capture.write_text(
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "shape Resource(raw: i32)\n"
            "impl Resource@encap { pub raw fn drop(self#) {} }\n"
            "impl Resource@Dup {\n"
            "  pub fn dup(self) -> Self { return Resource(raw = self.raw + 1) }\n"
            "}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 1)\n"
            "  auto closure: fn() -> i32 = { [dup resource] => resource.raw }\n"
            "  return closure()\n"
            "}\n", encoding="utf-8")
        compile_source(dup_capture, expect_success=True)
        dup_ir = dup_capture.with_suffix(".ll").read_text(encoding="utf-8")
        dup_calls = re.findall(r"\bcall\b[^\n]*@Dup_Resource_dup\(", dup_ir)
        assert len(dup_calls) == 1, dup_ir

        reject(root, "dup_capture_without_provider.tk",
               "shape Resource(raw: i32)\n"
               "impl Resource@encap { pub raw fn drop(self#) {} }\n"
               "fn main() -> i32 {\n"
               "  auto resource = Resource(raw = 1)\n"
               "  auto closure: fn() -> i32 = { [dup resource] => resource.raw }\n"
               "  return closure()\n"
               "}\n")

        reject(root, "copy_with_resource.tk",
               "shape Resource(^data: i32)\n"
               "impl Resource@encap { pub data }\n"
               "impl Resource@Copy {}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "copy_dup_overlap.tk",
               "shape Value(raw: i32)\n"
               "impl Value@encap { pub raw }\n"
               "impl Value@Copy {}\n"
               "impl Value@Dup { pub fn dup(self) -> Self { return Value(raw = self.raw) } }\n"
               "fn main() -> i32 { return 0 }\n")

        for name, declaration in (
                ("dup_mutable_receiver.tk", "pub fn dup(self#) -> Self"),
                ("dup_consuming_receiver.tk", "pub fn dup(cede self) -> Self"),
                ("dup_morphic_return.tk", "pub fn dup(self) -> Self#"),
                ("dup_return_dependency.tk", "pub fn dup(self) -> Self <- self")):
            reject(root, name,
                   "shape Resource(raw: i32)\n"
                   "trait @Dup { pub fn dup(self) -> Self }\n"
                   "impl Resource@encap { pub raw fn drop(self#) {} }\n"
                   "impl Resource@Dup { %s { return Resource(raw = self.raw) } }\n"
                   "fn main() -> i32 { return 0 }\n" % declaration)

        reject(root, "deleted_function.tk",
               "fn obsolete() = delete\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "removed_facet.tk",
               "trait @Clone {}\n"
               "shape Value(raw: i32)\n"
               "impl Value@Clone {}\n"
               "fn main() -> i32 { return 0 }\n")

        ordinary_clone = root / "ordinary_clone.tk"
        ordinary_clone.write_text(
            "shape Value(raw: i32)\n"
            "impl Value { pub fn clone(self) -> Self { return Value(raw = self.raw) } }\n"
            "fn main() -> i32 { auto value = Value(raw = 1); auto cloned = value.clone(); return cloned.raw }\n",
            encoding="utf-8")
        compile_source(ordinary_clone, expect_success=True)

    print("encap Slice 4 Copy/Dup audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print("encap Slice 4 Copy/Dup audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
