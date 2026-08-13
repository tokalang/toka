# Toka 1.0.0-rc.5 Candidate Qualification Plan

**Status:** Preparation only. No exact RC5 candidate revision has been
recorded, and `v1.0.0-rc.5` is not tagged, drafted, or published. Historical
RC tags and their build caches are not release inputs for this candidate.

## Decision and boundary

RC5 is a new source candidate from the frozen 1.0 language surface. It
collects the compiler, cache-validation, diagnostic-generation, and
cross-platform SDK corrections made after RC4. Qualification remains
fail-closed: source changes alone are not release evidence.

- Public compiler, SDK, formatter, LSP, and diagnostic-spec labels must be
  `1.0.0-rc.5` before a candidate revision is selected.
- `.tki` metadata remains format 2, while
  `TOKA_COMPILER_INTERFACE_VERSION` is `0.9.9-10`. RC4 caches used the older
  compatibility key and must not be reused. Every qualification build,
  semantic replay, package, and installed-archive replay must start without
  RC4 `.tki`, object, or semantic-cache artifacts.
- The regression recovery covers atomic ordering validation and lowering,
  generic ownership and dynamic-interface CodeGen, trusted SDK-module
  provenance, diagnostic registration, interface-cache validation, and the
  platform seams exercised by SDK source builds.
- Matrix jobs produce private evidence only. A post-matrix summary must verify
  one immutable SHA, the fixed Linux x64, Linux arm64, macOS x64, and macOS
  arm64 target set, and thirteen passing stages. Windows/MSYS2 remains
  non-blocking Tier-2 dogfood evidence.
- A tag may create only a complete draft with the four exact native archives
  and `SHA256SUMS`. The draft must be replayed from clean extraction before a
  separately protected promotion can make it public.

## Required evidence before draft authorization

1. Commit and push the RC5 identity migration and regression recovery to
   `main`. A clean release-override build must prove that `tokac`, `toka`,
   `tokafmt`, and `tokalsp` report `1.0.0-rc.5`, and that emitted interfaces
   use format 2 with compatibility key `0.9.9-10`.
2. Select one clean pushed revision and record it as the sole RC5 candidate
   SHA. No historical RC tag, worktree, archive, or cache may supply build
   inputs.
3. Before hosted dispatch, run isolated local prequalification for native
   macOS ARM64 and Docker Linux ARM64/AMD64. This uses the same gate but is
   explicitly non-authoritative; see
   [`local_release_prequalification.md`](local_release_prequalification.md).
4. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.5`. This path cannot
   publish a release.
5. Linux x64/arm64 and macOS x64/arm64 must each produce a clean
   `toka.release-gate` v2 report with that SHA, `source_dirty=false`,
   `version_label=v1.0.0-rc.5`, and all thirteen stages passing. Each target
   must also produce a passing TaskHandle Lifecycle v2 conformance record
   bound to that SHA and the same contract digest.
6. Review the generated qualification summary and all four native reports
   together. Confirm that no report or packaged output reused an RC4 cache.
   Only then may maintainers authorize an annotated `v1.0.0-rc.5` tag.
7. The tag workflow must repeat the four-target gate and create a **draft**
   containing exactly four native archives plus `SHA256SUMS`. It must not
   publish automatically.
8. Record the draft asset SHA-256 digests, then replay every archive from a
   clean extraction through `toka doctor`, `toka new`, registry resolution,
   `toka run`, offline lock replay, and `toka preview`. Record the receipts in
   the audit.
9. After the audit contains the complete draft and clean-replay evidence, seek
   separate authorization through the protected promotion workflow. Any
   missing or mismatched evidence blocks promotion.

The resulting record belongs in
[`release_audits/v1.0.0-rc.5.md`](release_audits/v1.0.0-rc.5.md).
