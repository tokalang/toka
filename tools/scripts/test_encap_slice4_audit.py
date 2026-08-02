#!/usr/bin/env python3
"""Regression evidence for the gated @Encap Slice 4 Copy/Dup rules."""

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
    command = (str(TOKAC), "--workspace-node",
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
            "impl NonZero@Encap { pub raw }\n"
            "impl NonZero@Copy {}\n"
            "fn main() -> i32 {\n"
            "  auto point = Point(x = 1)\n"
            "  auto point_copy = Point(point)\n"
            "  auto value = NonZero(raw = 2)\n"
            "  auto value_copy = NonZero(value)\n"
            "  auto capture = { [copy value] => value.raw }:fn() -> i32\n"
            "  return point_copy.x + value_copy.raw + capture()\n"
            "}\n", encoding="utf-8")
        compile_source(valid, expect_success=True)

        generic_copy = root / "generic_copy.tk"
        generic_copy.write_text(
            "shape Capsule<T>(value: T)\n"
            "impl<T> Capsule<T>@Encap { pub value }\n"
            "impl<T: @Copy> Capsule<T>@Copy {}\n"
            "fn main() -> i32 {\n"
            "  auto value = Capsule<i32>(value = 1)\n"
            "  auto copied = Capsule<i32>(value)\n"
            "  return copied.value\n"
            "}\n", encoding="utf-8")
        compile_source(generic_copy, expect_success=True)

        nested_generic_copy = root / "nested_generic_copy.tk"
        nested_generic_copy.write_text(
            "shape Inner<T>(value: T)\n"
            "impl<T> Inner<T>@Encap { pub value }\n"
            "impl<T: @Copy> Inner<T>@Copy {}\n"
            "shape Outer<T>(inner: Inner<T>)\n"
            "impl<T> Outer<T>@Encap { pub inner }\n"
            "impl<T: @Copy> Outer<T>@Copy {}\n"
            "fn main() -> i32 {\n"
            "  auto value = Outer<i32>(inner = Inner<i32>(value = 7))\n"
            "  auto copied = Outer<i32>(value)\n"
            "  return copied.inner.value - 7\n"
            "}\n", encoding="utf-8")
        compile_source(nested_generic_copy, expect_success=True)

        reject(root, "generic_copy_unproven_domain.tk",
               "shape Capsule<T>(value: T)\n"
               "impl<T> Capsule<T>@Encap { pub value }\n"
               "impl<T> Capsule<T>@Copy {}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "generic_copy_resource.tk",
               "shape Resource(raw: i32)\n"
               "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
               "shape Capsule<T>(value: T)\n"
               "impl<T> Capsule<T>@Encap { pub value }\n"
               "impl<T: @Copy> Capsule<T>@Copy {}\n"
               "fn main() -> i32 {\n"
               "  auto resource = Resource(raw = 1)\n"
               "  auto value = Capsule<Resource>(value = cede resource)\n"
               "  auto copied = Capsule<Resource>(value)\n"
               "  return copied.value.raw\n"
               "}\n")

        generic_dup = root / "generic_dup.tk"
        generic_dup.write_text(
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "shape Resource(raw: i32)\n"
            "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
            "impl Resource@Dup {\n"
            "  pub fn dup(self) -> Self { return Resource(raw = self.raw + 1) }\n"
            "}\n"
            "shape Wrapper<T>(value: T)\n"
            "impl<T> Wrapper<T>@Encap { pub value }\n"
            "impl<T: @Dup> Wrapper<T>@Dup {\n"
            "  pub fn dup(self) -> Self {\n"
            "    return Wrapper<T>(value = self.value.dup())\n"
            "  }\n"
            "}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 1)\n"
            "  auto wrapper = Wrapper<Resource>(value = cede resource)\n"
            "  auto closure = { [dup wrapper] => wrapper.value.raw }:fn() -> i32\n"
            "  return wrapper.value.raw + closure()\n"
            "}\n", encoding="utf-8")
        compile_source(generic_dup, expect_success=True)
        generic_dup_ir = generic_dup.with_suffix(".ll").read_text(encoding="utf-8")
        wrapper_calls = re.findall(
            r"\bcall\b[^\n]*@Dup_Wrapper_M_Resource_dup\(", generic_dup_ir)
        assert len(wrapper_calls) == 1, generic_dup_ir

        reject(root, "generic_dup_unsatisfied_bound.tk",
               "trait @Dup { pub fn dup(self) -> Self }\n"
               "shape Resource(raw: i32)\n"
               "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
               "shape Wrapper<T>(value: T)\n"
               "impl<T> Wrapper<T>@Encap { pub value }\n"
               "impl<T: @Dup> Wrapper<T>@Dup {\n"
               "  pub fn dup(self) -> Self { return self }\n"
               "}\n"
               "fn main() -> i32 {\n"
               "  auto resource = Resource(raw = 1)\n"
               "  auto wrapper = Wrapper<Resource>(value = cede resource)\n"
               "  auto closure = { [dup wrapper] => wrapper.value.raw }:fn() -> i32\n"
               "  return closure()\n"
               "}\n")

        reject(root, "generic_copy_dup_overlap.tk",
               "trait @Dup { pub fn dup(self) -> Self }\n"
               "shape Capsule<T>(value: T)\n"
               "impl<T> Capsule<T>@Encap { pub value }\n"
               "impl<T: @Copy> Capsule<T>@Copy {}\n"
               "impl<T: @Dup> Capsule<T>@Dup {\n"
               "  pub fn dup(self) -> Self {\n"
               "    return Capsule<T>(value = self.value)\n"
               "  }\n"
               "}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "specialized_generic_copy.tk",
               "shape Pair<T, U>(first: T, second: U)\n"
               "impl<T, U> Pair<T, U>@Encap { pub first, second }\n"
               "impl<T: @Copy, U: @Copy> Pair<T, i32>@Copy {}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "specialized_generic_policy.tk",
               "shape Pair<T, U>(first: T, second: U)\n"
               "impl<T, U> Pair<T, i32>@Encap { pub first, second }\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "generic_dup_duplicate_provider.tk",
               "trait @Dup { pub fn dup(self) -> Self }\n"
               "shape Wrapper<T>(value: T)\n"
               "impl<T> Wrapper<T>@Encap { pub value }\n"
               "impl<T: @Dup> Wrapper<T>@Dup {\n"
               "  pub fn dup(self) -> Self { return self }\n"
               "}\n"
               "impl<T: @Dup> Wrapper<T>@Dup {\n"
               "  pub fn dup(self) -> Self { return self }\n"
               "}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "transparent_generic_dup_overlap.tk",
               "trait @Dup { pub fn dup(self) -> Self }\n"
               "shape Transparent<T>(value: T)\n"
               "impl<T: @Dup> Transparent<T>@Dup {\n"
               "  pub fn dup(self) -> Self { return self }\n"
               "}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "capsule_without_copy.tk",
               "shape Secret(raw: i32)\n"
               "impl Secret@Encap { pub raw }\n"
               "fn main() -> i32 {\n"
               "  auto value = Secret(raw = 1)\n"
               "  auto copied = Secret(value)\n"
               "  return copied.raw\n"
               "}\n")

        closure_copy = root / "closure_copy_capsule.tk"
        closure_copy.write_text(
            "shape Secret(raw: i32)\n"
            "impl Secret@Encap { pub raw }\n"
            "fn main() -> i32 {\n"
            "  auto secret = Secret(raw = 1)\n"
            "  auto closure = { [copy secret] => secret.raw }:fn() -> i32\n"
            "  return closure()\n"
            "}\n", encoding="utf-8")
        rejected_closure_copy = compile_source(closure_copy, expect_success=False)
        assert "E04581" in rejected_closure_copy.stderr, rejected_closure_copy.stderr

        dup_capture = root / "dup_capture.tk"
        dup_capture.write_text(
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "shape Resource(raw: i32)\n"
            "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
            "impl Resource@Dup {\n"
            "  pub fn dup(self) -> Self { return Resource(raw = self.raw + 1) }\n"
            "}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 1)\n"
            "  auto closure = { [dup resource] => resource.raw }:fn() -> i32\n"
            "  return resource.raw + closure()\n"
            "}\n", encoding="utf-8")
        compile_source(dup_capture, expect_success=True)
        dup_ir = dup_capture.with_suffix(".ll").read_text(encoding="utf-8")
        dup_calls = re.findall(r"\bcall\b[^\n]*@Dup_Resource_dup\(", dup_ir)
        assert len(dup_calls) == 1, dup_ir

        reject(root, "dup_capture_without_provider.tk",
               "shape Resource(raw: i32)\n"
               "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
               "fn main() -> i32 {\n"
               "  auto resource = Resource(raw = 1)\n"
               "  auto closure = { [dup resource] => resource.raw }:fn() -> i32\n"
               "  return closure()\n"
               "}\n")

        reject(root, "try_dup_is_not_provider.tk",
               "trait @Dup { pub fn dup(self) -> Self }\n"
               "shape Resource(raw: i32)\n"
               "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
               "impl Resource@Dup {\n"
               "  pub fn try_dup(self) -> Self { return Resource(raw = self.raw) }\n"
               "}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "copy_with_resource.tk",
               "shape Resource(^data: i32)\n"
               "impl Resource@Encap { pub data }\n"
               "impl Resource@Copy {}\n"
               "fn main() -> i32 { return 0 }\n")

        reject(root, "copy_dup_overlap.tk",
               "shape Value(raw: i32)\n"
               "impl Value@Encap { pub raw }\n"
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
                   "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
                   "impl Resource@Dup { %s { return Resource(raw = self.raw) } }\n"
                   "fn main() -> i32 { return 0 }\n" % declaration)

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
