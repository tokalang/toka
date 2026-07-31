#!/usr/bin/env python3
"""End-to-end evidence for the @encap policy epoch Slice 2 checks."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOKAC = ROOT / "build" / "bin" / "tokac"


def compile_source(source: Path, root: Path, *, expect_success: bool,
                   workspace_node: str = "slice2-workspace-v1",
                   extra: tuple[str, ...] = ()) -> subprocess.CompletedProcess[str]:
    command = (str(TOKAC), "--workspace-node", workspace_node,
               "--workspace-root", str(root), "-c", "-o",
               str(source.with_suffix(".o")), *extra, str(source))
    completed = subprocess.run(command, cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (completed.returncode == 0) != expect_success:
        outcome = "unexpectedly succeeded" if completed.returncode == 0 else "failed"
        raise RuntimeError("command %s:\n$ %s\n%s" %
                           (outcome, " ".join(command), completed.stderr))
    return completed


def write_policy(path: Path, grant: str) -> None:
    path.write_text(
        "pub shape Box(visible: i32, hidden: i32)\n"
        "impl Box@encap {\n"
        "    " + grant + "\n"
        "}\n"
        "pub fn make() -> Box { return Box(visible = 1, hidden = 2) }\n",
        encoding="utf-8")


def main() -> int:
    if not TOKAC.is_file():
        raise RuntimeError("build/bin/tokac is missing; run cmake --build build first")

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        lib = root / "lib.tk"
        main = root / "main.tk"
        write_policy(lib, "pub visible")

        main.write_text(
            "import ./lib\n"
            "fn main() -> i32 { auto value = lib::make(); return value.visible }\n",
            encoding="utf-8")
        compile_source(main, root, expect_success=True)

        main.write_text(
            "import ./lib\n"
            "fn main() -> i32 { auto value = lib::make(); return value.hidden }\n",
            encoding="utf-8")
        denied = compile_source(main, root, expect_success=False)
        assert "E0418" in denied.stderr

        main.write_text(
            "import ./lib::{Box}\n"
            "fn main() -> i32 { auto value = Box(visible = 1, hidden = 2); return 0 }\n",
            encoding="utf-8")
        denied_init = compile_source(main, root, expect_success=False)
        if "E0418" not in denied_init.stderr:
            raise AssertionError(denied_init.stderr)

        main.write_text(
            "import ./lib::{Box, make}\n"
            "fn main() -> i32 { auto source = make(); auto value = Box(visible = 2, source.*); return 0 }\n",
            encoding="utf-8")
        denied_spread = compile_source(main, root, expect_success=False)
        assert "E0418" in denied_spread.stderr

        main.write_text(
            "import ./lib::{Box, make}\n"
            "fn main() -> i32 { auto source = make(); auto Box(extracted = .hidden, ..) = source; return 0 }\n",
            encoding="utf-8")
        denied_pattern = compile_source(main, root, expect_success=False)
        assert "E0418" in denied_pattern.stderr

        write_policy(lib, "pub(crate) visible")
        main.write_text(
            "import ./lib\n"
            "fn main() -> i32 { auto value = lib::make(); return value.visible }\n",
            encoding="utf-8")
        rejected_crate = compile_source(main, root, expect_success=False)
        assert "E01252" in rejected_crate.stderr

        write_policy(lib, "pub(friend) visible")
        rejected_path = compile_source(main, root, expect_success=False)
        assert "E01252" in rejected_path.stderr

        conditional = root / "conditional.tk"
        conditional.write_text(
            "shape Generic<'T>(value: T)\n"
            "impl<'T: @Send> Generic<'T>@encap { pub value }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        rejected_conditional = compile_source(conditional, root, expect_success=False)
        if "E0406" not in rejected_conditional.stderr:
            raise AssertionError(rejected_conditional.stderr)

        duplicate = root / "duplicate.tk"
        duplicate.write_text(
            "shape Duplicate(value: i32)\n"
            "impl Duplicate@encap { pub value }\n"
            "impl Duplicate@encap { pub value }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        rejected_duplicate = compile_source(duplicate, root, expect_success=False)
        assert "E0406" in rejected_duplicate.stderr

        drop = root / "drop.tk"
        drop.write_text(
            "shape WithDrop(value: i32)\n"
            "impl WithDrop@encap { pub value fn drop(self#) {} }\n"
            "fn main() -> i32 { return 0 }\n", encoding="utf-8")
        compile_source(drop, root, expect_success=True)

    print("encap Slice 2 policy audit: PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print("encap Slice 2 policy audit: FAILED: %s" % error, file=sys.stderr)
        raise SystemExit(1)
