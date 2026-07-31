#!/usr/bin/env python3
"""Regression evidence for the gated @encap Slice 5 TKI v2 contract."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def compile_source(source: Path, root: Path, *, expect_success: bool) -> subprocess.CompletedProcess[str]:
    output = source.with_suffix(".o")
    command = (str(TOKAC), "--encap-epoch=v5", "--workspace-node",
               "slice5-workspace-v1", "--workspace-root", str(root),
               "-c", "-o", str(output), str(source))
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
            "impl NonZero@encap { pub raw }\n"
            "impl NonZero@Copy {}\n"
            "pub shape Tracked(value: i32)\n"
            "impl Tracked@encap { pub value fn drop(self#) {} }\n"
            "pub fn make() -> NonZero { return NonZero(raw = 7) }\n",
            encoding="utf-8")
        consumer.write_text(
            "import ./lib::{NonZero, make}\n"
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
                "// @tki v2 custom_drop: Tracked = encap_Tracked_drop",
        ):
            assert expected in text, expected
        assert "structural_drop" not in text

        provider.rename(root / "lib.tk.source-hidden")
        compile_source(consumer, root, expect_success=True)

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
