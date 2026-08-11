# Toka 1.0.0-rc.1 Candidate Qualification Plan

**Status:** Published historical candidate. `v1.0.0-rc.1` is the annotated
tag at `05ba085252977d945d5f4e11ecfad363147388d8`, published as a GitHub
pre-release on 2026-08-10. This plan records that candidate's qualification
boundary; it does not qualify later commits on `main`.

## Decision

On 2026-08-10, maintainers chose to begin `1.0.0-rc.1` directly. No new
`0.9.9` tag was created after the historical `v0.9.9-rc4` pre-release. RC1 is
a GitHub pre-release and does not replace the latest stable SDK selection.

This decision supersedes the proposed `0.9.9-02` adoption-release sequence in
[`0_9_9_release_plan.md`](0_9_9_release_plan.md), not its historical evidence.
The language surface remains frozen by
[`1_0_freeze_decision_list.md`](1_0_freeze_decision_list.md).

## Candidate Boundary

- Public compiler, SDK, package, and diagnostic labels are `1.0.0-rc.1`.
- `TOKA_COMPILER_INTERFACE_VERSION` remains `0.9.9-02`: it is the independent
  `.tki` / semantic-cache compatibility key, and a release-label change alone
  does not justify invalidating source-less interfaces.
- No new language syntax, TKI format, or runtime contract is introduced by
  this candidate preparation.
- `toka test` is explicitly Preview for RC1. It is an experimental
  source-tree scanner, not the stable project-test contract and not release
  evidence; packages continue to name their own qualification command.

## Pre-publication evidence boundary

1. The candidate version migration and launch documentation are committed and
   pushed to `main`. `tokac`, `toka`, `tokafmt`, and `tokalsp` agree on
   `1.0.0-rc.1` after an ordinary source-tree reconfigure. Normal CI remains
   green on Linux x64, Linux arm64, and macOS arm64, with Windows dogfood kept
   as non-blocking Tier 2 evidence.
2. The default installer resolves the latest stable **Toka SDK**, not a package
   release. The installer must fail before replacing an existing SDK, and its
   documentation must give the exact `v1.0.0-rc.1` invocation for RC testers.
3. `.github/workflows/release.yml` is manually dispatched at that exact
   revision with `tag_name=v1.0.0-rc.1` and `publish_release=false`.
4. Linux x64/arm64 and macOS x64/arm64 each produce a clean
   `toka.release-gate` v2 report with all thirteen stages passing.
5. The four reports, their revision, `source_dirty=false`, and their version
   label are reviewed together. Historical RC4 or 0.9.9 evidence cannot
   substitute for this step.
6. Only then may maintainers separately authorize creation of annotated
   `v1.0.0-rc.1` and its GitHub pre-release archives.

The published release and its four platform archives are available at
[`v1.0.0-rc.1`](https://github.com/tokalang/toka/releases/tag/v1.0.0-rc.1).
Any commit after `05ba0852` requires a distinct candidate label and a new
four-target gate; an existing RC tag is never moved or reused.

## Evidence Before Announcement

After the tagged archives exist, install the exact RC tag into a clean home on
each available native release host. `toka doctor`, `toka new`, `toka add
regex`, `toka run`, and an offline lock replay must succeed. This is the
public developer's first-hour path, distinct from the compiler release gate.

The release remains a GitHub pre-release: the unqualified installer continues
to select the latest stable SDK until final 1.0 publication. The RC's public
entry points must therefore use its explicit tag, and its package replication
guide must describe the reviewed GitHub Release plus static-catalog path rather
than an unauthenticated registry upload.

## Adoption Evidence

The former adoption goals remain useful RC evidence: two representative
applications, the integration boundaries they actually require, and
AI-assisted editing against checked-in tooling contracts. They are gathered
during the RC period rather than represented by an unqualified 0.9.9 release.
They do not reopen the frozen language surface.
