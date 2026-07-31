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
POLICY_BLOCK = re.compile(r"^\s*impl[^\n]*@encap\s*\{")
METHOD_DECL = re.compile(r"^\s*(?:pub\s+)?fn\s+([^ (]+)")


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


def compile_library_source(source: Path, output: Path) -> None:
    command = (str(TOKAC), "--encap-epoch=v6", "--workspace-node",
               "slice6-library-v1", "--workspace-root", str(ROOT),
               "-c", "-o", str(output), str(source))
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        raise RuntimeError("library source failed:\n$ %s\n%s" %
                           (" ".join(command), completed.stderr))


def brace_delta(line: str) -> int:
    depth = 0
    quoted = False
    escaped = False
    for index, char in enumerate(line):
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == "/" and index + 1 < len(line) and line[index + 1] == "/":
            break
        if char == '"':
            quoted = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
    return depth


def assert_policy_blocks_only_contain_drop(source: Path, text: str) -> None:
    depth = 0
    in_policy = False
    for line in text.splitlines():
        if not in_policy:
            if POLICY_BLOCK.match(line):
                in_policy = True
                depth = brace_delta(line)
            continue
        if depth == 1:
            method = METHOD_DECL.match(line)
            if method and method.group(1) != "drop":
                raise AssertionError("%s: @encap policy contains %s" %
                                     (source, method.group(1)))
        depth += brace_delta(line)
        if depth == 0:
            in_policy = False


def assert_migrated_text() -> None:
    for source in (ROOT / "lib").rglob("*.tk"):
        text = source.read_text(encoding="utf-8")
        for pattern in LIBRARY_PATTERNS:
            assert not pattern.search(text), "%s: %s" % (source, pattern.pattern)
        assert_policy_blocks_only_contain_drop(source, text)

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
        for index, library_source in enumerate((
                ROOT / "lib" / "core" / "string.tk",
                ROOT / "lib" / "std" / "vec.tk",
                ROOT / "lib" / "stdx" / "net" / "http.tk",
                ROOT / "lib" / "build.tk")):
            compile_library_source(library_source, root / ("library-%d.o" % index))

        valid = root / "valid.tk"
        valid.write_text(
            "shape Point(x: i32)\n"
            "shape RawHandle(*slot: void)\n"
            "trait @Dup { pub fn dup(self) -> Self }\n"
            "shape Secret(raw: i32)\n"
            "impl Secret@encap { pub raw }\n"
            "impl Secret@Copy {}\n"
            "shape TaskRef(*handle: void)\n"
            "impl TaskRef@encap {\n"
            "  pub handle\n"
            "  fn drop(self#) {}\n"
            "}\n"
            "impl TaskRef@Dup { pub fn dup(self) -> Self { return TaskRef(*handle = self.*handle) } }\n"
            "fn main() -> i32 {\n"
            "  auto point = Point(x = 1)\n"
            "  auto point_copy = Point(point)\n"
            "  auto raw = RawHandle(*slot = null as *void)\n"
            "  auto raw_copy = RawHandle(raw)\n"
            "  auto secret = Secret(raw = 2)\n"
            "  auto secret_copy = Secret(secret)\n"
            "  auto task = TaskRef(*handle = null as *void)\n"
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
