# Toka 1.0.0-rc.7 Candidate Qualification Plan

**Status:** Preparation only. Commit `62b73552` is the semantic baseline, not
the final candidate. No immutable RC7 candidate SHA has been selected, and
`v1.0.0-rc.7` is not tagged, drafted, or published.

## Frozen language boundary

- Safe nullable syntax is permanently removed. `T?`, `nul ^T`, `nul ~T`,
  `none`, and nullable `?`/`??` are rejected. `.await?` is unchanged.
- Plain raw pointers `*T` are non-zero. `nul *T` is the FFI/system may-zero
  form, and narrowing requires an explicit guard or `.unwrap()`.
- `T | miss` is constructed only by function return and has no default value.
- `Option` remains a distinct standard-library type in RC7; this candidate
  does not rewrite it as `miss` syntax.
- Init and PlaceState contracts are unchanged.

## Compatibility boundary

- Public version label: `1.0.0-rc.7` / `v1.0.0-rc.7`.
- `.tki` metadata format remains `2`.
- Compiler-interface compatibility key is `0.9.9-12`.
- RC6 `.tki`, object, and semantic-cache artifacts are prohibited inputs.

## Required evidence before tag authorization

1. Commit and push all RC7 identity, compiler, SDK, test, and documentation
   changes. Select one clean, immutable candidate SHA only after local gates
   pass.
2. Run isolated local prequalification for native macOS ARM64 and Docker Linux
   ARM64/AMD64 from that exact revision. Local evidence is non-authoritative.
3. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.7`.
4. Linux x64, Linux arm64, macOS x64, and macOS arm64 must each produce a
   `toka.release-gate` v2 report for the same SHA, with `source_dirty=false`,
   all thirteen stages passing, CTest at least 15/15, and Conformance at least
   298/298.
5. Each target must produce a passing TaskHandle Lifecycle v2 conformance
   record bound to the same candidate SHA and canonical contract digest.
6. After reviewing all four reports together, a maintainer may authorize and
   push an annotated `v1.0.0-rc.7` tag that peels exactly to the candidate SHA.
7. The tag workflow repeats the four-target gate and may create only a draft
   containing four native archives plus `SHA256SUMS`.
8. Replay every archive from a clean extraction through `toka doctor`,
   `toka new`, registry/path dependency resolution, `toka run`, strict offline
   lock replay, and `toka preview`. Record a hosted macOS Intel receipt.
9. Only after the audit contains immutable SHA, job, digest, and replay
   evidence may the protected promotion workflow publish the pre-release.

The evidence record belongs in
[`release_audits/v1.0.0-rc.7.md`](release_audits/v1.0.0-rc.7.md).
