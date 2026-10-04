#!/usr/bin/env python3

"""Verify a complete release archive set and its SHA-256 manifest."""

import argparse
import hashlib
from pathlib import Path
import sys
import json
try:
    import release_platform_policy as platforms
except ModuleNotFoundError:
    from tools.scripts import release_platform_policy as platforms


TARGETS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_names(version_label,targets=None):
    return platforms.archive_names(version_label,targets or platforms.core_targets(version_label))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--version-label", required=True)
    parser.add_argument("--checksums-output", type=Path)
    parser.add_argument("--require-checksums", action="store_true")
    parser.add_argument('--qualification-summary',type=Path)
    args = parser.parse_args()

    targets=platforms.core_targets(args.version_label)
    if platforms.modern(args.version_label):
        if args.qualification_summary is None:raise SystemExit('0.12 assets require a bound qualification summary')
        summary=json.loads(args.qualification_summary.read_text());targets=platforms.included_targets(summary,summary.get('candidate_revision'),args.version_label)
    expected = expected_names(args.version_label,targets)
    actual = tuple(sorted(path.name for path in args.assets_dir.glob("toka-*.tar.gz") if path.is_file()))
    if actual != tuple(sorted(expected)):
        raise SystemExit("archive names do not match the exact version-bound target set")
    if platforms.modern(args.version_label) and platforms.OPTIONAL in targets:
        errors=platforms.optional_errors(summary['optional_targets'][platforms.OPTIONAL],summary['candidate_revision'],args.version_label,sha256(args.assets_dir/('toka-%s-%s.tar.gz'%(args.version_label,platforms.OPTIONAL))))
        if errors:raise SystemExit('; '.join(errors))
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
