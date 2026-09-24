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
                metadata = FIXTURES / "enum_metadata.tk"
                parity(metadata, 0)
                run(metadata)
                observed = compile(metadata, "--non-call-transfer-shadow=json")
                records = [record for record in json.loads(observed.stdout)["records"]
                           if record["location"]["file"].endswith("enum_metadata.tk") and
                           record["boundary"] == "initialization" and record["location"]["line"] == 4]
                assert len(records) == 1 and records[0].get("static_storage_origins") and not records[0]["plan"]["dependency_roots"], records
                print("PASS exact static Err payload metadata with input-bound return unchanged", flush=True)
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
                            "void leaf_freed(void *p) { if (enabled && p) ++count; }\n"
                            "static int tokens[16];\n"
                            "void token_drop(int id) { if (id >= 0 && id < 16) ++tokens[id]; }\n"
                            "int token_count(int id) { return id >= 0 && id < 16 ? tokens[id] : -1; }\n")
            clang = os.environ.get("CC") or next((path for path in ("/opt/homebrew/opt/llvm@20/bin/clang",
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
                if not args.entry_baseline:
                    resource = library / "stdx/serde/replacement_probe.tk"
                    resource_body = (FIXTURES / "borrowed_replacement.tk").read_text().replace("fn exercise()", "pub fn exercise()")
                    resource.write_text(resource_body)
                    driver = work / "resource_replacement.tk"
                    driver.write_text("import stdx/serde/replacement_probe::{exercise}\nfn main() -> i32 { return exercise() }\n")
                    parity(driver, 0, environment=dict(env, TOKA_LIB=str(library)))
                    run(driver, tracker, environment=dict(env, TOKA_LIB=str(library)))
                    print("PASS borrowed replacement: 6 resource/callee-scope/moved-from checks", flush=True)
                    fault_source = FIXTURES / "replacement_fault.tk"
                    # Shadow is check-only by contract; it cannot exercise a
                    # CodeGen fault. Sema parity is checked independently above.
                    for output_flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                        output = work / ("replacement-positive" + extension)
                        admitted = compile(fault_source, output_flag, "-o", output)
                        assert admitted.returncode == 0 and output.exists(), admitted.stderr
                    for fault in ("missing", "rejected", "type", "source", "destination", "snapshot", "place"):
                        for output_flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                            output = work / (fault + extension)
                            failed = compile(fault_source, output_flag, "-o", output,
                                             "--borrowed-replacement-fault=" + fault)
                            assert failed.returncode == 1 and "E0701" in failed.stderr and "borrowed value replacement" in failed.stderr and not output.exists(), (fault, failed.stderr)
                    print("PASS borrowed replacement: 14 missing/mismatch/no-artifact checks", flush=True)
                    readonly = FIXTURES / "replacement_readonly.tk"
                    rejected = parity(readonly, 1)
                    assert "E0438" not in rejected.stderr and "E0410" not in rejected.stderr, rejected.stderr
                    for output_flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                        output = work / ("readonly" + extension)
                        rejected = compile(readonly, output_flag, "-o", output)
                        assert rejected.returncode == 1 and "error[E" in rejected.stderr and not output.exists(), rejected.stderr
                    print("PASS readonly replacement rejects and restores source; no artifacts", flush=True)

        if args.section in ("static-error", "all"):
            # This is deliberately not an expected-failure oracle: until it
            # passes the concrete factory slice cannot claim its full matrix.
            parity(FIXTURES / "static_error.tk", 0)
            run(FIXTURES / "static_error.tk")
            print("PASS static error independent of input", flush=True)
            parity(FIXTURES / "enum_static_sources.tk", 0)
            run(FIXTURES / "enum_static_sources.tk")
            print("PASS concrete producers and static rebinding", flush=True)
            parity(FIXTURES / "enum_shadow_static.tk", 0)
            run(FIXTURES / "enum_shadow_static.tk")
            print("PASS original static binding survives dynamic name shadowing", flush=True)
            rollback = parity(FIXTURES / "enum_failed_call_rollback.tk", 1)
            assert "E04554" in rollback.stderr and "E0455]" not in rollback.stderr and "E0438" not in rollback.stderr, rollback.stderr
            print("PASS rejected call restores enum source", flush=True)


if __name__ == "__main__":
    main()
