# Toka 1.0.0-rc.3 Candidate Qualification Plan

**Status:** In qualification. This plan creates neither a tag nor a public
release. `v1.0.0-rc.1` remains the published historical candidate; the
immutable `v1.0.0-rc.2` tag is withdrawn and must not be reused.

## Decision and boundary

RC3 is a new source candidate from the frozen 1.0 language surface. It closes
the public-contract defects discovered while auditing RC2 without introducing
grammar, PAL, CodeGen, TKI-format, or runtime behavior changes.

- Public compiler, SDK, formatter, LSP, package, and diagnostic-spec labels
  are `1.0.0-rc.3`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`; the candidate label is
  not a `.tki` or semantic-cache compatibility change.
- The TaskHandle machine contract is explicitly a `qualified-subset`: it
  states only current direct-await/cold-cleanup behavior and marks deferred
  cancellation, TaskScope, and exact PlaceState bridges as unqualified.
- `check`, `index`, `context`, and `query` emit exactly one machine-readable
  JSON object for accepted and rejected semantic inputs. Failed index/context/
  query requests use `toka.diagnostics` v2.
- The release archive includes `semantic_diff_preview.py`; archive smoke
  executes `toka preview` from a clean extraction and verifies the packaged,
  release-matched AI Completion Card v0.2. A failing `toka test` command exits
  nonzero.
- Matrix jobs only produce private artifacts. One post-matrix publisher
  verifies all four archives and creates or updates the GitHub pre-release
  once, so no partial release becomes public.
- The scoped DX-0 authoring-friction corpus makes four recurring H/P and
  borrowed-pattern mistakes executable without freezing a new semantic schema
  or changing source syntax.

## Required evidence before publication

1. The RC3 version migration and all listed fixes are committed and pushed to
   `main`. A clean reconfigure proves that `tokac`, `toka`, `tokafmt`, and
   `tokalsp` agree on `1.0.0-rc.3`.
2. The pushed clean revision is recorded as the sole RC3 candidate SHA. RC1
   and withdrawn RC2 tags remain immutable and are not release inputs.
3. Dispatch `.github/workflows/release.yml` from that SHA with
   `tag_name=v1.0.0-rc.3` and `publish_release=false`.
4. Linux x64/arm64 and macOS x64/arm64 each produce a clean
   `toka.release-gate` v2 report with the candidate SHA, `source_dirty=false`,
   `version_label=v1.0.0-rc.3`, and all thirteen stages passing. Windows/MSYS2
   remains non-blocking Tier-2 dogfood evidence.
5. Review all four reports together. Only then may maintainers authorize a new
   annotated `v1.0.0-rc.3` tag. Its workflow must first complete all matrix
   jobs, then create the pre-release atomically from exactly four archives.
6. Record SHA-256 digests and replay the tagged native archives from clean
   extraction through `toka doctor`, `toka new`, registry resolution, `toka
   run`, offline lock replay, and `toka preview` before announcement.

The resulting record belongs in
[`release_audits/v1.0.0-rc.3.md`](release_audits/v1.0.0-rc.3.md). A later
`main` revision is a different candidate and requires a new label and gate.
