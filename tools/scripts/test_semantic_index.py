#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = (root / args.build_dir / "bin" / ("tokac" + suffix)).resolve()
    workspace = root / "tests/tooling/semantic_workspace"
    source = workspace / "main.tk"
    library = workspace / "lib.tk"
    require(tokac.is_file(), "tokac binary is missing")

    def run(*options):
        command = [tokac, *options, source]
        result = subprocess.run(
            [str(part) for part in command], cwd=root, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            raise RuntimeError(
                "semantic command failed: %s\n%s%s"
                % (" ".join(str(part) for part in command),
                   result.stdout, result.stderr)
            )
        return result.stdout

    first = run("--semantic-index=json")
    second = run("--semantic-index=json")
    require(first == second, "semantic index output is not deterministic")
    index = json.loads(first)
    require(index["schema"] == "toka.semantic-index" and index["version"] == 1,
            "semantic index schema is not versioned")

    main_path = str(source.resolve())
    library_path = str(library.resolve())
    local_symbols = [
        symbol for symbol in index["symbols"]
        if symbol["declaration"]["file"] in (main_path, library_path)
    ]
    values = [symbol for symbol in local_symbols if symbol["name"] == "value"]
    parameter = next(symbol for symbol in values if symbol["kind"] == "parameter"
                     and symbol["declaration"]["file"] == main_path)
    shadow = next(symbol for symbol in values if symbol["kind"] == "variable")
    require(parameter["id"] != shadow["id"] and parameter["scope"] != shadow["scope"],
            "shadowed declarations do not have distinct semantic identities")

    add = next(symbol for symbol in local_symbols
               if symbol["name"] == "add" and symbol["kind"] == "function")
    add_occurrences = [
        occurrence for occurrence in index["occurrences"]
        if occurrence["symbol"] == add["id"]
    ]
    require(len(add_occurrences) == 3,
            "cross-module function references are incomplete")

    def query(kind, line=0, character=0, extra=()):
        output = run(
            "--semantic-query", kind,
            "--query-file", source,
            "--line", str(line),
            "--character", str(character),
            *extra,
        )
        value = json.loads(output)
        require(value["schema"] == "toka.semantic-query" and value["version"] == 1,
                "semantic query schema is not versioned")
        return value["result"]

    definition = query("definition", 3, 19)
    require(definition["file"] == library_path and
            definition["range"]["start"] == {"line": 6, "character": 7},
            "cross-module definition did not resolve to the declaration")

    references = query("references", 3, 19)
    require(len(references) == 3 and
            {item["location"]["file"] for item in references} ==
            {main_path, library_path},
            "cross-module references did not preserve semantic identity")

    rename = query("rename", 2, 12, ("--rename-to", "input"))
    require(rename["allowed"] and len(rename["edits"]) == 2,
            "safe rename did not target the parameter and its use")
    require(all(edit["location"]["range"]["start"]["line"] != 5
                for edit in rename["edits"]),
            "safe rename incorrectly included the shadowed local")
    conflict = query("rename", 2, 12, ("--rename-to", "result"))
    require(not conflict["allowed"] and not conflict["edits"],
            "rename did not reject a same-scope conflict")

    completion = query("completion", 6, 19)
    details = {item["name"]: item["detail"] for item in completion}
    require("add" in details and "add(value: i32, delta: i32)" in details["add"],
            "completion is missing resolved signature detail")
    require({"value", "result"}.issubset(details),
            "completion is missing visible local symbols")

    document_symbols = query("documentSymbols")
    require({"compute", "main"}.issubset(
                {symbol["name"] for symbol in document_symbols}),
            "document symbol query is incomplete")

    checks = [
        "schema", "determinism", "shadowing", "cross-module-definition",
        "cross-module-references", "safe-rename", "rename-conflict",
        "typed-completion", "document-symbols",
    ]
    print(json.dumps({
        "checks": checks,
        "count": len(checks),
        "result": "pass",
        "schema": "toka.semantic-index-test",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
