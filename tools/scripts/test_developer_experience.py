#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import re
import shutil
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
        encoding="utf-8",
        errors="replace",
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


def release_version(output, tool):
    match = re.search(r"\bversion ([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?)\b", output)
    require(match is not None, tool + " did not report a release version")
    return match.group(1)


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
    tokalsp = build_dir / "bin" / ("tokalsp" + suffix)
    checks = []

    require(tokac.is_file() and toka.is_file() and tokafmt.is_file() and
            tokalsp.is_file(), "SDK binaries are missing")
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

    sdk_build_check = run(
        [tokac, "--check-only", "-I", root / "lib", root / "lib/build.tk"], root,
    )
    require("W0408" not in sdk_build_check.stdout + sdk_build_check.stderr,
            "SDK build module emits mutable-call warning noise")
    user_warning = run(
        [tokac, "--check-only", "-I", root / "lib",
         root / "tests/warn/call_arg_missing_mutable_sigil.tk"], root,
    )
    require("W0408" in user_warning.stdout + user_warning.stderr,
            "user-source mutable-call warning disappeared")
    checks.extend(("sdk-build-warning-clean", "user-w0408-visible"))

    tokac_version = release_version(run([tokac, "--version"], root).stdout, "tokac")
    toka_help = run([toka, "--help"], root).stdout
    require("Usage: toka" in toka_help, "toka help is incomplete")
    require("add <package-or-url>" in toka_help, "toka help hides Registry package names")
    add_help = run([toka, "add", "--help"], root).stdout
    require("Usage: toka add <package-or-url>" in add_help and
            "toka add tokakv" in add_help,
            "toka add help does not explain the Registry workflow")
    require("Toka " + tokac_version in toka_help,
            "toka help version does not agree with tokac")
    toka_version = release_version(run([toka, "--version"], root).stdout, "toka")
    run([toka, "--not-a-real-command"], root, expected=1)
    tokafmt_version = release_version(run([tokafmt, "--version"], root).stdout, "tokafmt")
    require("tokafmt version" in run([tokafmt, "--version"], root).stdout, "tokafmt version is missing")
    run([tokafmt, "--not-a-real-option"], root, expected=1)
    require("Usage: tokalsp" in run([tokalsp, "--help"], root).stdout,
            "tokalsp help is incomplete")
    tokalsp_version = release_version(run([tokalsp, "--version"], root).stdout, "tokalsp")
    require({tokac_version, toka_version, tokafmt_version, tokalsp_version} == {tokac_version},
            "SDK binaries do not agree on the release version")
    run([tokalsp, "--not-a-real-option"], root, expected=1)
    checks.extend(("toka-help", "toka-add-help", "toka-unknown-command", "tokafmt-cli",
                   "tokalsp-cli", "sdk-version-agreement"))

    with tempfile.TemporaryDirectory(prefix="toka-developer-experience-") as temp:
        temp_root = Path(temp)
        preview = run([toka, "test"], temp_root)
        require("Preview:" in preview.stdout and "not the stable project test contract" in preview.stdout,
                "toka test is not clearly marked Preview")
        failed_test_root = temp_root / "failed_preview_test"
        (failed_test_root / "tests").mkdir(parents=True)
        (failed_test_root / "tests" / "compile_error.tk").write_text(
            "fn main() -> i32 {\n"
            "    auto value = 1\n"
            "    auto ^moved = ^value\n"
            "    return value\n"
            "}\n", encoding="utf-8")
        failed_preview = run([toka, "test"], failed_test_root, expected=1)
        require("[FAILED (Compile)]" in failed_preview.stdout,
                "toka test did not report the preview fixture failure")
        checks.extend(("toka-test-preview", "toka-test-preview-failure-exit"))

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

        source_workspace = temp_root / "source-workspace"
        tokafmt_source = source_workspace / "tools/tokafmt"
        tokafmt_source.mkdir(parents=True)
        (tokafmt_source / "Project.tk").write_bytes(
            (root / "tools/tokafmt/Project.tk").read_bytes()
        )
        shutil.copytree(root / "tools/tokafmt/src", tokafmt_source / "src")
        shutil.copytree(build_dir / "generated",
                        source_workspace / "build/generated")
        source_build_env = os.environ.copy()
        source_build_env["PATH"] = str(build_dir / "bin") + os.pathsep + source_build_env.get("PATH", "")
        source_build_env["TOKA_LIB"] = str(root / "lib")
        run([toka, "build"], tokafmt_source, env=source_build_env)
        require((tokafmt_source / ("target/debug/tokafmt" + suffix)).is_file(),
                "tokafmt Project.tk source build did not produce its executable")
        checks.extend(("tokafmt-project-idempotence", "tokafmt-project-source-build"))

        prefix = temp_root / "sdk"
        run(["cmake", "--install", build_dir, "--prefix", prefix], root)
        installed_bin = prefix / "bin"
        installed_toka = installed_bin / ("toka" + suffix)
        installed_tokac = installed_bin / ("tokac" + suffix)
        env = os.environ.copy()
        env["PATH"] = str(installed_bin) + os.pathsep + env.get("PATH", "")
        # Exercise the installed SDK's own prefix discovery even when the
        # caller uses TOKA_LIB to build the source-tree toolchain.
        env.pop("TOKA_LIB", None)
        run([installed_toka, "doctor"], temp_root, env=env)
        # Project-aware semantic commands currently target published SDK hosts.
        if sys.platform != "win32":
            incompatible_python = temp_root / "incompatible-python"
            incompatible_python.mkdir()
            fake_python = incompatible_python / "python3"
            fake_python.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            fake_python.chmod(0o755)
            bad_python_env = env.copy()
            bad_python_env["PATH"] = (
                str(incompatible_python) + os.pathsep + bad_python_env.get("PATH", "")
            )
            failed_doctor = run(
                [installed_toka, "doctor"], temp_root,
                env=bad_python_env, expected=1,
            )
            require("Python 3.10+" in failed_doctor.stdout,
                    "toka doctor did not reject an incompatible Python helper")
            checks.append("doctor-python-runtime-contract")
            missing_python_project = temp_root / "missing_python_project"
            run([installed_toka, "new", missing_python_project], temp_root, env=env)
            failed_add = run(
                [installed_toka, "add", "missing-python-probe"],
                missing_python_project, env=bad_python_env, expected=1,
            )
            require("Python 3 package helper" in failed_add.stdout and
                    "toka doctor" in failed_add.stdout,
                    "toka add hid the package-helper launch failure")
            checks.append("package-helper-launch-diagnostic")

        direct = temp_root / "direct.tk"
        direct.write_text("fn main() -> i32 { return 0 }\n", encoding="utf-8")
        executable = temp_root / ("direct" + suffix)
        run([installed_tokac, direct, "-o", executable], temp_root, env=env)
        run([executable], temp_root, env=env)

        debug_source = temp_root / "debug.tk"
        debug_source.write_text(
            "fn bump(x: i32) -> i32 {\n"
            "    auto y = x + 1\n"
            "    return y\n"
            "}\n"
            "fn main() -> i32 { return bump(1) - 2 }\n",
            encoding="utf-8",
        )
        debug_ir = temp_root / "debug.ll"
        run([installed_tokac, "-g", "--emit-llvm", debug_source, "-o", debug_ir],
            temp_root, env=env)
        ir = debug_ir.read_text(encoding="utf-8")
        require("DICompileUnit" in ir and "DISubprogram" in ir and
                "DILocalVariable" in ir,
                "tokac -g did not emit function, line, and local-variable metadata")
        run([installed_toka, "new", "smoke"], temp_root, env=env)
        output = run([installed_toka, "run"], temp_root / "smoke", env=env)
        require("Hello, Toka!" in output.stdout, "installed toka project did not run")

        # Mirror the release archive layout and invoke the manager by PATH name,
        # so argv[0] is `toka` rather than an absolute path. This is the normal
        # relocatable CLI path and must not require an explicit TOKA_LIB.
        relocated_sdk = temp_root / "relocated-sdk"
        shutil.copytree(installed_bin, relocated_sdk / "bin")
        shutil.copytree(prefix / "share/toka/lib", relocated_sdk / "lib")
        relocated_env = env.copy()
        relocated_env["PATH"] = (
            str(relocated_sdk / "bin") + os.pathsep + env.get("PATH", "")
        )
        relocated_env.pop("TOKA_LIB", None)
        relocated_manager = "toka" + suffix
        run([relocated_manager, "doctor"], temp_root, env=relocated_env)
        relocated_project = temp_root / "relocated-smoke"
        run([relocated_manager, "new", relocated_project], temp_root, env=relocated_env)
        relocated_output = run(
            [relocated_manager, "run"], relocated_project, env=relocated_env,
        )
        require("Hello, Toka!" in relocated_output.stdout,
                "PATH-invoked relocated SDK required an explicit TOKA_LIB")
        checks.append("relocatable-path-invocation")

        dependency = temp_root / "dependency"
        dependency_module = dependency / "lib" / "dependency" / "mod.tk"
        dependency_module.parent.mkdir(parents=True)
        (dependency / "package.tk").write_text(
            'pub const PACKAGE = (name = "dependency", version = "1.0.0", dependencies = ())\n',
            encoding="utf-8",
        )
        dependency_module.write_text(
            "pub fn value() -> i32 { return 1 }\n", encoding="utf-8"
        )
        smoke = temp_root / "smoke"
        run([installed_toka, "add", dependency], smoke, env=env)
        offline_env = env.copy()
        offline_env["TOKA_OFFLINE"] = "1"
        run([installed_toka, "fetch"], smoke, env=offline_env)
        if sys.platform != "win32":
            smoke_source = smoke / "src/main.tk"
            smoke_source.write_text(
                "import dependency::{value}\n"
                "fn main() -> i32 { return value() - 1 }\n",
                encoding="utf-8",
            )
            project_check = run(
                [installed_toka, "check", "--json", "src/main.tk"], smoke, env=env,
            )
            project_check_doc = json.loads(project_check.stdout)
            require(project_check_doc.get("schema") == "toka.diagnostics" and
                    project_check_doc.get("success") is True,
                    "toka check did not resolve the locked project dependency")
            project_evidence = run(
                [installed_toka, "evidence", "--json", "src/main.tk"], smoke, env=env,
            )
            project_evidence_doc = json.loads(project_evidence.stdout)
            require(project_evidence_doc.get("schema") == "toka.semantic-evidence",
                    "toka evidence did not emit JSON for a locked project dependency")
            checks.extend(("project-aware-check", "project-aware-evidence"))

        absolute_project = temp_root / "absolute_smoke"
        run([installed_toka, "new", absolute_project], temp_root, env=env)
        output = run([installed_toka, "run"], absolute_project, env=env)
        require("Hello, Toka!" in output.stdout,
                "installed toka absolute-path project did not run")
        checks.extend(("cmake-install", "toka-doctor", "installed-compile-run",
                       "tokac-dwarf-metadata", "installed-new-run",
                       "installed-package-helper-discovery",
                       "installed-new-absolute-path-run"))

    print(json.dumps({
        "checks": checks,
        "count": len(checks),
        "result": "pass",
        "schema": "toka.developer-experience",
        "version": 1,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
