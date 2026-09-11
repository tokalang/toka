#!/usr/bin/env python3
"""Concrete JSON factories; static-error lifetime remains a required positive."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/json_leaf_factories"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--section", choices=("all", "core", "entries", "static-error"), default="all")
    parser.add_argument("--entry-baseline", action="store_true",
                        help="diagnose exact pre-slice entry bodies; never an acceptance mode")
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-json-leaf-") as directory:
        work = Path(directory)

        def compile(source, *flags, environment=env):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  cwd=Path(environment["TOKA_LIB"]).parent, env=environment, text=True,
                                  capture_output=True, timeout=90)

        def parity(source, expected, environment=env):
            normal = compile(source, "--check-only", environment=environment)
            shadow = compile(source, "--check-only", "--non-call-transfer-shadow=json", environment=environment)
            assert normal.returncode == shadow.returncode == expected, (source.name, normal.stderr, shadow.stderr)
            assert normal.stderr == shadow.stderr, (source.name, normal.stderr, shadow.stderr)
            json.loads(shadow.stdout)
            return normal

        def run(source, *objects, environment=env):
            output = work / source.stem
            built = compile(source, *objects, "-o", output, environment=environment)
            assert built.returncode == 0, (source.name, built.stderr)
            result = subprocess.run([str(output)], text=True, capture_output=True, timeout=10)
            assert result.returncode == 0, (source.name, result.returncode, result.stderr)

        if args.section in ("core", "entries", "all"):
            # Compile the exact existing trait/three entry-point bodies with
            # the leaf tests. This isolates unrelated JsonNode/generic debt;
            # it is not a claim that the complete json module is restored.
            module = (ROOT / "lib/stdx/serde/json.tk").read_text()
            if args.entry_baseline:
                assert args.section == "entries", "baseline comparison is entries-only"
                module = subprocess.run(["git", "show", "e1402bd23ef78f460021a432422689cfc4ae1fd9:lib/stdx/serde/json.tk"],
                                        cwd=ROOT, text=True, capture_output=True, check=True).stdout
            declarations = []
            for header in ("pub trait @FromJson", "impl i32@FromJson",
                           "impl string@FromJson", "impl str@FromJson"):
                matches = re.findall(r"^" + re.escape(header) + r" \{\n.*?^\}",
                                     module, flags=re.M | re.S)
                assert len(matches) == 1, header
                declarations.append(matches[0])
            alias = re.findall(r"^pub alias JsonRes = .*", module, flags=re.M)
            assert len(alias) == 1
            suffix = "\n" + alias[0] + "\n" + "\n".join(declarations) + "\n"
            if args.entry_baseline:
                for header in ("pub fn json_skip_whitespace", "fn json_parse_hex4", "fn json_push_utf8"):
                    helpers = re.findall(r"^" + re.escape(header) + r"\(.*?^\}", module, flags=re.M | re.S)
                    assert len(helpers) == 1, header
                    suffix += helpers[0] + "\n"
            if args.section != "entries":
                basic = FIXTURES / "basic.tk"
                parity(basic, 0)
                run(basic)
                print("PASS parsing/runtime/parity: 22 checks", flush=True)
            for source in sorted(FIXTURES.glob("reject_*.tk")) if args.section != "entries" else []:
                normal = parity(source, 1)
                diagnostic = "E0454" if source.name == "reject_wrong_dependency.tk" else "E0455"
                assert "error[" + diagnostic + "]" in normal.stderr and source.name in normal.stderr, normal.stderr
                for flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (source.stem + extension)
                    result = compile(source, flag, "-o", output)
                    assert result.returncode == 1 and not output.exists(), (source.name, result.stderr)
                print("PASS lifetime/parity/no-artifact: " + source.name, flush=True)

            # Observe the real string release instructions in an isolated copy.
            # No production allocator, Drop or RC implementation is changed.
            library = work / "lib"
            shutil.copytree(ROOT / "lib", library,
                            ignore=shutil.ignore_patterns("*.o", "*.tki", "__pycache__"))
            string = library / "core/string.tk"
            text = string.read_text()
            release = "unsafe free [0]self.*#buf"
            assert text.count(release) == 2, "review changed string release instrumentation"
            string.write_text("extern fn leaf_freed(nul *ptr: void) -> void\n" +
                              text.replace(release, "leaf_freed(self.*buf as nul *void); " + release))
            shim = work / "tracker.c"
            shim.write_text("static int enabled, count;\n"
                            "void leaf_watch(void) { enabled = 1; count = 0; }\n"
                            "int leaf_releases(void) { return count; }\n"
                            "void leaf_freed(void *p) { if (enabled && p) ++count; }\n")
            clang = next((path for path in ("/opt/homebrew/opt/llvm@20/bin/clang",
                         "/opt/homebrew/opt/llvm/bin/clang", shutil.which("clang"))
                         if path and Path(path).is_file()), None)
            assert clang, "clang required for cleanup instrumentation"
            tracker = work / "tracker.o"
            subprocess.run([clang, "-c", str(shim), "-o", str(tracker)], check=True)
            cleanup = work / "cleanup.tk"
            if args.section != "entries":
                cleanup.write_text((FIXTURES / "cleanup.tk").read_text())
                run(cleanup, tracker, environment=dict(env, TOKA_LIB=str(library)))
                print("PASS factory cleanup: 8 checks, discard/early-return/failure/input death", flush=True)
            if args.section in ("entries", "all"):
                # Preserve a logical module identity for the real method
                # bodies; external main-file definitions otherwise have an
                # unrelated fail-closed assignment identity limitation.
                adapter = library / "stdx/serde/json_leaf_entries.tk"
                adapter.write_text("import stdx/serde/json_leaf::{parse_i32_value, parse_string_value, parse_str_view}\n" + suffix)
                entry = work / "entries.tk"
                import_entry = "import stdx/serde/json_leaf_entries::{@FromJson}\n"
                entry.write_text(import_entry + (FIXTURES / "entries.tk").read_text())
                parity(entry, 0, environment=dict(env, TOKA_LIB=str(library)))
                # The old decoder has no truncated-unicode refusal; baseline
                # mode diagnoses replacement cleanup only, not new semantics.
                if not args.entry_baseline:
                    run(entry, tracker, environment=dict(env, TOKA_LIB=str(library)))
                cleanup.write_text(import_entry + (FIXTURES / "entry_cleanup.tk").read_text())
                run(cleanup, tracker, environment=dict(env, TOKA_LIB=str(library)))
                print("PASS existing entries: 5 behavior, 4 cleanup checks", flush=True)

        if args.section in ("static-error", "all"):
            # This is deliberately not an expected-failure oracle: until it
            # passes the concrete factory slice cannot claim its full matrix.
            parity(FIXTURES / "static_error.tk", 0)
            run(FIXTURES / "static_error.tk")
            print("PASS static error independent of input", flush=True)


if __name__ == "__main__":
    main()
