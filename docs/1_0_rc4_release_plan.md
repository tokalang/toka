# Toka 1.0.0-rc.4 Candidate Qualification Plan

**Status:** Historical plan. Annotated tag object
`ad9499d18cf98bcfd930c7ea7b999191155550db` points to candidate commit
`5c9cb77b5708d2ce598720821da97b683c0edf9f`, and the GitHub pre-release was
published at `2026-08-12T12:33:33Z`. The repository has no recorded
protected-promotion workflow run or clean first-hour replay receipt for RC4,
so those original requirements remain unverified. When this plan was written,
`v1.0.0-rc.1` was the published historical candidate; immutable
`v1.0.0-rc.2` and `v1.0.0-rc.3` tags were non-qualified and could not be reused.

## Decision and boundary

RC4 is a new source candidate from the frozen 1.0 language surface. It fixes
two release-integrity defects: a test made an unqualified `race2`
cancel-join-drain assertion, and Toka-authored SDK tools hard-coded an older RC
label despite the release override. It also includes bounded `std/process` and
`stdx/crypto` API additions, with explicit POSIX and RFC-vector boundaries.

- Public compiler, SDK, formatter, LSP, and diagnostic-spec labels are
  `1.0.0-rc.4`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`; this candidate label
  is not a `.tki` or semantic-cache compatibility change.
- The current TaskHandle v2 contract remains a qualified subset. RC4 changes
  no runtime guarantee; its source regression checks only that externally
  canceled `race2` children do not execute their user side effects. It does not
  claim unqualified race2 cancel-join-drain or global wait-registry retirement.
- `toka --help` and `tokafmt --version` obtain the release label from their
  sibling `tokac`; `tokac`, `toka`, `tokafmt`, and `tokalsp` must agree for both
  normal and `TOKA_RELEASE_VERSION_OVERRIDE` builds.
- `std/process` retains explicit child wait ownership: configuration applies in
  the POSIX child only, cancellation neither waits nor hides cleanup, and
  Windows/WASI reject custom configuration. `stdx/crypto` adds SHA-512 and
  HMAC-SHA-1/SHA-512 without FFI crypto authority.
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
3. Before dispatch, run the isolated local prequalification for native macOS
   ARM64 and Docker Linux ARM64/AMD64. It uses the same gate but is explicitly
   non-authoritative; see
   [`local_release_prequalification.md`](local_release_prequalification.md).
4. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.4`. This path cannot
   publish a release.
5. Linux x64/arm64 and macOS x64/arm64 each produce a clean
   `toka.release-gate` v2 report with that SHA, `source_dirty=false`,
   `version_label=v1.0.0-rc.4`, and all thirteen stages passing. Each also
   produces a passing TaskHandle v2 conformance record bound to the same
   contract digest. Windows/MSYS2 remains non-blocking Tier-2 dogfood evidence.
6. Review the generated qualification summary and all four reports together.
   Only then may maintainers authorize an annotated `v1.0.0-rc.4` tag. Its
   workflow must repeat the four-target gate and create a **draft** with four
   archives plus `SHA256SUMS`; it never publishes automatically.
7. Record SHA-256 digests and replay the tagged draft archives from clean
   extraction through `toka doctor`, `toka new`, registry resolution, `toka
   run`, offline lock replay, and `toka preview`. Record the receipt in the
   audit, then seek separate authorization for protected promotion.

The resulting record belongs in
[`release_audits/v1.0.0-rc.4.md`](release_audits/v1.0.0-rc.4.md).
