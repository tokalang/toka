#!/usr/bin/env python3
"""Deterministic sustained and corruption qualification for QSLite."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import random
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "toka.qslite-reference"
SCHEMA_VERSION = 1


def target_name() -> str:
    system = {"Darwin": "macos", "Linux": "linux"}.get(
        platform.system(), platform.system().lower()
    )
    machine = {"x86_64": "x64", "AMD64": "x64", "aarch64": "arm64"}.get(
        platform.machine(), platform.machine().lower()
    )
    return system + "-" + machine


def run(command: list[str], *, cwd: Path = ROOT, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if result.returncode != expected:
        raise RuntimeError(
            "command returned %d, expected %d: %s\nstdout:\n%s\nstderr:\n%s"
            % (result.returncode, expected, command, result.stdout, result.stderr)
        )
    return result


def compile_qslite(tokac: Path, output: Path) -> None:
    run(
        [
            str(tokac),
            "-I", "lib",
            "-I", "examples/qslite/lib",
            "examples/qslite/src/main.tk",
            "-o", str(output),
            "-O2",
        ]
    )


def command(binary: Path, database: Path, *arguments: str, expected: int = 0) -> str:
    result = run([str(binary), str(database), *arguments], expected=expected)
    return result.stdout


def scan(binary: Path, database: Path) -> dict[int, str]:
    output = command(binary, database, "scan")
    rows: dict[int, str] = {}
    for line in output.splitlines():
        key, value = line.split("\t", 1)
        rows[int(key)] = value
    return rows


def sustained_run(binary: Path, directory: Path, seed: int, operation_count: int) -> dict[str, object]:
    directory.mkdir(parents=True)
    database = directory / "state.qslite"
    model: dict[int, str] = {}
    rng = random.Random(seed)
    reopen_count = 0

    for index in range(operation_count):
        key = rng.randrange(64)
        choice = rng.randrange(10)
        if choice < 6:
            value = "value-%04d-%08x" % (index, rng.getrandbits(32))
            if index % 37 == 0:
                value += "-snowman-\u2603"
            command(binary, database, "put", str(key), value)
            model[key] = value
        elif choice < 8:
            existed = key in model
            command(binary, database, "delete", str(key), expected=0 if existed else 3)
            model.pop(key, None)
        else:
            if key in model:
                output = command(binary, database, "get", str(key)).rstrip("\n")
                if output != model[key]:
                    raise AssertionError("get result diverged from reference model")
            else:
                command(binary, database, "get", str(key), expected=3)
        reopen_count += 1
        if index % 25 == 0 and scan(binary, database) != dict(sorted(model.items())):
            raise AssertionError("scan diverged from reference model")
        if index % 25 == 0:
            reopen_count += 1

    final_rows = scan(binary, database)
    reopen_count += 1
    if final_rows != dict(sorted(model.items())):
        raise AssertionError("final scan diverged from reference model")
    if (directory / "state.qslite.tmp").exists():
        raise AssertionError("successful workload left a temporary database")
    payload = database.read_bytes()
    return {
        "database": database,
        "database_sha256": hashlib.sha256(payload).hexdigest(),
        "operation_count": operation_count,
        "reopen_count": reopen_count,
        "row_count": len(model),
    }


def record(key: int, value_hex: str, digest: str | None = None) -> bytes:
    payload = "%d\t%s" % (key, value_hex)
    checksum = digest or hashlib.sha256(payload.encode("ascii")).hexdigest()
    return (payload + "\t" + checksum + "\n").encode("ascii")


def corruption_qualification(binary: Path, valid_database: Path, directory: Path) -> int:
    directory.mkdir()
    valid = valid_database.read_bytes()
    cases = (
        ("missing-header", b"", 2),
        ("unsupported-version", b"QSLITE\t2\n", 2),
        ("truncated-record", b"QSLITE\t1\n1\t61\n", 3),
        ("checksum", b"QSLITE\t1\n" + record(1, "61", "0" * 64), 4),
        ("invalid-hex", b"QSLITE\t1\n" + record(1, "zz"), 3),
        ("invalid-utf8", b"QSLITE\t1\n" + record(1, "ff"), 5),
        ("duplicate-key", b"QSLITE\t1\n" + record(1, "61") + record(1, "62"), 3),
        ("unsorted-key", b"QSLITE\t1\n" + record(2, "62") + record(1, "61"), 3),
        ("extra-field", b"QSLITE\t1\n" + record(1, "61").rstrip(b"\n") + b"\textra\n", 3),
    )
    for name, payload, error_code in cases:
        database = directory / (name + ".qslite")
        database.write_bytes(payload)
        before = database.read_bytes()
        result = run([str(binary), str(database), "scan"], expected=1)
        if "error[%d]" % error_code not in result.stdout:
            raise AssertionError("corruption case %s returned the wrong error: %s" % (name, result.stdout))
        if database.read_bytes() != before:
            raise AssertionError("corruption case mutated its input: " + name)
        if database.with_suffix(database.suffix + ".tmp").exists():
            raise AssertionError("corruption case left a temporary file: " + name)

    preserved = directory / "preserved.qslite"
    preserved.write_bytes(valid)
    before = preserved.read_bytes()
    command(binary, preserved, "get", "18446744073709551616", expected=2)
    if preserved.read_bytes() != before:
        raise AssertionError("invalid CLI input mutated a valid database")
    return len(cases) + 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tokac", type=Path, default=ROOT / "build/bin/tokac")
    parser.add_argument("--seed", type=int, default=100098)
    parser.add_argument("--operations", type=int, default=300)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="qslite-qualification-") as temporary:
        work = Path(temporary)
        binary = work / "qslite"
        compile_qslite(args.tokac.resolve(), binary)
        first = sustained_run(binary, work / "first", args.seed, args.operations)
        second = sustained_run(binary, work / "second", args.seed, args.operations)
        comparable = ("database_sha256", "operation_count", "reopen_count", "row_count")
        if any(first[key] != second[key] for key in comparable):
            raise AssertionError("fixed-seed runs are not deterministic")
        first_bytes = Path(first["database"]).read_bytes()
        second_bytes = Path(second["database"]).read_bytes()
        if first_bytes != second_bytes:
            raise AssertionError("fixed-seed database bytes differ")
        corruptions = corruption_qualification(binary, Path(first["database"]), work / "corruptions")
        revision = run(["git", "rev-parse", "HEAD"]).stdout.strip()
        report = {
            "compiler_revision": revision,
            "corruption_cases": corruptions,
            "database_sha256": first["database_sha256"],
            "operation_count": first["operation_count"],
            "platform": target_name(),
            "reopen_count": first["reopen_count"],
            "result": "pass",
            "row_count": first["row_count"],
            "schema": SCHEMA,
            "seed": args.seed,
            "stages": {
                "compile": "pass",
                "corruption": "pass",
                "deterministic_bytes": "pass",
                "reference_model": "pass",
                "reopen": "pass",
            },
            "version": SCHEMA_VERSION,
        }
        encoded = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
        if args.output:
            args.output.resolve().write_text(encoded, encoding="utf-8")
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
