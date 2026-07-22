#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command, cwd, env=None, expected=0):
    result = subprocess.run(
        [str(part) for part in command],
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != expected:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(
            "expected exit %d, got %d: %s"
            % (expected, result.returncode, " ".join(str(part) for part in command))
        )
    return result


def require(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = (root / args.build_dir).resolve()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = build_dir / "bin" / ("tokac" + suffix)
    toka = build_dir / "bin" / ("toka" + suffix)
    tokafmt = build_dir / "bin" / ("tokafmt" + suffix)
    checks = []

    require(tokac.is_file() and toka.is_file() and tokafmt.is_file(), "SDK binaries are missing")
    require("Usage: tokac" in run([tokac, "--help"], root).stdout, "tokac help is incomplete")
    run([tokac, "--not-a-real-option"], root, expected=1)
    run([tokac], root, expected=1)
    checks.extend(("tokac-help", "tokac-unknown-option", "tokac-no-input"))

    diagnostic = run(
        [tokac, "--check-json", root / "tests/fail/borrow_move.tk"],
        root,
        expected=1,
    )
    records = [json.loads(line) for line in diagnostic.stdout.splitlines() if line.strip()]
    require(records and records[0].get("code"), "tokac JSON diagnostics are empty")
    required_fields = {"file", "line", "col", "message", "code", "level", "compiler_version"}
    require(required_fields.issubset(records[0]), "tokac JSON diagnostic schema is incomplete")
    checks.append("tokac-json-diagnostics")

    require("Usage: toka" in run([toka, "--help"], root).stdout, "toka help is incomplete")
    run([toka, "--not-a-real-command"], root, expected=1)
    require("tokafmt version" in run([tokafmt, "--version"], root).stdout, "tokafmt version is missing")
    run([tokafmt, "--not-a-real-option"], root, expected=1)
    checks.extend(("toka-help", "toka-unknown-command", "tokafmt-cli"))

    with tempfile.TemporaryDirectory(prefix="toka-developer-experience-") as temp:
        temp_root = Path(temp)
        source_dir = temp_root / "project" / "src" / "nested"
        source_dir.mkdir(parents=True)
        source = source_dir / "main.tk"
        source.write_text("fn main()->i32{return 0}\n", encoding="utf-8")
        run([tokafmt, "--check", temp_root / "project"], root, expected=1)
        run([tokafmt, "--write", temp_root / "project"], root)
        first = source.read_bytes()
        run([tokafmt, "--check", temp_root / "project"], root)
        run([tokafmt, "--write", temp_root / "project"], root)
        require(first == source.read_bytes(), "tokafmt is not idempotent")
        checks.append("tokafmt-project-idempotence")

        prefix = temp_root / "sdk"
        run(["cmake", "--install", build_dir, "--prefix", prefix], root)
        installed_bin = prefix / "bin"
        installed_toka = installed_bin / ("toka" + suffix)
        installed_tokac = installed_bin / ("tokac" + suffix)
        env = os.environ.copy()
        env["PATH"] = str(installed_bin) + os.pathsep + env.get("PATH", "")
        run([installed_toka, "doctor"], temp_root, env=env)

        direct = temp_root / "direct.tk"
        direct.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
        executable = temp_root / ("direct" + suffix)
        run([installed_tokac, direct, "-o", executable], temp_root, env=env)
        run([executable], temp_root, env=env)
        run([installed_toka, "new", "smoke"], temp_root, env=env)
        output = run([installed_toka, "run"], temp_root / "smoke", env=env)
        require("Hello, Toka!" in output.stdout, "installed toka project did not run")
        checks.extend(("cmake-install", "toka-doctor", "installed-compile-run", "installed-new-run"))

    print(json.dumps({
        "checks": checks,
        "count": len(checks),
        "result": "pass",
        "schema": "toka.developer-experience",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
