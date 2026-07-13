#!/usr/bin/env python3
"""Bounded extraction for Toka source packages."""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tarfile


DEFAULT_MAX_ENTRIES = 10_000
DEFAULT_MAX_FILE_SIZE = 64 * 1024 * 1024
DEFAULT_MAX_TOTAL_SIZE = 512 * 1024 * 1024


class ExtractionError(RuntimeError):
    pass


def _validated_members(
    archive: tarfile.TarFile,
    max_entries: int,
    max_file_size: int,
    max_total_size: int,
) -> list[tuple[tarfile.TarInfo, PurePosixPath]]:
    members = archive.getmembers()
    if len(members) > max_entries:
        raise ExtractionError("archive entry limit exceeded")

    checked: list[tuple[tarfile.TarInfo, PurePosixPath]] = []
    seen: set[str] = set()
    total_size = 0
    for member in members:
        name = member.name
        if not name or "\\" in name or "\x00" in name:
            raise ExtractionError("archive contains an invalid path")
        if any(ord(character) < 32 for character in name):
            raise ExtractionError("archive path contains control characters")

        path = PurePosixPath(name)
        if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
            raise ExtractionError("archive path escapes the package root: " + name)
        normalized = path.as_posix()
        if normalized in seen:
            raise ExtractionError("archive contains a duplicate path: " + normalized)
        seen.add(normalized)

        if member.isdir():
            checked.append((member, path))
            continue
        if not member.isreg():
            raise ExtractionError("archive contains a link or special file: " + normalized)
        if member.size < 0 or member.size > max_file_size:
            raise ExtractionError("archive member size limit exceeded: " + normalized)
        total_size += member.size
        if total_size > max_total_size:
            raise ExtractionError("archive expanded-size limit exceeded")
        checked.append((member, path))
    return checked


def safe_extract(
    archive_path: Path,
    destination: Path,
    *,
    max_entries: int = DEFAULT_MAX_ENTRIES,
    max_file_size: int = DEFAULT_MAX_FILE_SIZE,
    max_total_size: int = DEFAULT_MAX_TOTAL_SIZE,
) -> None:
    if destination.exists() and any(destination.iterdir()):
        raise ExtractionError("extraction destination must be empty")
    destination.mkdir(parents=True, exist_ok=True)
    destination_root = destination.resolve()

    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            members = _validated_members(
                archive, max_entries, max_file_size, max_total_size
            )
            for member, relative in members:
                target = destination.joinpath(*relative.parts)
                resolved_parent = target.parent.resolve()
                if resolved_parent != destination_root and destination_root not in resolved_parent.parents:
                    raise ExtractionError("archive member escapes the package root")
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue

                target.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                if source is None:
                    raise ExtractionError("archive member has no data: " + relative.as_posix())
                with source, target.open("xb") as output:
                    shutil.copyfileobj(source, output, length=1024 * 1024)
                mode = 0o755 if member.mode & stat.S_IXUSR else 0o644
                os.chmod(target, mode)
    except (tarfile.TarError, OSError) as error:
        raise ExtractionError(str(error)) from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("destination")
    parser.add_argument("--max-entries", type=int, default=DEFAULT_MAX_ENTRIES)
    parser.add_argument("--max-file-size", type=int, default=DEFAULT_MAX_FILE_SIZE)
    parser.add_argument("--max-total-size", type=int, default=DEFAULT_MAX_TOTAL_SIZE)
    args = parser.parse_args()
    try:
        safe_extract(
            Path(args.archive),
            Path(args.destination),
            max_entries=args.max_entries,
            max_file_size=args.max_file_size,
            max_total_size=args.max_total_size,
        )
    except ExtractionError as error:
        sys.stderr.write("safe extraction failed: %s\n" % error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
