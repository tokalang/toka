"""Materialize an immutable registry consumer fixture for direct tokac tests."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys


def materialize_locked_library(root: Path, fixture_name: str, dependency: str,
                               work: Path) -> Path:
    """Fetch one fixture's locked dependency into ``work`` and return its lib."""
    fixture = root / "examples" / fixture_name
    lock = fixture / "package.lock"
    resolved = ""
    for line in lock.read_text(encoding="utf-8").splitlines()[1:]:
        fields = line.split("\t")
        if len(fields) == 8 and fields[0] == "package" and fields[1] == dependency:
            resolved = fields[4]
            break
    if not resolved:
        raise RuntimeError(f"{fixture_name} does not lock {dependency}")

    project = work / fixture_name
    project.mkdir()
    for name in ("package.tk", "package.lock"):
        shutil.copy2(fixture / name, project / name)
    subprocess.run(
        [sys.executable, str(root / "lib" / "toolchain" / "toka_package.py"),
         "fetch", "--manifest", "package.tk", "--lock", "package.lock", "--state", ".toka"],
        cwd=project,
        check=True,
        timeout=120,
    )
    library = project / ".toka" / "packages" / f"{dependency}-{resolved}" / "lib"
    if not library.is_dir():
        raise RuntimeError(f"registry fixture did not install {dependency}@{resolved}")
    return library
