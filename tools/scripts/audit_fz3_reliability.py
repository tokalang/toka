#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile


SANITIZER_MARKERS = (
    "AddressSanitizer",
    "LeakSanitizer",
    "UndefinedBehaviorSanitizer",
    "SUMMARY: UndefinedBehaviorSanitizer",
    "runtime error:",
)


def run(command, cwd, timeout=15):
    try:
        result = subprocess.run(
            command,
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        return {
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "timeout": False,
        }
    except subprocess.TimeoutExpired as error:
        return {
            "returncode": None,
            "stdout": error.stdout or b"",
            "stderr": error.stderr or b"",
            "timeout": True,
        }


def sanitizer_failure(result):
    output = (result["stdout"] + result["stderr"]).decode(
        "utf-8", errors="replace"
    )
    return any(marker in output for marker in SANITIZER_MARKERS)


def compiler_failure_reason(result):
    if result["timeout"]:
        return "timed out"
    if result["returncode"] is None:
        return "did not return a status"
    if result["returncode"] < 0:
        return "crashed with signal %d" % (-result["returncode"])
    if sanitizer_failure(result):
        return "reported sanitizer diagnostics"
    return None


def mutate_parser_source(source, rng, index):
    if not source:
        return source
    operation = index % 5
    start = rng.randrange(len(source))
    width = 1 + rng.randrange(min(12, len(source) - start))
    if operation == 0:
        return source[:start] + source[start + width :]
    if operation == 1:
        tokens = ("{", "}", "(", ")", "'", "cede ", "@@", "\x00")
        return source[:start] + rng.choice(tokens) + source[start:]
    if operation == 2:
        replacement = rng.choice(("?", "#", "<", "]", "0", "fn"))
        return source[:start] + replacement + source[start + width :]
    if operation == 3:
        return source[:start]
    span = source[start : start + width]
    return source[:start] + span + span + source[start + width :]


def compile_source(tokac, root, source_path, object_path, timeout):
    return run(
        [str(tokac), "-c", str(source_path), "-o", str(object_path)],
        root,
        timeout=timeout,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tokac", default="./build/bin/tokac")
    parser.add_argument("--seed", type=int, default=0x544F4B41)
    parser.add_argument("--parser-mutations", type=int, default=32)
    parser.add_argument("--timeout", type=int, default=15)
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    root = Path(__file__).resolve().parents[2]
    tokac = Path(args.tokac)
    if not tokac.is_absolute():
        tokac = (root / tokac).resolve()
    rng = random.Random(args.seed)
    failures = []
    counts = {
        "core_corpus": 0,
        "parser_mutations": 0,
        "sema_mutations": 0,
        "interface_mutations": 0,
        "determinism_checks": 0,
    }

    parser_seed = """shape Data(value: i32)\n\nfn main() -> i32 {\n    auto item = Data(value = 7)\n    return item.value - 7\n}\n"""
    sema_mutants = (
        parser_seed.replace("item.value", "missing.value"),
        parser_seed.replace("Data(value: i32)", "Data(value: MissingType)"),
        parser_seed.replace("Data(value = 7)", "Unknown(value = 7)"),
        "fn take(cede value: i32) -> i32 { return value }\nfn main() -> i32 { return take(1) }\n",
        "shape Box(value: i32)\nfn main() -> i32 { auto a = Box(value=1); auto b = cede a; return a.value }\n",
        "fn main() -> i32 { auto x: i32 = true; return x }\n",
        "fn main() -> i32 { auto x = missing(); return x }\n",
        "shape Pair(left: i32, right: i32)\nfn main() -> i32 { auto p = Pair(left=1); return p.left }\n",
        "fn worker() -> async i32 { return 0 }\nfn main() -> i32 { return worker().await }\n",
        "fn main() -> i32 { auto x = 1; auto y = cede x; return cede x }\n",
        "fn main() -> i32 { unsafe free 1; return 0 }\n",
    )
    core_pass = (
        "tests/pass/g01_simple.tk",
        "tests/pass/g03_import_selective_alias.tk",
        "tests/pass/g04_anon_records.tk",
        "tests/pass/g07_btree_map_test.tk",
        "tests/pass/g07_slab_test.tk",
        "tests/pass/g08_associated_type_basic.tk",
        "tests/pass/g08_auto_drop_composite.tk",
        "tests/pass/g08_generic_buffer.tk",
        "tests/pass/g08_pal_stress_test.tk",
        "tests/pass/g08_sret_closure.tk",
        "tests/pass/g09_async_suspension_state.tk",
        "tests/pass/g09_resource_cleanup_matrix.tk",
    )
    core_fail = (
        "tests/fail/borrow.tk",
        "tests/fail/closure_cede_capture_consumes.tk",
        "tests/fail/import_selective_alias_leak.tk",
        "tests/fail/import_selective_extern_leak.tk",
        "tests/fail/import_selective_global_leak.tk",
        "tests/fail/import_selective_shape_leak.tk",
        "tests/fail/import_selective_symbol_leak.tk",
        "tests/fail/import_selective_trait_leak.tk",
        "tests/fail/move_member_drop.tk",
        "tests/fail/parser_grouping_missing_expression.tk",
        "tests/fail/parser_named_init_missing_delimiter.tk",
        "tests/fail/thread_spawn_implicit_capture_escape.tk",
    )

    with tempfile.TemporaryDirectory(prefix="toka-fz3-reliability-") as temp:
        work = Path(temp)

        for index, relative in enumerate(core_pass):
            result = compile_source(
                tokac, root, root / relative,
                work / ("core_pass_%02d.o" % index), args.timeout
            )
            counts["core_corpus"] += 1
            failure_reason = compiler_failure_reason(result)
            if failure_reason:
                failures.append("core pass case %s %s" % (relative, failure_reason))
            elif result["returncode"] != 0:
                failures.append("core pass case %s was rejected" % relative)

        for index, relative in enumerate(core_fail):
            result = compile_source(
                tokac, root, root / relative,
                work / ("core_fail_%02d.o" % index), args.timeout
            )
            counts["core_corpus"] += 1
            failure_reason = compiler_failure_reason(result)
            if failure_reason:
                failures.append("core fail case %s %s" % (relative, failure_reason))
            elif result["returncode"] == 0:
                failures.append("core fail case %s was accepted" % relative)

        for index in range(args.parser_mutations):
            source = mutate_parser_source(parser_seed, rng, index)
            path = work / ("parser_%03d.tk" % index)
            path.write_bytes(source.encode("utf-8", errors="surrogatepass"))
            result = compile_source(
                tokac, root, path, work / (path.stem + ".o"), args.timeout
            )
            counts["parser_mutations"] += 1
            failure_reason = compiler_failure_reason(result)
            if failure_reason:
                failures.append("parser mutation %d %s" % (index, failure_reason))

        sema_order = list(range(len(sema_mutants)))
        rng.shuffle(sema_order)
        for output_index, mutant_index in enumerate(sema_order):
            path = work / ("sema_%03d.tk" % output_index)
            path.write_text(sema_mutants[mutant_index], encoding="utf-8")
            result = compile_source(
                tokac, root, path, work / (path.stem + ".o"), args.timeout
            )
            counts["sema_mutations"] += 1
            failure_reason = compiler_failure_reason(result)
            if failure_reason:
                failures.append("sema mutation %d %s" % (mutant_index, failure_reason))
            elif result["returncode"] == 0:
                failures.append("sema mutation %d was unexpectedly accepted" % mutant_index)

        interface_dir = work / "interface"
        interface_dir.mkdir()
        library = interface_dir / "lib.tk"
        consumer = interface_dir / "main.tk"
        library.write_text(
            "pub shape Item(value: i32)\npub fn make() -> Item { return Item(value=7) }\n",
            encoding="utf-8",
        )
        consumer.write_text(
            "import ./lib::{Item, make}\nfn main() -> i32 { return make().value - 7 }\n",
            encoding="utf-8",
        )
        base_compile = run(
            [str(tokac), "-c", str(library), "-o", str(interface_dir / "lib.o")],
            root,
            timeout=args.timeout,
        )
        base_tki = interface_dir / "lib.tki"
        base_failure_reason = compiler_failure_reason(base_compile)
        if base_failure_reason or base_compile["returncode"] != 0:
            suffix = base_failure_reason or "was rejected"
            failures.append("could not generate mutation interface fixture: " + suffix)
        elif not base_tki.exists():
            failures.append("interface fixture did not emit lib.tki")
        else:
            original_tki = base_tki.read_bytes()
            library.unlink()
            mutations = []
            for fraction in (8, 6, 4, 3, 2):
                mutations.append(original_tki[: max(1, len(original_tki) // fraction)])
            text = original_tki.decode("utf-8", errors="replace")
            mutations.extend(
                (
                    text.replace("compiler_version:", "compiler_version_broken:", 1).encode(),
                    text.replace("target_triple:", "target_triple_broken:", 1).encode(),
                    text.replace("source_hash:", "source_hash_broken:", 1).encode(),
                    "\n".join(
                        line for line in text.splitlines() if "// @meta" not in line
                    ).encode(),
                    b"\x00\xffnot-an-interface\n",
                )
            )
            for index, mutated in enumerate(mutations):
                case = work / ("interface_case_%02d" % index)
                case.mkdir()
                shutil.copy2(interface_dir / "lib.o", case / "lib.o")
                shutil.copy2(consumer, case / "main.tk")
                (case / "lib.tki").write_bytes(mutated)
                result = run(
                    [
                        str(tokac),
                        str(case / "main.tk"),
                        str(case / "lib.o"),
                        "-o",
                        str(case / "app"),
                    ],
                    root,
                    timeout=args.timeout,
                )
                counts["interface_mutations"] += 1
                failure_reason = compiler_failure_reason(result)
                if failure_reason:
                    failures.append("interface mutation %d %s" % (index, failure_reason))
                elif result["returncode"] == 0:
                    failures.append("interface mutation %d was unexpectedly accepted" % index)

        deterministic = work / "deterministic.tk"
        deterministic.write_text(
            "fn main() -> i32 { return missing_name }\n", encoding="utf-8"
        )
        first = compile_source(
            tokac, root, deterministic, work / "deterministic.o", args.timeout
        )
        second = compile_source(
            tokac, root, deterministic, work / "deterministic.o", args.timeout
        )
        counts["determinism_checks"] += 1
        if first["stderr"] != second["stderr"] or first["returncode"] != second["returncode"]:
            failures.append("diagnostic output is not deterministic")

        evidence = work / "evidence.tk"
        evidence.write_text(
            "fn main() -> i32 { auto value = 1; return value - 1 }\n",
            encoding="utf-8",
        )
        evidence_command = [
            str(tokac),
            "--dump-semantic-evidence=json",
            "-c",
            str(evidence),
            "-o",
            str(work / "evidence.o"),
        ]
        evidence_first = run(evidence_command, root, timeout=args.timeout)
        evidence_second = run(evidence_command, root, timeout=args.timeout)
        counts["determinism_checks"] += 1
        if (
            evidence_first["stdout"] != evidence_second["stdout"]
            or evidence_first["stderr"] != evidence_second["stderr"]
        ):
            failures.append("semantic evidence output is not deterministic")

        dependencies_command = [
            str(tokac),
            "--dump-dependencies=json",
            str(evidence),
        ]
        deps_first = run(dependencies_command, root, timeout=args.timeout)
        deps_second = run(dependencies_command, root, timeout=args.timeout)
        counts["determinism_checks"] += 1
        if deps_first["stdout"] != deps_second["stdout"]:
            failures.append("dependency JSON output is not deterministic")

        stable_lib = work / "stable_lib.tk"
        stable_lib.write_text("pub fn value() -> i32 { return 7 }\n", encoding="utf-8")
        stable_object = work / "stable_lib.o"
        stable_command = [str(tokac), "-c", str(stable_lib), "-o", str(stable_object)]
        stable_first = run(stable_command, root, timeout=args.timeout)
        stable_tki = work / "stable_lib.tki"
        first_bytes = stable_tki.read_bytes() if stable_tki.exists() else b""
        stable_second = run(stable_command, root, timeout=args.timeout)
        second_bytes = stable_tki.read_bytes() if stable_tki.exists() else b""
        counts["determinism_checks"] += 1
        if (
            stable_first["returncode"] != 0
            or stable_second["returncode"] != 0
            or not first_bytes
            or first_bytes != second_bytes
        ):
            failures.append("interface output is not deterministic")

    report = {
        "schema": "toka.fz3-reliability-audit",
        "version": 1,
        "seed": args.seed,
        "counts": counts,
        "result": "pass" if not failures else "fail",
        "failures": failures,
    }
    rendered = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
    if args.output:
        output = Path(args.output)
        if not output.is_absolute():
            output = root / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
    sys.stdout.write(rendered)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
