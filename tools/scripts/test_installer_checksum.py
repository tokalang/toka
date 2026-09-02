#!/usr/bin/env python3

"""Exercise install.sh checksum verification without using the network."""

import hashlib
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools/install.sh"
VERSION = "v1.0.0-rc.checksum-test"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def target_name():
    system = platform.system()
    machine = platform.machine()
    os_name = {"Darwin": "macos", "Linux": "linux"}.get(system)
    arch_name = {
        "x86_64": "x64",
        "aarch64": "arm64",
        "arm64": "arm64",
    }.get(machine)
    require(os_name is not None and arch_name is not None,
            "installer checksum test does not support this host")
    return os_name, arch_name


def make_archive(root, tarball, inner):
    payload = root / inner
    (payload / "bin").mkdir(parents=True)
    (payload / "lib").mkdir()
    for tool in ("tokac", "toka", "tokafmt", "tokalsp"):
        (payload / "bin" / tool).write_text(tool + "\n", encoding="utf-8")
    (payload / "lib" / "marker.tk").write_text("// test\n", encoding="utf-8")
    with tarfile.open(tarball, "w:gz") as archive:
        archive.add(payload, arcname=inner)


def run_installer(home, fake_bin, archive, sums):
    env = os.environ.copy()
    env.update({
        "HOME": str(home),
        "PATH": str(fake_bin) + os.pathsep + env["PATH"],
        "SHELL": "/bin/sh",
        "TEST_ARCHIVE": str(archive),
        "TEST_SUMS": str(sums),
    })
    return subprocess.run(
        ["sh", str(INSTALLER), VERSION],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def main():
    os_name, arch_name = target_name()
    filename = "toka-%s-%s-%s.tar.gz" % (VERSION, os_name, arch_name)
    inner = filename[:-7]

    with tempfile.TemporaryDirectory(prefix="toka-installer-checksum-") as temp:
        temp_root = Path(temp)
        archive = temp_root / filename
        sums = temp_root / "SHA256SUMS"
        fake_bin = temp_root / "bin"
        fake_bin.mkdir()
        fake_curl = fake_bin / "curl"
        fake_curl.write_text(
            "#!/bin/sh\n"
            "output=\n"
            "want_output=0\n"
            "for arg in \"$@\"; do\n"
            "  if [ \"$want_output\" = 1 ]; then output=$arg; want_output=0; continue; fi\n"
            "  if [ \"$arg\" = -o ]; then want_output=1; fi\n"
            "done\n"
            "case \"$output\" in\n"
            "  */SHA256SUMS) cp \"$TEST_SUMS\" \"$output\" ;;\n"
            "  *) cp \"$TEST_ARCHIVE\" \"$output\" ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        fake_curl.chmod(0o755)
        make_archive(temp_root, archive, inner)

        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        sums.write_text("%s  %s\n" % (digest, filename), encoding="utf-8")
        success_home = temp_root / "success-home"
        success_home.mkdir()
        success = run_installer(success_home, fake_bin, archive, sums)
        require(success.returncode == 0,
                "installer rejected a matching checksum:\n" + success.stdout + success.stderr)
        require((success_home / ".toka/bin/toka").is_file(),
                "verified archive was not activated")

        mismatch_home = temp_root / "mismatch-home"
        (mismatch_home / ".toka").mkdir(parents=True)
        sentinel = mismatch_home / ".toka/sentinel"
        sentinel.write_text("preserve\n", encoding="utf-8")
        sums.write_text("%s  %s\n" % ("0" * 64, filename), encoding="utf-8")
        mismatch = run_installer(mismatch_home, fake_bin, archive, sums)
        require(mismatch.returncode != 0 and "SHA-256 mismatch" in mismatch.stdout,
                "installer did not fail clearly on a checksum mismatch")
        require(sentinel.read_text(encoding="utf-8") == "preserve\n" and
                not (mismatch_home / ".toka/bin/toka").exists(),
                "checksum failure modified the existing installation")

    print("Installer checksum tests PASSED")


if __name__ == "__main__":
    main()
