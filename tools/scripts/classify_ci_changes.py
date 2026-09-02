#!/usr/bin/env python3

"""Classify a change set so documentation does not start platform builds."""

import argparse
from pathlib import Path
import subprocess


DOC_PREFIXES = ("docs/", ".github/ISSUE_TEMPLATE/")
DOC_FILES = {
    "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    ".github/PULL_REQUEST_TEMPLATE.md",
}
DOC_SUFFIXES = {".gif", ".jpeg", ".jpg", ".md", ".mdx", ".png", ".webp"}


def is_documentation(path):
    candidate = Path(path)
    return (path in DOC_FILES or path.startswith(DOC_PREFIXES) or
            candidate.suffix.lower() in DOC_SUFFIXES)


def requires_heavy(paths):
    paths = tuple(path for path in paths if path)
    return not paths or any(not is_documentation(path) for path in paths)


def changed_paths(base, head):
    if not base or set(base) == {"0"}:
        return ()
    result = subprocess.run(
        ["git", "diff", "--name-only", base, head],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        return ()
    return tuple(result.stdout.splitlines())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--github-output")
    args = parser.parse_args()

    paths = changed_paths(args.base, args.head)
    heavy = requires_heavy(paths)
    output = "heavy=%s\n" % ("true" if heavy else "false")
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as stream:
            stream.write(output)
    else:
        print(output, end="")
    print("change scope: %s (%d files)" %
          ("compiler/sdk" if heavy else "documentation-only", len(paths)))


if __name__ == "__main__":
    main()
