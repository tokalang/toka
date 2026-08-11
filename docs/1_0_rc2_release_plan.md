# Toka 1.0.0-rc.2 Candidate Qualification Plan

**Status:** In qualification. This plan creates neither a tag nor a public
release. `v1.0.0-rc.1` remains an immutable, published historical candidate.

## Decision

On 2026-08-11, maintainers chose `1.0.0-rc.2` for the next public candidate
from `main`. The candidate keeps the frozen 1.0 language surface and carries
the post-RC1 developer-experience fixes and machine-interface qualification.
It does not create syntax, TKI-format, or runtime-contract changes.

## Candidate Boundary

- Public compiler, SDK, package, formatter, LSP, and diagnostic-spec labels
  are `1.0.0-rc.2`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`. It is the independent
  `.tki` and semantic-cache compatibility key; a candidate-label change alone
  must not invalidate source-less interfaces.
- `toka check`, `index`, `evidence`, `cede-obligations`, and related documented
  `--json` paths keep their one-document stdout contract under a golden
  regression. `toka test` remains Preview, not a stable package-test contract.
- Linux x64, Linux arm64, macOS x64, and macOS arm64 are all blocking release
  targets. Windows/MSYS2 remains non-blocking Tier-2 dogfood evidence.

## Required Evidence Before Publication

1. The version migration is committed and pushed to `main`. A clean
   reconfigure proves that `tokac`, `toka`, `tokafmt`, and `tokalsp` agree on
   `1.0.0-rc.2`; the developer-experience regression enforces that agreement.
2. The pushed, clean `main` revision is recorded as the sole candidate SHA.
   Existing RC1 tags and artifacts cannot be moved, renamed, or reused.
3. `.github/workflows/release.yml` is manually dispatched from that exact
   revision with `tag_name=v1.0.0-rc.2` and `publish_release=false`.
4. Each of Linux x64/arm64 and macOS x64/arm64 produces a clean
   `toka.release-gate` v2 report with `revision` equal to the candidate SHA,
   `source_dirty=false`, `version_label=v1.0.0-rc.2`, and all thirteen stages
   passing.
5. Only after all four reports are reviewed together may maintainers authorize
   the annotated `v1.0.0-rc.2` tag and the GitHub pre-release archives.
6. The attached archives must be SHA-256 recorded and replayed from a clean,
   native release-host extraction through `toka doctor`, `toka new`, registry
   resolution, `toka run`, and offline lock replay before announcement.

The result is recorded after qualification in
[`release_audits/v1.0.0-rc.2.md`](release_audits/v1.0.0-rc.2.md). It is a
separate historical record from the [RC1 audit](release_audits/v1.0.0-rc.1.md).
