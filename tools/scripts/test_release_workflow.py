#!/usr/bin/env python3

"""Fail closed when the release workflow can publish a partial matrix result."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/release.yml"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def job_block(text, name, next_name=None):
    marker = "  %s:\n" % name
    start = text.find(marker)
    require(start >= 0, "release workflow is missing job: " + name)
    end = len(text)
    if next_name:
        next_marker = "  %s:\n" % next_name
        end = text.find(next_marker, start + len(marker))
        require(end >= 0, "release workflow job ordering changed: " + next_name)
    return text[start:end]


def main():
    text = WORKFLOW.read_text(encoding="utf-8")
    gate = job_block(text, "release-gate", "publish-release")
    publish = job_block(text, "publish-release")

    require("softprops/action-gh-release" not in gate,
            "matrix gate must not publish a release directly")
    require("contents: read" in gate,
            "matrix gate must not receive release-write permission")
    require("actions/upload-artifact@v4" in gate and
            "name: release-archive-${{ matrix.name }}" in gate,
            "each successful matrix member must upload a named archive artifact")
    require("needs: release-gate" in publish,
            "release publication must wait for the full matrix")
    require("needs.release-gate.result == 'success'" in publish,
            "release publication must require an all-green matrix")
    require("actions/download-artifact@v4" in publish and
            "pattern: release-archive-*" in publish and
            "merge-multiple: true" in publish,
            "publisher must gather every matrix archive artifact")
    require("expected=4" in publish,
            "publisher must reject an incomplete archive set")
    require("softprops/action-gh-release@v2" in publish and
            "files: release-assets/*.tar.gz" in publish,
            "only the aggregate publisher may attach public release assets")

    print("Release workflow atomic-publication gate PASSED")


if __name__ == "__main__":
    main()
