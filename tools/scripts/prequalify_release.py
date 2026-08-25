#!/usr/bin/env python3

"""Run the release gate locally from an isolated committed checkout.

This is a prequalification aid, not a replacement for the four GitHub-hosted
qualification rows. It prevents local work-in-progress, stale build products,
and a missing release-version override from masking an RC failure.
"""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "toka.local-release-prequalification"
SCHEMA_VERSION = 1
SUPPORTED_TARGETS = ("native", "linux-arm64", "linux-x64")


class PrequalificationError(RuntimeError):
    pass


def command_text(command):
    return " ".join(subprocess.list2cmdline([part]) for part in command)


def run(command, cwd=None, env=None, dry_run=False):
    print("+ " + command_text(command))
    if dry_run:
        return 0
    result = subprocess.run(command, cwd=str(cwd) if cwd else None, env=env)
    return result.returncode


def checked_output(command, cwd=None):
    result = subprocess.run(
        command, cwd=str(cwd) if cwd else None, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise PrequalificationError(
            "command failed: %s\n%s%s" %
            (command_text(command), result.stdout, result.stderr)
        )
    return result.stdout.strip()


def native_target():
    if sys.platform == "darwin":
        os_name = "macos"
    elif sys.platform.startswith("linux"):
        os_name = "linux"
    else:
        raise PrequalificationError(
            "native prequalification supports macOS and Linux only; "
            "use the Windows dogfood workflow separately"
        )
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        arch = "arm64"
    elif machine in ("x86_64", "amd64"):
        arch = "x64"
    else:
        raise PrequalificationError("unsupported native architecture: " + machine)
    return os_name + "-" + arch


def require_program(name, advice):
    if shutil.which(name) is None:
        raise PrequalificationError("missing %s; %s" % (name, advice))


def macos_environment():
    require_program("brew", "install Homebrew and LLVM 20 prerequisites first")
    prefixes = {}
    for package in ("llvm@20", "lld@20", "zstd", "openssl@3"):
        try:
            prefixes[package] = checked_output(["brew", "--prefix", package])
        except PrequalificationError:
            raise PrequalificationError(
                "missing Homebrew prerequisite %s; run: brew install llvm@20 lld@20 zstd openssl@3" %
                package
            )
    env = os.environ.copy()
    env["PATH"] = prefixes["llvm@20"] + "/bin:" + env.get("PATH", "")
    env["LLVM_DIR"] = prefixes["llvm@20"] + "/lib/cmake/llvm"
    env["LLD_DIR"] = prefixes["lld@20"] + "/lib/cmake/lld"
    env["ZSTD_LIBRARY"] = prefixes["zstd"] + "/lib/libzstd.dylib"
    env["OPENSSL_ROOT_DIR"] = prefixes["openssl@3"]
    return env


def configured_environment(target):
    for program in ("cmake", "ninja", "python3"):
        require_program(program, "install the local release prerequisites first")
    if target.startswith("macos-"):
        return macos_environment()
    return os.environ.copy()


def configure_command(source, build, version, env):
    command = [
        "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DTOKA_RELEASE_VERSION_OVERRIDE=" + version.lstrip("v"),
    ]
    for key in ("LLVM_DIR", "LLD_DIR", "ZSTD_LIBRARY", "OPENSSL_ROOT_DIR"):
        value = env.get(key)
        if value:
            command.append("-D%s=%s" % (key, value))
    return command


def read_gate_report(path, target, revision, version):
    if not path.is_file():
        raise PrequalificationError("release gate did not write its report: " + str(path))
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise PrequalificationError("invalid release-gate report: %s" % error)
    errors = []
    if report.get("schema") != "toka.release-gate" or report.get("version") != 2:
        errors.append("unexpected gate schema")
    if report.get("target") != target:
        errors.append("target is %r" % report.get("target"))
    if report.get("revision") != revision:
        errors.append("revision is %r" % report.get("revision"))
    if report.get("version_label") != version:
        errors.append("version label is %r" % report.get("version_label"))
    if report.get("source_dirty") is not False:
        errors.append("source_dirty is not false")
    if errors:
        raise PrequalificationError("invalid release-gate evidence: " + "; ".join(errors))
    return report


def clone_revision(revision, destination, dry_run):
    clone = ["git", "clone", "--no-checkout", "--no-local", str(ROOT), str(destination)]
    if run(clone, dry_run=dry_run) != 0:
        raise PrequalificationError("could not create isolated local checkout")
    if run(["git", "checkout", "--detach", revision], cwd=destination, dry_run=dry_run) != 0:
        raise PrequalificationError("could not check out candidate revision " + revision)


def run_native(source, output, target, revision, version, dry_run):
    env = os.environ.copy() if dry_run else configured_environment(target)
    build = source / "build"
    if run(configure_command(source, build, version, env), env=env, dry_run=dry_run) != 0:
        raise PrequalificationError("native CMake configuration failed")
    report = output / ("release-gate-%s.json" % target)
    work = output / ("work-%s" % target)
    gate = [
        sys.executable, "tools/scripts/release_gate.py", "--target", target,
        "--version", version, "--build-dir", str(build),
        "--work-dir", str(work), "--output", str(report),
    ]
    exit_code = run(gate, cwd=source, env=env, dry_run=dry_run)
    if dry_run:
        return {"executor": "native", "result": "planned", "target": target}
    gate_report = read_gate_report(report, target, revision, version)
    return {
        "executor": "native",
        "exit_code": exit_code,
        "gate_result": gate_report.get("result"),
        "report": report.name,
        "result": "pass" if exit_code == 0 and gate_report.get("result") == "pass" else "fail",
        "target": target,
    }


def docker_image(target):
    return "toka-release-qualification:%s" % target


def run_docker(source, output, target, revision, version, docker_cores, dry_run):
    if not dry_run:
        require_program("docker", "install Docker Desktop or select --target native")
    arch = target.split("-", 1)[1]
    platform_name = "linux/%s" % ("arm64" if arch == "arm64" else "amd64")
    base = "ubuntu:24.04" if arch == "arm64" else "ubuntu:22.04"
    image = docker_image(target)
    dockerfile = source / "tools/docker/Dockerfile.release-qualification"
    build_image = [
        "docker", "build", "--quiet", "--platform", platform_name,
        "--build-arg", "BASE_IMAGE=" + base, "--tag", image,
        "--file", str(dockerfile), str(dockerfile.parent),
    ]
    if run(build_image, dry_run=dry_run) != 0:
        raise PrequalificationError("could not build local Linux qualification image")
    report = output / ("release-gate-%s.json" % target)
    work = output / ("work-%s" % target)
    build = "/src/build"
    gate_command = " ".join([
        "git config --global --add safe.directory /src",
        "&&",
        "cmake -S /src -B " + build + " -G Ninja -DCMAKE_BUILD_TYPE=Release",
        "-DTOKA_RELEASE_VERSION_OVERRIDE=" + version.lstrip("v"),
        "&&",
        "python3 tools/scripts/release_gate.py --target " + target,
        "--version " + version,
        "--build-dir " + build,
        "--work-dir /out/work-" + target,
        "--output /out/" + report.name,
    ])
    run_gate = [
        "docker", "run", "--rm", "--platform", platform_name,
        "--env", "CORES=%d" % docker_cores,
        "--volume", "%s:/src" % source,
        "--volume", "%s:/out" % output,
        "--workdir", "/src", image, "bash", "-lc", gate_command,
    ]
    exit_code = run(run_gate, dry_run=dry_run)
    if dry_run:
        return {"executor": "docker", "result": "planned", "target": target}
    gate_report = read_gate_report(report, target, revision, version)
    return {
        "executor": "docker",
        "exit_code": exit_code,
        "gate_result": gate_report.get("result"),
        "report": report.name,
        "result": "pass" if exit_code == 0 and gate_report.get("result") == "pass" else "fail",
        "target": target,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Run isolated local release prequalification before GitHub Actions."
    )
    parser.add_argument("--revision", default="HEAD",
                        help="committed candidate revision (default: HEAD)")
    parser.add_argument("--version", default="v1.0.0-rc.8",
                        help="release label beginning with v")
    parser.add_argument("--target", choices=SUPPORTED_TARGETS, action="append",
                        help="native, linux-arm64, or linux-x64; repeatable")
    parser.add_argument("--output-dir", default=str(ROOT / "build/local-release-prequalification"),
                        help="host directory for reports and stage logs")
    parser.add_argument("--docker-cores", type=int, default=2,
                        help="parallel workers for Docker gates (default: 2)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the isolated checkout and gate commands without running them")
    args = parser.parse_args()

    if not args.version.startswith("v"):
        raise SystemExit("release label must begin with v")
    if args.docker_cores <= 0:
        raise SystemExit("--docker-cores must be positive")
    revision = checked_output(["git", "rev-parse", "--verify", args.revision + "^{commit}"], cwd=ROOT)
    targets = args.target or ["native"]
    resolved_targets = []
    for requested in targets:
        target = native_target() if requested == "native" else requested
        if target.startswith("macos-") and target != native_target():
            raise SystemExit(
                "macOS cross-architecture qualification is not supported locally; "
                "use GitHub Actions for " + target
            )
        if target not in resolved_targets:
            resolved_targets.append(target)

    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="toka-release-prequalification-") as temporary:
        for target in resolved_targets:
            source = Path(temporary) / ("source-" + target)
            try:
                clone_revision(revision, source, args.dry_run)
                if target.startswith("linux-") and target != native_target():
                    result = run_docker(source, output, target, revision, args.version,
                                        args.docker_cores, args.dry_run)
                elif target.startswith("linux-"):
                    result = run_native(source, output, target, revision,
                                        args.version, args.dry_run)
                else:
                    result = run_native(source, output, target, revision,
                                        args.version, args.dry_run)
            except PrequalificationError as error:
                result = {"error": str(error), "result": "fail", "target": target}
            results.append(result)

    overall = "planned" if args.dry_run else (
        "pass" if results and all(result["result"] == "pass" for result in results) else "fail"
    )
    summary = {
        "revision": revision,
        "result": overall,
        "schema": SCHEMA,
        "targets": results,
        "version": SCHEMA_VERSION,
        "version_label": args.version,
    }
    summary_path = output / "local-release-prequalification-summary.json"
    summary_path.write_text(json.dumps(summary, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    raise SystemExit(0 if overall in ("pass", "planned") else 1)


if __name__ == "__main__":
    main()
