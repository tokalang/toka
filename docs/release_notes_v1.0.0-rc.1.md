# Toka v1.0.0-rc.1 Candidate Notes

**Status:** In qualification. This document describes the source candidate;
`v1.0.0-rc.1` has not yet been tagged, published, or made the latest release.

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

## Qualification Status

The current source has passed its normal CI and Windows dogfood checks. It
must still pass the exact-revision, four-target, thirteen-stage Release Gate
before an annotated tag or GitHub pre-release is created. The required evidence
and publication boundary are recorded in
[`1_0_rc1_release_plan.md`](1_0_rc1_release_plan.md).

Before announcement, the tagged SDK archives also undergo the public
first-hour check: exact-tag installation, `toka doctor`, project creation,
verified registry resolution, execution, and offline lock replay. This is the
developer-experience boundary for the RC; it does not reopen the frozen
language surface.
