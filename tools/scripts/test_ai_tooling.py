#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    suffix = ".exe" if sys.platform == "win32" else ""
    binary_dir = (root / args.build_dir / "bin").resolve()
    tokac = binary_dir / ("tokac" + suffix)
    toka = binary_dir / ("toka" + suffix)
    require(tokac.is_file() and toka.is_file(), "Toka SDK binaries are missing")

    def run(command, expected=0, cwd=root):
        result = subprocess.run(
            [str(part) for part in command], cwd=cwd, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        if result.returncode != expected:
            raise RuntimeError(
                "expected exit %d, got %d: %s\n%s%s"
                % (expected, result.returncode,
                   " ".join(str(part) for part in command),
                   result.stdout, result.stderr)
            )
        return result

    moved = root / "tests/fail/borrow_move.tk"
    structured = json.loads(run(
        [tokac, "--diagnostics-json", "--check-only", moved], expected=1
    ).stdout)
    require(structured["schema"] == "toka.diagnostics" and
            structured["version"] == 2 and not structured["success"],
            "structured diagnostic envelope is invalid")
    move = next(item for item in structured["diagnostics"]
                if item["code"] == "E0440")
    require(move["severity"] == "error" and move["primary"]["file"] and
            move["related"] and
            move["related"][0]["label"] == "conflicting borrow originates here",
            "multi-span ownership diagnostic is incomplete")

    deprecated = root / "tests/tooling/diagnostics/deprecated_var.tk"
    fix_document = json.loads(run(
        [tokac, "--diagnostics-json", "--check-only", deprecated], expected=1
    ).stdout)
    diagnostic = next(item for item in fix_document["diagnostics"]
                      if item["code"] == "E01244")
    require(len(diagnostic["fixes"]) == 1 and
            diagnostic["fixes"][0]["applicability"] == "machine-applicable",
            "deprecated var diagnostic has no machine-applicable fix")
    edit = diagnostic["fixes"][0]["edits"][0]
    require(edit["newText"] == "auto" and edit["file"] == str(deprecated),
            "deprecated var fix edit is invalid")

    with tempfile.TemporaryDirectory(prefix="toka-fix-apply-") as temp:
        fixed = Path(temp) / "fixed.tk"
        lines = deprecated.read_text(encoding="utf-8").splitlines(keepends=True)
        start = edit["range"]["start"]
        end = edit["range"]["end"]
        require(start["line"] == end["line"], "test fix unexpectedly spans lines")
        line = lines[start["line"]]
        lines[start["line"]] = (
            line[:start["character"]] + edit["newText"] +
            line[end["character"]:]
        )
        fixed.write_text("".join(lines), encoding="utf-8")
        run([tokac, "--check-only", fixed])

    def test_apply_and_recompile(fixture_path, code, expect_exit, expected_snippets):
        doc = json.loads(run([tokac, "--diagnostics-json", "--check-only", fixture_path], expected=expect_exit).stdout)
        edits = []
        for d in doc.get("diagnostics", []):
            if d.get("code") == code:
                for f in d.get("fixes", []):
                    for e in f.get("edits", []):
                        edits.append(e)
        require(edits, f"no edits found for {code} in {fixture_path.name}")
        edits.sort(key=lambda e: (e["range"]["start"]["line"], e["range"]["start"]["character"]), reverse=True)
        lines = fixture_path.read_text(encoding="utf-8").splitlines(keepends=True)
        for e in edits:
            line_idx = e["range"]["start"]["line"]
            start_c = e["range"]["start"]["character"]
            end_c = e["range"]["end"]["character"]
            lines[line_idx] = lines[line_idx][:start_c] + e["newText"] + lines[line_idx][end_c:]
        fixed_text = "".join(lines)
        for snippet in expected_snippets:
            require(snippet in fixed_text, f"expected snippet '{snippet}' not found in fixed source")
        with tempfile.TemporaryDirectory(prefix=f"toka-fix-{code}-") as temp:
            fixed = Path(temp) / "fixed.tk"
            fixed.write_text(fixed_text, encoding="utf-8")
            recheck = json.loads(run([tokac, "--diagnostics-json", "--check-only", fixed]).stdout)
            require(not any(d.get("code") == code for d in recheck.get("diagnostics", [])),
                    f"diagnostic {code} still present after applying machine-fixes")
            run([tokac, "--check-only", fixed])

    test_apply_and_recompile(
        root / "tests/warn/call_arg_missing_mutable_sigil.tk",
        "W0408", 0,
        ["inc(c#)", "box#.update_other(c#)", "Service::static_inc(c#)", "f#(c#)", "inc(container.item#)", "mutate_i32(values[0]#)"]
    )
    test_apply_and_recompile(
        root / "tests/fail/call_arg_unexpected_mutable_sigil.tk",
        "E04635", 1,
        ["read_counter(c)", "read_counter(container.item)", "read_i32(values[0])"]
    )

    explanation = json.loads(run(
        [tokac, "--explain=json", "E0438"]
    ).stdout)
    require(explanation["schema"] == "toka.diagnostic-explanation" and
            explanation["id"] == "ERR_USE_MOVED" and explanation["guidance"],
            "diagnostic explanation is incomplete")
    run([tokac, "--explain", "NOT_A_CODE"], expected=1)

    toka_explanation = json.loads(run(
        [toka, "explain", "E0438", "--json"]
    ).stdout)
    require(toka_explanation == explanation,
            "toka explain does not preserve the compiler explanation")
    toka_check = json.loads(run(
        [toka, "check", "--json", moved], expected=1
    ).stdout)
    require(toka_check["schema"] == "toka.diagnostics",
            "toka check --json did not emit structured diagnostics")

    semantic_source = root / "tests/tooling/semantic_workspace/main.tk"
    context_command = [
        toka, "context", semantic_source,
        "--query-file", semantic_source,
        "--line", "13", "--character", "12",
    ]
    first_context = run(context_command).stdout
    second_context = run(context_command).stdout
    require(first_context == second_context,
            "semantic context output is not deterministic")
    context = json.loads(first_context)
    result = context["result"]
    require(context["schema"] == "toka.semantic-query" and
            context["query"] == "context" and
            result["symbol"]["name"] == "identity" and
            len(result["visibleSymbols"]) <= 20 and
            isinstance(result["truncated"], bool),
            "bounded semantic context is incomplete")

    contract_source = root / "tests/conformance/async/async_cede_unique_parameter_independent_payload_root.tk"
    first_index = run([tokac, "--semantic-index=json", contract_source]).stdout
    second_index = run([tokac, "--semantic-index=json", contract_source]).stdout
    require(first_index == second_index,
            "semantic index API contracts are not deterministic")
    index = json.loads(first_index)
    mutate = next(symbol for symbol in index["symbols"]
                  if symbol["name"] == "mutate" and symbol["kind"] == "function")
    callable_contract = mutate["contract"]
    parameter = callable_contract["parameters"][0]
    require(callable_contract["kind"] == "callable" and
            callable_contract["effect"] == "async" and
            callable_contract["return"] == {"dependencies": [], "type": "i32"} and
            parameter["name"] == "p" and parameter["type"] == "^Cell#" and
            parameter["morphology"] == "unique" and parameter["flow"] == "cede" and
            parameter["payloadWritable"] and not parameter["handleRebindable"],
            "callable ownership and permission contract is incomplete")
    value = next(symbol for symbol in index["symbols"]
                 if symbol["name"] == "value" and symbol["kind"] == "field")
    field_contract = value["contract"]
    require(field_contract["kind"] == "field" and
            field_contract["morphology"] == "value" and
            field_contract["flow"] == "value" and
            not field_contract["payloadWritable"],
            "field ownership and permission contract is incomplete")
    manager_index = json.loads(run(
        [toka, "index", "--json", contract_source]
    ).stdout)
    require(manager_index == index,
            "toka index does not preserve the compiler API contract index")
    direct_references = json.loads(run([
        tokac, "--semantic-query", "references", semantic_source,
        "--query-file", semantic_source, "--line", "13", "--character", "12",
    ]).stdout)
    manager_references = json.loads(run([
        toka, "query", "references", semantic_source,
        "--query-file", semantic_source, "--line", "13", "--character", "12",
        "--json",
    ]).stdout)
    require(manager_references == direct_references and
            manager_references["query"] == "references" and
            len(manager_references["result"]) >= 2,
            "toka query does not preserve semantic impact references")

    csv_source = root / "lib/stdx/data/csv.tk"
    first_csv_index = run([toka, "index", "--json", csv_source]).stdout
    second_csv_index = run([toka, "index", "--json", csv_source]).stdout
    require(first_csv_index == second_csv_index,
            "CSV public API index is not deterministic")
    csv_symbols = json.loads(first_csv_index)["symbols"]
    csv_functions = {
        symbol["name"]: symbol for symbol in csv_symbols
        if symbol["kind"] == "function" and symbol["name"] in {
            "parse_records", "read_record", "write_record"
        }
    }
    require(set(csv_functions) == {"parse_records", "read_record", "write_record"},
            "CSV public API declarations are incomplete")
    parse_contract = csv_functions["parse_records"]["contract"]
    read_contract = csv_functions["read_record"]["contract"]
    write_contract = csv_functions["write_record"]["contract"]
    require(parse_contract["effect"] == "sync" and
            parse_contract["parameters"][0]["type"] == "str" and
            not parse_contract["parameters"][0]["payloadWritable"],
            "CSV parser ownership contract is incomplete")
    require(read_contract["return"]["type"] == "Result<Option<Vec<string>>,CsvError>" and
            read_contract["parameters"][0]["type"] == "BufferedReader<'R>" and
            read_contract["parameters"][0]["payloadWritable"] and
            read_contract["parameters"][1]["type"] == "usize",
            "CSV reader capability contract is incomplete")
    require(write_contract["parameters"][0]["type"] == "BufferedWriter<'W>" and
            write_contract["parameters"][0]["payloadWritable"] and
            write_contract["parameters"][1]["type"] == "Vec<string>" and
            write_contract["parameters"][1]["flow"] == "cede",
            "CSV writer transfer contract is incomplete")

    with tempfile.TemporaryDirectory(prefix="toka-punning-replay-") as temp_dir:
        temp_path = Path(temp_dir)
        lib_source = temp_path / "replay_lib.tk"
        lib_source.write_text(
            "pub shape ReplayPoint(x: i32, y: i32)\n"
            "pub shape ReplayConfig(x: i32, y: i32 = 99)\n\n"
            "pub fn make_point(x: i32, y: i32) -> ReplayPoint {\n"
            "    return ReplayPoint(x, y)\n"
            "}\n\n"
            "pub fn make_mixed_point(x: i32) -> ReplayPoint {\n"
            "    return ReplayPoint(x, y = 77)\n"
            "}\n\n"
            "pub fn make_config(x: i32) -> ReplayConfig {\n"
            "    return ReplayConfig(x, ..)\n"
            "}\n",
            encoding="utf-8"
        )
        lib_obj = temp_path / "replay_lib.o"
        lib_tki = temp_path / "replay_lib.tki"
        run([tokac, "-c", lib_source, "-o", lib_obj], cwd=temp_path)
        require(lib_obj.is_file() and lib_tki.is_file(), "TKI/Obj compilation failed for replay_lib")
        tki_content = lib_tki.read_text(encoding="utf-8")
        require("pub shape ReplayPoint(x: i32, y: i32)" in tki_content, "ReplayPoint missing from TKI")

        # Remove source .tk file to guarantee pure source-less replay
        lib_source.unlink()
        require(not lib_source.exists(), "Source was not successfully removed for source-less test")

        consumer_source = temp_path / "consumer.tk"
        consumer_source.write_text(
            "import ./replay_lib::*\n\n"
            "fn main() -> i32 {\n"
            "    auto p1 = make_point(10, 20)\n"
            "    if p1.x != 10 || p1.y != 20 { return 1 }\n"
            "    auto p2 = make_mixed_point(15)\n"
            "    if p2.x != 15 || p2.y != 77 { return 2 }\n"
            "    auto cfg = make_config(42)\n"
            "    if cfg.x != 42 || cfg.y != 99 { return 3 }\n"
            "    return 0\n"
            "}\n",
            encoding="utf-8"
        )
        consumer_exe = temp_path / ("consumer" + suffix)
        run([tokac, consumer_source, lib_obj, "-o", consumer_exe], cwd=temp_path)
        run([consumer_exe], cwd=temp_path)

    checks = [
        "diagnostic-schema", "multi-span", "machine-fix", "fix-application",
        "compiler-explain", "unknown-code", "toka-explain", "toka-check",
        "semantic-context", "context-determinism", "context-bound",
        "api-contracts", "contract-determinism", "toka-index", "toka-query",
        "csv-api-contracts", "shape-punning-sourceless-replay",
    ]
    print(json.dumps({
        "checks": checks,
        "count": len(checks),
        "result": "pass",
        "schema": "toka.ai-tooling-test",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
