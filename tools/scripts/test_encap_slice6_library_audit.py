#!/usr/bin/env python3
"""Regression evidence for the @encap Slice 6 library migration."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"
LIBRARY_PATTERNS = (
    re.compile(r"=\s*delete\b"),
    re.compile(r"@(?:Clone|Drop)\b"),
    re.compile(r"\bpub(?:\([^)]*\))?\s+\*(?:\s*!|\s*(?:\n|$))"),
)
NORMATIVE_DOCUMENTS = (ROOT / "docs" / "syntax.md",
                       ROOT / "docs" / "syntax_zh.md")


def compile_source(source: Path, *, expect_success: bool) -> subprocess.CompletedProcess[str]:
    output = source.with_suffix(".ll")
    command = (str(TOKAC), "--encap-epoch=v6", "--workspace-node",
               "slice6-workspace-v1", "--workspace-root", str(source.parent),
               "-c", "--emit-llvm", "-o", str(output), str(source))
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (completed.returncode == 0) != expect_success:
        outcome = "unexpectedly succeeded" if completed.returncode == 0 else "failed"
        raise RuntimeError("command %s:\n$ %s\n%s" %
                           (outcome, " ".join(command), completed.stderr))
    return completed


def assert_migrated_text() -> None:
    for source in (ROOT / "lib").rglob("*.tk"):
        text = source.read_text(encoding="utf-8")
        for pattern in LIBRARY_PATTERNS:
            assert not pattern.search(text), "%s: %s" % (source, pattern.pattern)

    for document in NORMATIVE_DOCUMENTS:
        text = document.read_text(encoding="utf-8")
        assert "= delete" not in text, document
        assert "@Clone" not in text and "@Drop" not in text, document
        assert "pub *" not in text, document

    marker = (ROOT / "lib" / "core" / "marker.tk").read_text(encoding="utf-8")
    traits = (ROOT / "lib" / "core" / "traits.tk").read_text(encoding="utf-8")
    vec = (ROOT / "lib" / "std" / "vec.tk").read_text(encoding="utf-8")
    assert "pub trait @Copy {}" in marker
    assert "pub trait @Dup" in marker and "pub fn dup(self) -> Self" in marker
    assert "pub trait @encap {}" in traits
    assert "impl<'T> Vec<'T>@encap" in vec
    assert "impl<'T: @Copy> Vec<'T>@encap" not in vec


def reject(root: Path, name: str, source: str) -> None:
    result_path = root / name
    result_path.write_text(source, encoding="utf-8")
    rejected = compile_source(result_path, expect_success=False)
    assert "E0406" in rejected.stderr, rejected.stderr


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    assert_migrated_text()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        valid = root / "valid.tk"
        valid.write_text(
            "shape Point(x: i32)\n"
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "shape Secret(raw: i32)\n"
            "impl Secret@encap { pub raw }\n"
            "impl Secret@Copy {}\n"
            "shape TaskRef(handle: i32)\n"
            "impl TaskRef@encap {\n"
            "  pub handle\n"
            "  fn drop(self#) {}\n"
            "}\n"
            "impl TaskRef@Dup { pub fn dup(self) -> Self { return TaskRef(handle = self.handle) } }\n"
            "fn main() -> i32 {\n"
            "  auto point = Point(x = 1)\n"
            "  auto point_copy = Point(point)\n"
            "  auto secret = Secret(raw = 2)\n"
            "  auto secret_copy = Secret(secret)\n"
            "  auto task = TaskRef(handle = 0)\n"
            "  auto task_copy = task.dup()\n"
            "  return point_copy.x + secret_copy.raw\n"
            "}\n", encoding="utf-8")
        compile_source(valid, expect_success=True)

        reject(root, "deleted.tk", "fn obsolete() = delete\nfn main() -> i32 { return 0 }\n")
        reject(root, "legacy_facet.tk", "trait @Clone {}\nfn main() -> i32 { return 0 }\n")

    print("encap Slice 6 library audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print("encap Slice 6 library audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
