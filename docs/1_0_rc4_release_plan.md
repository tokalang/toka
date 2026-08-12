# Toka 1.0.0-rc.4 Candidate Qualification Plan

**Status:** In qualification. This plan creates neither a tag nor a public
release. `v1.0.0-rc.1` remains the published historical candidate; immutable
`v1.0.0-rc.2` and `v1.0.0-rc.3` tags are non-qualified and must not be reused.

## Decision and boundary

RC4 is a new source candidate from the frozen 1.0 language surface. It fixes
two release-integrity defects: a test made an unqualified `race2`
cancel-join-drain assertion, and Toka-authored SDK tools hard-coded an older RC
label despite the release override.

- Public compiler, SDK, formatter, LSP, and diagnostic-spec labels are
  `1.0.0-rc.4`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`; this candidate label
  is not a `.tki` or semantic-cache compatibility change.
- The current TaskHandle v2 contract remains a qualified subset. RC4 changes
  no runtime guarantee; its source regression now checks global cleanup only
  after the bounded `block_on` drain boundary.
- `toka --help` and `tokafmt --version` obtain the release label from their
  sibling `tokac`; `tokac`, `toka`, `tokafmt`, and `tokalsp` must agree for both
  normal and `TOKA_RELEASE_VERSION_OVERRIDE` builds.
- Matrix jobs produce private evidence only. A post-matrix summary must verify
  one SHA, the fixed four-target set, and thirteen passing stages. A tag may
  create only a complete draft with exact archive names and `SHA256SUMS`; it
  never publishes automatically.

## Required evidence before draft authorization

1. The RC4 version migration and fixes are committed and pushed to `main`. A
   clean release-override build proves all four SDK executables report
   `1.0.0-rc.4` and developer-experience validation passes.
2. The pushed clean revision is recorded as the sole RC4 candidate SHA. No
   historical RC tag is a release input.
3. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.4`. This path cannot
   publish a release.
4. Linux x64/arm64 and macOS x64/arm64 each produce a clean
   `toka.release-gate` v2 report with that SHA, `source_dirty=false`,
   `version_label=v1.0.0-rc.4`, and all thirteen stages passing. Each also
   produces a passing TaskHandle v2 conformance record bound to the same
   contract digest. Windows/MSYS2 remains non-blocking Tier-2 dogfood evidence.
5. Review the generated qualification summary and all four reports together.
   Only then may maintainers authorize an annotated `v1.0.0-rc.4` tag. Its
   workflow must repeat the four-target gate and create a **draft** with four
   archives plus `SHA256SUMS`; it never publishes automatically.
6. Record SHA-256 digests and replay the tagged draft archives from clean
   extraction through `toka doctor`, `toka new`, registry resolution, `toka
   run`, offline lock replay, and `toka preview`. Record the receipt in the
   audit, then seek separate authorization for protected promotion.

The resulting record belongs in
[`release_audits/v1.0.0-rc.4.md`](release_audits/v1.0.0-rc.4.md).
