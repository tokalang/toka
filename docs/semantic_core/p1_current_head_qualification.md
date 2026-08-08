# P-1 Current-HEAD Qualification Ledger

Status: **Historical qualification — `8d680fea4a9301cec21efc310a73a15ce4eb8157`
was the P-1 baseline at that revision.** This ledger records the exact
clean-worktree release gate that closed that P-1 instance; it is not a claim
that later PlaceState, TCB, manifest, or current-HEAD work is qualified. The
current bounded conformance requalification is recorded, including its
226-passed/0-failed closure, in
[`current_head_release_qualification_triage.md`](current_head_release_qualification_triage.md).
That closure does not replace this document's exact-revision thirteen-stage
package-gate evidence.

## Audit identity

- Revision: `8d680fea4a9301cec21efc310a73a15ce4eb8157`
- Date: 2026-08-05
- Target: `macos-arm64`
- Source: clean detached worktree; `source_dirty: false`
- Configuration: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, using the
  default release version `0.9.9-01`
- Runner: `tools/scripts/release_gate.py --target macos-arm64 --build-dir
  build`
- Report schema: `toka.release-gate` v2; `version_label: v0.9.9-01`

## Qualified evidence

| Gate | Result at this revision |
|---|---|
| release build | pass |
| release positive suite | 397 passed, 0 failed |
| negative diagnostic suite | 321 passed, 0 failed |
| warning suite | 1 passed, 0 failed |
| semantic replay, source-backed vs source-less | 32 passed, 0 failed |
| semantic cache invalidation | 13 passed, 0 failed |
| `@Encap` Slice 5 TKI v2 audit | pass |
| untrusted unsafe-TKI API revalidation | pass |
| tooling | 61 checks, 6 evaluation tasks; all pass |
| incremental build | pass |
| native/reference qualification | 100 cycles, 31 modules; pass |
| QSLite reference and source-less consumer | 300 operations, 10 corruption cases, 6 toolchain stages; pass |
| async fixtures | 6 passed, 0 failed |
| sanitizer reliability audit | 81 checks; pass |
| release-package smoke | 12 checks; pass |

The release gate completed all thirteen stages with exit code zero. In
particular, the package smoke used the same default release version as the
gate, so the packaged `tokac` and `toka` version checks are part of this single
qualified record rather than a later substitute run.

## P-1 exit conditions

The qualified run establishes the P-1 baseline conditions in the semantic
evolution roadmap:

1. current release, replay, TaskHandle/async, Encap/unsafe, and relevant
   pass/fail coverage run from one clean exact revision;
2. no release-gate crash, assertion, source/TKI disagreement, or unsound
   acceptance remained in that run;
3. the runner's revision, target, version, and per-stage results are recorded
   above;
4. delayed-initialization cleanup is covered by the passing suite; and
5. existing-destination transfer, partial-`cede` replay, and TaskScope/QSLite
   consumer paths are covered by the passing source and source-less gates.

## Scope boundary

P-1 qualifies existing behavior only. It does **not** close the proposed
PlaceState Core, the full bounded permission/partial-`cede` conformance
matrix, Async TCB Phase 5/6 conformance, async/place cleanup bridging,
Semantic Manifest Level B, Safe `unsafe` wrapper obligations, or protocol
capabilities. Those remain ordered by
[`semantic_contract_evolution_roadmap_rfc.md`](semantic_contract_evolution_roadmap_rfc.md).

The prior qualified run at `b937224aa3a3dc29978967097b40682ca0f6ceae`
precedes the bounded partial-`cede` repair and is superseded as the current
baseline by this record. The earlier blocked audit at
`f388fedb7a8d9f70ba49185f4ed2176f297174f1` is superseded as a current-HEAD
status record. It remains available through repository history as historical
failure evidence; it must not be used to describe `8d680fea`.

## Next action

Begin the internal PlaceState/permission-flow conformance audit from this
qualified baseline. That work first reconciles the exact-place state, cleanup,
and source-less replay representations; it introduces no new surface syntax.
