# Toka v1.0.0-rc.2 Candidate Notes

**Status:** In qualification. This document describes the source candidate;
`v1.0.0-rc.2` is not tagged or published until its exact revision passes the
four-platform release gate.

## Purpose

`1.0.0-rc.2` is the next public 1.0 release candidate from the frozen language
surface. It qualifies the current SDK and developer workflow without claiming
new language syntax or a new `.tki` compatibility format.

## Candidate Improvements

- The documented public `--json` compiler-tooling paths have a strict
  one-document stdout contract, with golden coverage for successful and
  failing semantic queries.
- `toka test` is honestly marked Preview: it is an experimental source-tree
  scanner, not the stable project-test or package-qualification command.
- `toka new` accepts an absolute destination path without placing that full
  path in generated package or executable names.
- Installed `toka add` and `toka fetch` locate their package helper through
  the SDK's own library root; users do not need to export `TOKA_LIB` first.
- The four blocking release architectures are explicit: Linux x64/arm64 and
  macOS x64/arm64. Windows/MSYS2 remains non-blocking dogfood evidence.

## Qualification Boundary

The candidate must pass the exact-revision, four-target, thirteen-stage
Release Gate before a tag or GitHub pre-release is created. The required
reports, archive digests, and native first-hour replay are defined in
[`1_0_rc2_release_plan.md`](1_0_rc2_release_plan.md). A later `main` revision
is not part of this candidate and requires a new label and gate.

When published, install `v1.0.0-rc.2` explicitly rather than relying on
GitHub's stable-release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.2
```
