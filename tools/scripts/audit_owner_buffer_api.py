#!/usr/bin/env python3
"""Keep high-level Toka async I/O on owner-carrying buffer APIs."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SCOPES = (ROOT / "lib" / "stdx", ROOT / "official", ROOT / "examples", ROOT / "tools")
RAW_CALL = re.compile(
    r"\.(?:read_async|read_async_timeout|read_exact_async|"
    r"write_all_async|write_all_async_timeout|write_async)\("
)
OWNER_CALL = re.compile(r"\.(?:read_into_async|write_from_async)\(")


def main() -> int:
    raw_sites = []
    owner_sites = 0
    for scope in SCOPES:
        for source in scope.rglob("*.tk"):
            text = source.read_text()
            owner_sites += len(OWNER_CALL.findall(text))
            for line, content in enumerate(text.splitlines(), start=1):
                if RAW_CALL.search(content):
                    raw_sites.append((source, line))

    print(f"owner-carrying async I/O call sites: {owner_sites}")
    print(f"raw async I/O call sites: {len(raw_sites)}")
    for source, line in raw_sites:
        print(f"  {source.relative_to(ROOT)}:{line}")

    if raw_sites:
        print("raw async I/O calls found:", file=sys.stderr)
        for source, line in raw_sites:
            print(f"  {source.relative_to(ROOT)}:{line}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
