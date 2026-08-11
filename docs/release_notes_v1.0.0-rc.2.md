# Toka v1.0.0-rc.2 Candidate Notes

**Status:** Withdrawn; never publicly released. This document is retained as
historical candidate notes. The immutable `v1.0.0-rc.2` tag must not be moved,
reused, or published.

## Purpose

`1.0.0-rc.2` was a pre-public candidate from the frozen language surface. A
post-qualification audit withdrew it before release because several public
contracts were stronger than their implementation or package behavior. RC3
supersedes it with a new revision and fresh qualification.

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
- The SDK includes the experimental, release-matched AI Completion Card v0.1
  for coding agents; compiler JSON evidence and project tests remain
  authoritative for every proposed edit.
- The four blocking release architectures are explicit: Linux x64/arm64 and
  macOS x64/arm64. Windows/MSYS2 remains non-blocking dogfood evidence.

## Qualification Boundary

The current candidate and publication path are documented in
[`release_notes_v1.0.0-rc.3.md`](release_notes_v1.0.0-rc.3.md) and
[`1_0_rc3_release_plan.md`](1_0_rc3_release_plan.md).
