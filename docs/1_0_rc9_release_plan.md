# Toka 1.0.0-rc.9 Candidate Qualification Plan

**Status:** Preparation only. No immutable RC9 candidate SHA has been selected,
and `v1.0.0-rc.9` is not tagged, drafted, or published.

## Candidate scope

RC9 is the release checkpoint for the accepted signature-driven call-transfer
ADR and the EXP-LIN-02 owning-aggregate copy fix. It includes the call-route,
atomic rollback, CodeGen fail-closed, Evidence v2, lint, and source/TKI replay
work required to activate that decision by default.

The candidate does not start a general transaction engine, branch join,
PlaceState redesign, new async/TCB work, LSP inlay hints, or a broader exact-
place language surface. D.3, D.4, and M1b.2a remain internal qualification
artifacts rather than new public semantic authorities.

## Compatibility boundary

- Public version label: `1.0.0-rc.9` / `v1.0.0-rc.9`.
- `.tki` metadata format: `3`.
- Compiler-interface compatibility key: `0.9.9-15`.
- Required place-yield ABI schema: `1`.
- All RC8 `.tki`, object, semantic-manifest, and semantic-cache artifacts are
  prohibited inputs.
- Argument-level `cede` accepted by RC8 remains valid. RC9 additionally accepts
  qualified bare call transfers selected by a `cede` formal.

## Required evidence before tag authorization

1. Merge the published RC8 qualification and promotion record, complete the
   RC9 compatibility/version changes, and select a clean immutable candidate
   only after all preparation changes are committed.
2. Run the full compiler build, CTest, positive/fail/conformance suites,
   source-less TKI replay, cede Evidence v1/v2, implicit-move lint, mixed-call
   rollback, and `E0761` fault qualification at that exact revision.
3. Run isolated local prequalification for native macOS ARM64 and Docker Linux
   ARM64/AMD64 from the exact candidate revision. Local evidence is
   non-authoritative.
4. Confirm a release build reports interface key `0.9.9-15`, rejects an RC8
   `0.9.9-14` interface, and creates only `v1.0.0-rc.9` archives.
5. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.9`.
6. Linux x64, Linux arm64, macOS x64, and macOS arm64 must each pass all
   thirteen release stages with the same SHA, `source_dirty=false`, and the
   same version label. Windows/MSYS2 remains Tier-2 feedback.
7. Review the four reports together before authorizing an annotated
   `v1.0.0-rc.9` tag that peels exactly to the candidate SHA.
8. The tag workflow repeats the gate and creates only a draft containing four
   native archives plus `SHA256SUMS`.
9. Replay every archive from a clean extraction through `toka doctor`, project
   creation/run, path and registry resolution, strict offline lock replay, and
   semantic preview. Record the hosted macOS Intel replay.
10. Only after the audit contains the immutable SHA, jobs, digests, replay
    receipts, and protected approval may the pre-release be promoted.

Evidence belongs in
[`release_audits/v1.0.0-rc.9.md`](release_audits/v1.0.0-rc.9.md).
