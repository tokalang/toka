# Toka 1.0.0-rc.8 Candidate Qualification Plan

**Status:** Preparation only. No immutable RC8 candidate SHA has been selected,
and `v1.0.0-rc.8` is not tagged, drafted, or published.

## Candidate scope

RC8 is a release checkpoint for the post-RC7 Init, Handle Grammar, borrowing,
miss-lookup, and `for alias` work. It does not start mutable PlaceIterator P2,
alias return, projection-place, consuming iteration, or async iteration.

The candidate keeps migration mode A:

- shared/read Array and Vec `for alias` use canonical `@PlaceIterator` and an
  internal exact-place carrier;
- writable non-array alias remains on its qualified compatibility carrier;
- existing `for auto &&x` and structural level-2 forms remain accepted for
  compatibility and are not a final 1.0 permanence promise.

## Compatibility boundary

- Public version label: `1.0.0-rc.8` / `v1.0.0-rc.8`.
- `.tki` metadata format: `3`.
- Compiler-interface compatibility key: `0.9.9-14`.
- Required place-yield ABI schema: `1`.
- All RC7 `.tki`, object, and semantic-cache artifacts are prohibited inputs.

## Required evidence before tag authorization

1. Resolve every must-fix item in the RC8 readiness audit and select a clean,
   immutable candidate SHA only after merge to `main`.
2. Run the commit-bound Handle Grammar full audit with zero unexpected source
   failures, zero admitted/lowered violations, all suites green, and the
   canonical PlaceIterator security redline passing.
3. Run isolated local prequalification for native macOS ARM64 and Docker Linux
   ARM64/AMD64 from the exact candidate revision. Local evidence is
   non-authoritative.
4. Dispatch `.github/workflows/release.yml` with
   `candidate_ref=<exact SHA>` and `tag_name=v1.0.0-rc.8`.
5. Linux x64, Linux arm64, macOS x64, and macOS arm64 must each pass the full
   release gate with `source_dirty=false`, the same candidate SHA, and the same
   version label. Windows/MSYS2 dogfood is Tier 2 feedback and does not block.
6. Review the four hosted reports together before authorizing an annotated
   `v1.0.0-rc.8` tag that peels exactly to the candidate SHA.
7. The tag workflow repeats the four-target gate and creates only a draft with
   four native archives plus `SHA256SUMS`.
8. Replay every archive from a clean extraction through `toka doctor`, project
   creation/run, path and registry resolution, strict offline lock replay, and
   semantic preview. Record the hosted macOS Intel replay.
9. Only after the audit contains immutable SHA, job, digest, and replay
   evidence may the protected promotion workflow publish the pre-release.

Evidence belongs in
[`release_audits/v1.0.0-rc.8.md`](release_audits/v1.0.0-rc.8.md).
