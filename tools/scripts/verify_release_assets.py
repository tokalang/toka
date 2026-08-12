#!/usr/bin/env python3

"""Verify a complete release archive set and its SHA-256 manifest."""

import argparse
import hashlib
from pathlib import Path
import sys


TARGETS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_names(version_label):
    return tuple("toka-%s-%s.tar.gz" % (version_label, target) for target in TARGETS)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--version-label", required=True)
    parser.add_argument("--checksums-output", type=Path)
    parser.add_argument("--require-checksums", action="store_true")
    args = parser.parse_args()

    expected = expected_names(args.version_label)
    actual = tuple(sorted(path.name for path in args.assets_dir.glob("toka-*.tar.gz") if path.is_file()))
    if actual != tuple(sorted(expected)):
        raise SystemExit("archive names do not match the required four-target set")
    allowed = set(expected) | {"SHA256SUMS"}
    unexpected = sorted(path.name for path in args.assets_dir.iterdir()
                        if path.is_file() and path.name not in allowed)
    if unexpected:
        raise SystemExit("release asset directory contains unexpected files: " + ", ".join(unexpected))
    lines = ["%s  %s" % (sha256(args.assets_dir / name), name) for name in sorted(expected)]
    manifest = "\n".join(lines) + "\n"
    output = args.checksums_output or args.assets_dir / "SHA256SUMS"
    if args.require_checksums:
        if not output.is_file() or output.read_text(encoding="utf-8") != manifest:
            raise SystemExit("SHA256SUMS does not match the complete archive set")
    else:
        output.write_text(manifest, encoding="utf-8")
    print("release asset verification PASSED")


if __name__ == "__main__":
    main()
