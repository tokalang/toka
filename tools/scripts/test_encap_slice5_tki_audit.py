#!/usr/bin/env python3
"""Regression evidence for the gated @Encap Slice 5 TKI v2 contract."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def compile_source(source: Path, root: Path, *, expect_success: bool,
                   emit_llvm: bool = False) -> subprocess.CompletedProcess[str]:
    output = source.with_suffix(".ll" if emit_llvm else ".o")
    command = (str(TOKAC), "--workspace-node",
               "slice5-workspace-v1", "--workspace-root", str(root),
               "-c", *(("--emit-llvm",) if emit_llvm else ()),
               "-o", str(output), str(source))
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
        provider = root / "lib.tk"
        consumer = root / "main.tk"
        provider.write_text(
            "pub shape NonZero(raw: i32)\n"
            "impl NonZero@Encap { pub raw }\n"
            "impl NonZero@Copy {}\n"
            "pub shape Capsule<T>(value: T)\n"
            "impl<T> Capsule<T>@Encap { pub value }\n"
            "impl<T: @Copy> Capsule<T>@Copy {}\n"
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "pub shape Resource(raw: i32)\n"
            "impl Resource@Encap { pub raw fn drop(self#) {} }\n"
            "impl Resource@Dup {\n"
            "  pub fn dup(self) -> Self { return Resource(raw = self.raw + 1) }\n"
            "}\n"
            "pub shape Wrapper<T>(value: T)\n"
            "impl<T> Wrapper<T>@Encap { pub value }\n"
            "impl<T: @Dup> Wrapper<T>@Dup {\n"
            "  pub fn dup(self) -> Self {\n"
            "    return Wrapper<T>(value = self.value.dup())\n"
            "  }\n"
            "}\n"
            "pub shape Tracked(value: i32)\n"
            "impl Tracked@Encap { pub value fn drop(self#) {} }\n"
            "pub fn make() -> NonZero { return NonZero(raw = 7) }\n",
            encoding="utf-8")
        consumer.write_text(
            "import ./lib::{NonZero, Resource, make}\n"
            "fn main() -> i32 {\n"
            "  auto value = make()\n"
            "  auto copied = NonZero(value)\n"
            "  return copied.raw - 7\n"
            "}\n", encoding="utf-8")
        compile_source(provider, root, expect_success=True)
        interface = root / "lib.tki"
        assert interface.is_file(), "v5 provider did not emit a TKI"
        text = interface.read_text(encoding="utf-8")
        for expected in (
                "// @meta format_version: 2",
                "// @meta identity_schema_version: 2",
                "// @meta logical_module_path: lib",
                "// @meta resolver_binding_digest:",
                "// @tki v2 field_graph: NonZero.raw = i32",
                "// @tki v2 policy: NonZero = global:raw",
                "// @tki v2 copy_witness: NonZero = explicit-verified",
                "// @tki v2 dup_provider: NonZero = intrinsic-copy",
                "// @tki v2 copy_recipe: Capsule = all(T:@Copy)",
                "// @tki v2 dup_provider: Resource = user",
        ):
            assert expected in text, expected
        tracked_drop = re.search(
            r"// @tki v2 custom_drop: Tracked = "
            r"(Encap___toka_owner_N[0-9a-f]+_drop)", text)
        assert tracked_drop, text
        assert "structural_drop" not in text

        provider.rename(root / "lib.tk.source-hidden")
        compile_source(consumer, root, expect_success=True)

        generic_consumer = root / "generic_main.tk"
        generic_consumer.write_text(
            "import ./lib::{Capsule}\n"
            "fn main() -> i32 {\n"
            "  auto value = Capsule<i32>(value = 7)\n"
            "  auto copied = Capsule<i32>(value)\n"
            "  return copied.value - 7\n"
            "}\n", encoding="utf-8")
        compile_source(generic_consumer, root, expect_success=True)

        generic_noncopy_consumer = root / "generic_noncopy_main.tk"
        generic_noncopy_consumer.write_text(
            "import ./lib::{Capsule, Resource}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 7)\n"
            "  auto value = Capsule<Resource>(value = cede resource)\n"
            "  auto copied = Capsule<Resource>(value)\n"
            "  return copied.value.raw\n"
            "}\n", encoding="utf-8")
        rejected_generic_noncopy = compile_source(
            generic_noncopy_consumer, root, expect_success=False)
        assert "E0406" in rejected_generic_noncopy.stderr, rejected_generic_noncopy.stderr

        dup_consumer = root / "dup_main.tk"
        dup_consumer.write_text(
            "import ./lib::{Resource}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 1)\n"
            "  auto closure = { [dup resource] => resource.raw }:fn() -> i32\n"
            "  return resource.raw + closure()\n"
            "}\n", encoding="utf-8")
        compile_source(dup_consumer, root, expect_success=True, emit_llvm=True)
        dup_ir = dup_consumer.with_suffix(".ll").read_text(encoding="utf-8")
        dup_calls = re.findall(
            r"\bcall\b[^\n]*@Dup___toka_owner_N[0-9a-f]+_dup\(",
            dup_ir)
        assert len(dup_calls) == 1, dup_ir

        generic_dup_consumer = root / "generic_dup_main.tk"
        generic_dup_consumer.write_text(
            "import ./lib::{Resource, Wrapper}\n"
            "fn main() -> i32 {\n"
            "  auto resource = Resource(raw = 1)\n"
            "  auto wrapper = Wrapper<Resource>(value = cede resource)\n"
            "  auto closure = { [dup wrapper] => wrapper.value.raw }:fn() -> i32\n"
            "  return wrapper.value.raw + closure()\n"
            "}\n", encoding="utf-8")
        compile_source(generic_dup_consumer, root, expect_success=True,
                       emit_llvm=True)
        generic_dup_ir = generic_dup_consumer.with_suffix(".ll").read_text(
            encoding="utf-8")
        generic_dup_calls = re.findall(
            r"\bcall\b[^\n]*@Dup___toka_owner_N[0-9a-f]+"
            r"_M_[0-9]+_[0-9A-Za-z]+_dup\(",
            generic_dup_ir)
        assert len(generic_dup_calls) == 1, generic_dup_ir

        v1 = text.replace("// @meta format_version: 2",
                          "// @meta format_version: 1", 1)
        interface.write_text(v1, encoding="utf-8")
        rejected_v1 = compile_source(consumer, root, expect_success=False)
        assert "Incompatible or stale interface file" in rejected_v1.stderr

        forged = text.replace("// @meta logical_module_path: lib",
                              "// @meta logical_module_path: forged", 1)
        interface.write_text(forged, encoding="utf-8")
        rejected_identity = compile_source(consumer, root, expect_success=False)
        assert "resolver identity" in rejected_identity.stderr

        missing_identity = text.replace(
            "// @meta resolver_binding_digest:",
            "// ignored resolver_binding_digest:", 1)
        interface.write_text(missing_identity, encoding="utf-8")
        rejected_missing = compile_source(consumer, root, expect_success=False)
        assert "Missing logical_module_path or resolver_binding_digest" in rejected_missing.stderr

    print("encap Slice 5 TKI v2 audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print("encap Slice 5 TKI v2 audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
