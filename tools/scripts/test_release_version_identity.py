#!/usr/bin/env python3
"""Check default, overridden and packaged-tool version identities."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def run(command):
    result = subprocess.run(command, cwd=ROOT, env=os.environ.copy(),
                            capture_output=True, text=True, timeout=180)
    if result.returncode != 0:
        raise RuntimeError("command failed: %s\n%s%s" %
                           (" ".join(str(part) for part in command),
                            result.stdout, result.stderr))
    return result.stdout


def require_version(build, version, source_version):
    header = (build / "include/toka/Version.h").read_text(encoding="utf-8")
    module = (build / "generated/toka_build/release_version.tk").read_text(
        encoding="utf-8")
    major, minor, patch = source_version.split(".")
    for line in ('#define TOKA_VERSION_MAJOR ' + major,
                 '#define TOKA_VERSION_MINOR ' + minor,
                 '#define TOKA_VERSION_PATCH ' + patch,
                 '#define TOKA_VERSION_STRING "%s"' % version):
        if line not in header:
            raise RuntimeError("generated compiler header lacks " + line)
    if 'string::from("%s")' % version not in module:
        raise RuntimeError("generated SDK version differs from compiler header")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--version", default="v0.12.0")
    args = parser.parse_args()
    expected = args.version.removeprefix("v")
    source_version = expected.split("-", 1)[0].split("+", 1)[0]
    if not re.fullmatch(r"0\.(?:11|12)\.(0|[1-9][0-9]*)", source_version):
        raise RuntimeError("expected version is outside the supported 0.11.x/0.12.x lines")
    probe_override = source_version + "-dev.19"

    with tempfile.TemporaryDirectory(prefix="toka-release-version-") as temporary:
        build = Path(temporary) / "build"
        base = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release"]
        run(base)
        require_version(build, source_version, source_version)
        run(base + ["-DTOKA_RELEASE_VERSION_OVERRIDE=" + probe_override])
        require_version(build, probe_override, source_version)
        run(base + ["-DTOKA_RELEASE_VERSION_OVERRIDE="])
        require_version(build, source_version, source_version)

    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        binary = args.build_dir.resolve() / "bin" / tool
        output = run([str(binary), "--version"])
        if expected not in output or "1.0.0-rc" in output:
            raise RuntimeError("%s reports the wrong release identity: %s" %
                               (tool, output.strip()))
    print("Release version identity tests PASSED")


if __name__ == "__main__":
    main()
