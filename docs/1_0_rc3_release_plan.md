# Toka 1.0.0-rc.3 Candidate Qualification Plan

**Status:** Historical, non-qualified tag. The immutable `v1.0.0-rc.3` tag
failed its tag gate before draft creation and must not be reused. `v1.0.0-rc.1`
remains the published historical candidate; the immutable `v1.0.0-rc.2` tag is
also withdrawn.

## Decision and boundary

RC3 is a new source candidate from the frozen 1.0 language surface. It closes
the public-contract defects discovered while auditing RC2 without introducing
grammar, PAL, CodeGen, TKI-format, or runtime behavior changes.

- Public compiler, SDK, formatter, LSP, package, and diagnostic-spec labels
  are `1.0.0-rc.3`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`; the candidate label is
  not a `.tki` or semantic-cache compatibility change.
- Historical TaskHandle Lifecycle Contract v1 is retained unchanged, with an
  explicit withdrawn-evidence erratum. The new v2 contract is a
  `qualified-subset`: it states only current direct-await/cold-cleanup behavior,
  names stable guarantee IDs, and binds passed evidence to the exact revision.
- `check`, `index`, `context`, `query`, and valid unknown-code `explain`
  requests emit exactly one machine-readable
  JSON object for accepted and rejected semantic inputs. Failed index/context/
  query requests use `toka.diagnostics` v2.
- The release archive includes `semantic_diff_preview.py`; archive smoke
  executes `toka preview` from a clean extraction and verifies the packaged,
  release-matched AI Completion Card v0.2. A failing `toka test` command exits
  nonzero.
- Matrix jobs only produce private evidence. A post-matrix summary verifies
  all four reports have one SHA, target set, and thirteen passing stages. A tag
  can then create only a complete **draft** release with exact archive names
  and SHA-256 manifest; protected manual promotion verifies the downloaded
  draft assets and a first-hour replay receipt before public pre-release.
- The scoped DX-0 authoring-friction corpus makes four recurring H/P and
  borrowed-pattern mistakes executable without freezing a new semantic schema
  or changing source syntax.

## Required evidence before publication

1. The RC3 version migration and all listed fixes are committed and pushed to
   `main`. A clean reconfigure proves that `tokac`, `toka`, `tokafmt`, and
   `tokalsp` agree on `1.0.0-rc.3`.
2. The pushed clean revision is recorded as the sole RC3 candidate SHA. RC1
   and withdrawn RC2 tags remain immutable and are not release inputs.
3. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.3`. This path cannot
   publish a release.
4. Linux x64/arm64 and macOS x64/arm64 each produce a clean
   `toka.release-gate` v2 report with the candidate SHA, `source_dirty=false`,
   `version_label=v1.0.0-rc.3`, and all thirteen stages passing. Each also
   produces a passing TaskHandle v2 conformance record for that SHA; all four
   must bind the same contract digest. Windows/MSYS2 remains non-blocking Tier-2
   dogfood evidence.
5. Review the generated qualification summary and all four reports together.
   Only then may maintainers authorize a new annotated `v1.0.0-rc.3` tag. Its
   workflow must repeat the exact four-target gate and create a **draft** with
   exactly four archives plus `SHA256SUMS`; it never publishes automatically.
6. Record SHA-256 digests and replay the tagged draft archives from clean
   extraction through `toka doctor`, `toka new`, registry resolution, `toka
   run`, offline lock replay, and `toka preview` before announcement. Record
   the receipt in the audit, then dispatch `promote_release.yml`; its protected
   environment is the final explicit authorization to publish. Repository
   administrators must configure `release-publication` with required reviewers;
   the YAML environment name alone cannot create that protection.

The resulting record belongs in
[`release_audits/v1.0.0-rc.3.md`](release_audits/v1.0.0-rc.3.md). A later
`main` revision is a different candidate and requires a new label and gate.
