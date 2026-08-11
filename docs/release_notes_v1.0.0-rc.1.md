# Toka v1.0.0-rc.1 Candidate Notes

**Status:** Published pre-release. `v1.0.0-rc.1` was tagged at
`05ba085252977d945d5f4e11ecfad363147388d8` and published on 2026-08-10. It
remains a pre-release and is not the latest stable SDK.

## Purpose

`1.0.0-rc.1` begins Toka's public 1.0 release-candidate line after the frozen
language and SDK engineering work represented by `v0.9.9-01`. It does not add
new source syntax. Its purpose is to qualify the current frozen implementation
on the release matrix and collect adoption feedback without presenting an
unfinished candidate as a final 1.0 release.

## Candidate Boundary

- The language surface remains the one frozen in
  [`1_0_freeze_decision_list.md`](1_0_freeze_decision_list.md).
- Linux x64/arm64 and macOS x64/arm64 remain the blocking release targets.
  Windows/MSYS2 remains non-blocking dogfood evidence.
- Source-less `.tki` and semantic-cache compatibility stay keyed by the
  existing internal compiler-interface version. The public RC label alone
  does not alter their format.

## Historical qualification boundary

The release was created only from its tagged revision. The exact-revision,
four-target, thirteen-stage boundary and release process are recorded in
[`1_0_rc1_release_plan.md`](1_0_rc1_release_plan.md). Later commits on `main`
are not part of this RC and require a distinct candidate label before release.

The tagged SDK archives are the public developer-experience boundary: exact
archive installation, `toka doctor`, project creation, verified registry
resolution, execution, and offline lock replay. This does not reopen the
frozen language surface.
