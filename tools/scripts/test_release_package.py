#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tarfile


def run(command, cwd, env):
    result = subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("command failed: " + " ".join(command))
    return result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("--version")
    args = parser.parse_args()

    archive = Path(args.archive).resolve()
    if not archive.is_file():
        raise SystemExit("release archive not found: %s" % archive)

    checks = []
    with tempfile.TemporaryDirectory(prefix="toka-package-smoke-") as temp:
        root = Path(temp)
        with tarfile.open(str(archive), "r:gz") as package:
            package.extractall(str(root))
        entries = sorted(path for path in root.iterdir() if path.is_dir())
        if len(entries) != 1:
            raise SystemExit("archive must contain exactly one package root")
        package_root = entries[0]
        generated_python = list(package_root.rglob("__pycache__")) + list(package_root.rglob("*.pyc"))
        if generated_python:
            raise SystemExit("release archive contains generated Python cache files")
        checks.append("no-python-cache")
        suffix = ".exe" if sys.platform == "win32" else ""
        required = ("tokac", "toka", "tokafmt", "tokalsp")
        for name in required:
            binary = package_root / "bin" / (name + suffix)
            if not binary.is_file():
                raise SystemExit("missing packaged binary: %s" % binary.name)
            checks.append("binary:" + name)

        for name in ("toka_package.py", "toka_safe_extract.py"):
            helper = package_root / "lib" / "toolchain" / name
            if not helper.is_file():
                raise SystemExit("missing packaged package helper: %s" % name)
            checks.append("package-helper:" + name)

        env = os.environ.copy()
        env["TOKA_LIB"] = str(package_root / "lib")
        env["PATH"] = str(package_root / "bin") + os.pathsep + env.get("PATH", "")
        tokac = package_root / "bin" / ("tokac" + suffix)
        toka = package_root / "bin" / ("toka" + suffix)
        tokac_version = run([str(tokac), "--version"], root, env)
        expected_version = args.version[1:] if args.version and args.version.startswith("v") else args.version
        if expected_version and expected_version not in tokac_version:
            raise SystemExit("packaged tokac version mismatch: " + tokac_version.strip())
        checks.append("tokac-version")
        toka_version = run([str(toka), "--version"], root, env)
        if expected_version and expected_version not in toka_version:
            raise SystemExit("packaged toka version mismatch: " + toka_version.strip())
        checks.append("toka-version")

        direct = root / "direct.tk"
        direct.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
        direct_exe = root / ("direct" + suffix)
        run([str(tokac), str(direct), "-o", str(direct_exe)], root, env)
        run([str(direct_exe)], root, env)
        checks.append("tokac-compile-run")

        run([str(toka), "new", "smoke_app"], root, env)
        output = run([str(toka), "run"], root / "smoke_app", env)
        if "Hello, Toka!" not in output:
            raise SystemExit("packaged toka run did not produce expected output")
        checks.append("toka-new-run")

        dependency = root / "local dependency"
        dependency_module = dependency / "lib" / "dep" / "mod.tk"
        dependency_module.parent.mkdir(parents=True)
        (dependency / "package.tk").write_text(
            'pub const PACKAGE = (name = "dep", version = "1.0.0", dependencies = ())\n',
            encoding="utf-8",
        )
        dependency_module.write_text("pub fn value() -> i32 { return 1 }\n", encoding="utf-8")
        package_project = root / "package-project"
        package_project.mkdir()
        (package_project / "package.tk").write_text(
            'pub const PACKAGE = (name = "root", version = "1.0.0", dependencies = (dep = %s,))\n'
            % json.dumps(str(dependency)),
            encoding="utf-8",
        )
        run([str(toka), "fetch"], package_project, env)
        lock = package_project / "package.lock"
        if not lock.read_text(encoding="utf-8").startswith("toka-lock-v1\n"):
            raise SystemExit("packaged toka did not create a v1 lock")
        offline_env = env.copy()
        offline_env["TOKA_OFFLINE"] = "1"
        run([str(toka), "fetch"], package_project, offline_env)
        run([str(toka), "rm", "dep"], package_project, env)
        checks.append("toka-package-lock-offline-remove")

    print(json.dumps({
        "checks": checks,
        "count": len(checks),
        "result": "pass",
        "schema": "toka.release-package-smoke",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
