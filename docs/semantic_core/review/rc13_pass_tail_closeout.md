# RC13 appended-check closeout — WIP

Preceding TOML/Template/callable corrections were accepted at `8057bfab` and
the SDK-root routing correction at `99c89a88` (acceptance recorded in `216b2548`).
This is a separate, not-yet-accepted package. E, fn_, pushing and publishing
remain paused; the independent RFC worktree change is preserved.

## Purpose-preserving changes

- TKI cache and replay fixtures use ordinary T syntax. The shared memory-summary
  fixture explicitly declares its global i32 storage and mutable-call intent;
  its global-write effect assertions remain. Both memory-summary and readonly
  gates pass without compiler exceptions.
- Reference Vec tests use the existing consuming `appended` API with the declared
  mutable receiver; no reference-consuming path was enabled.
- Native-build replay uses full copied source SDKs, no outside symlinks, and
  isolated source/cwd roots for both build and process tests. Its qualification
  runner uses the selected TOKAC/TOKA, not a missing checkout-local build.
- `toka preview` now locates the script through the selected source SDK as well
  as the installed SDK layout. It was previously failing before analysis because
  the script was absent, not because two semantic documents disagreed.
- Removed `-> cede T` signatures no longer produce a return-transfer contract.
  The cede evidence test checks the actual `return cede p` plan, exact p identity,
  source invalidation and Return destination. Consumer replay still checks the
  explicit caller-transfer contract and double-consume/use-after-move rejection.
- Two shared-permission negatives now expect the earlier binding-stage
  AccessCapabilityMismatch, with a checked reason string in both source and
  replay modes. A generic missing-cede test checks E04570 before any move rather
  than requiring leaked E0438 state.
- Cache discriminators now construct a valid value on each side of associated-
  type changes, and explicitly narrow an i64 when the consumer returns i32.
  They still compare before/after contracts and actual rejection. Cache: 13/13.
- Static-string wrapper replacement is a positive. Distinct dynamic owner
  replacement negatives retain E0442, for both Wait and await. No overbroad
  wrapper loan was restored.
- Old morphic payload-peeking helpers are replaced by whole-T transport helpers
  with reference/non-reference domains. Concrete consumers read fields, methods
  and indices after obtaining the actual handle. Explicit shared/raw/reference
  copy controls retain content and shared exact-once checks. The unique negative
  reaches a genuine second explicit move in a generic helper (E0438).

## Bounded implementation corrections

- A reference-valued field selected directly from a checked call result uses
  that field's completed formal-to-actual mapping, not sibling dependencies.
  Referents remain storage-level for &T, including &str descriptors. Whole live
  non-consuming arguments, exact resolved field types and existing unknown-
  source exclusions are required for the newly connected field-address case.
  No raw/recursive container independence is inferred.
- Fresh whole unique-owner initialization applies OWN-FLOW-01's local H/P
  rebuild, not the old binding's local P as a permanent ceiling. It is limited
  to direct checked unique bindings; pointee type, represented restrictions,
  aliases, PAL, liveness, overlap and normal validation remain checked. Other
  transfer destinations are not broadened by this change.
- Await already had task-result proof projection. Binding fact consumption now
  includes Await alongside Wait; missing or cancellation-captured proof is not
  manufactured.
- Complete init and successful Outcome init update tracked field state as well
  as whole-place state. Err arms remain uninitialized. Strong alias Outcome
  identity uses the stable alias declaration, not the underlying Packet. The
  dedicated gate checks source/hidden replay, actual execution and Err-arm
  rejection, while retaining all original sidecar/body tamper checks.

## Verification and accounting

Development logs live under `/Users/zhyi/GitDP/tokalang/validation/rc13-tail-*`.
Eight related CTest passed (334.63 seconds) during development. A later dedicated
contract matrix covers descriptor lifetime, unique ceilings/PAL, selected-member
borrows and static/dynamic async sources; final results are recorded below once
the candidate is fixed.

The first appended-check development run restored 21/22 scripts from 13/22.
Semantic replay remained failing (48/56 cases at that snapshot). Subsequent
morphic fixture changes are not backfilled into those numbers. Original logs:
`validation/rc13-pass-tail-closeout-r1/results.json` and per-script logs.

Outstanding replay cases are retained, not blessed: source-hidden callable/task
result qualification, VecIntoIterator result storage, nested raw-backed resource
graphs and additional partial/async paths. An isolated bind-before-return Vec
probe still rejects IncompleteFacts; it was not applied to the SDK or its sealed
byte-contract fingerprint. These must not be called mere spelling migrations.

No new full baseline or package acceptance is claimed at this WIP checkpoint.

The dedicated contract matrix subsequently passed all 14 cases, including
normal/shadow agreement, object/LLVM no-artifact checks and real positive runs
(`validation/rc13-tail-discriminating-r4.log`). Generic whole-value transport
controls also run, and the unique negative rejects the actual second move.
At that development checkpoint, fixed-candidate all-22/full results were still
pending; those directed results do not replace the full results recorded below.

## Fixed candidate results

Fixed implementation/test tree: `ea7697eb2c20fd913c29fe7b798455a8f2dab8cd`
(following WIP implementation `beaa9e62`). Evidence directory:
`/Users/zhyi/GitDP/tokalang/validation/rc13-pass-tail-final`.
All phases reported zero tracked-source changes. The unrelated RFC's preserved
SHA-256 remains `1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.

| Phase | Actual result | Seconds | Exit |
| --- | --- | --- | --- |
| Complete tools build | passed | 0.63 | 0 |
| All appended checks, including after failures | 21/22 scripts | 661.45 | 1 |
| Full PASS command | 459/459 programs; replay tail fails | 739.36 | 1 |
| Full FAIL suite | 480/480 | 134.51 | 0 |
| Unfiltered CTest | 118/121 | 1506.44 | 8 |

Compiler SHA-256:
`4250fd2aa9ef23c66babb5a4136ef196cdbfa29e58bb8f82f40472b641e7f2e5`.
Full commands, manifests and comparison are retained in metadata.json,
source-manifest.json, preserved-worktree.patch and comparison.json.
The comparison baseline is the earlier actual full run at `123a3aa4`, not a
retroactively attributed full run of the accepted callable correction.
No newly failing PASS/FAIL cases or abnormal exits were recorded.

Two added CTests (callable shadow and tail contracts) are not recoveries. Two
old CTests failed because new fixture comments shifted the frozen line anchors:
`toka_call_transfer_shadow_m1` and `toka_non_call_transfer_shadow_stage0`.
The former found no transaction at line 7; the latter found no matching rejected
assignment at its old line, not a newly admitted shared permission amplification.
The underlying call/assignment were left intact and the comments consolidated
to preserve their original line numbers. After the full run, those two gates
passed **2/2, 48.84 seconds** (`validation/rc13-tail-line-anchors.log`). No planner
assertions changed. This incremental result is **not** a rewritten 120/121 full
CTest result. The other full CTest failure remains the existing source-hidden
`toka_stage1_indirect_parameter_cede` qualification gap.

### Remaining appended check: semantic replay

Fixed replay is **49/56 case directories**, not 49 individual programs.
All 22 scripts were attempted; source-hidden failures were not skipped.
Exact results and logs: `validation/rc13-pass-tail-fixed/results.json` and its
per-script logs. Compared with the earlier 13/22 ledger, eight scripts recovered:
TKI cache, memory summary, readonly, cede evidence, preview, Outcome body recheck,
semantic cache invalidation and incremental build.

| Remaining case directory | Observed boundary / first failure |
| --- | --- |
| async_suspend_001_return_deps | Source-hidden task result proof unavailable; source-backed static/dynamic controls pass |
| callable_001_modes | Source-hidden callable factory environment unavailable |
| ergonomics_002_closure_dependencies | Source-hidden closure environment unavailable; original move/lifetime targets remain required |
| iterator_002_owned_map | VecIntoIterator<i32> construction/return storage facts incomplete in the provider |
| permission_003_independent_flow | Async source-hidden TaskHandle result qualification; synchronous unique-flow controls pass |
| permission_005_partial_cede_lifecycle | make_pair(global counter) rejected before partial move / cancellation is reached |
| runtime_002_webhook_resource_graph | Recursive Vec<TriggerRule> / nested resource element dependencies unproved |

No declaration-only function type was made an environment-independent witness.
No general Vec/recursive raw-storage contract, source-hidden protocol or E was
introduced to eliminate these failures. The original failing targets remain.

### Additional isolated remaining-case diagnosis (outside the fixed tree)

`validation/rc13-tail-partial-async-probe/` preserves the actual Pair/Token
provider and local/global counter controls. Merely adding i32 and explicit #
to the async global-counter case does not fix it. The non-async **local** counter
control compiles/runs (including two destructor increments); the same **global**
counter control rejects IncompleteFacts. This localizes another source-identity/
fact boundary, rather than proving the whole Pair representation invalid or
claiming an introduction date. No source-global exception was added.

The isolated Vec iterator bind-before-return attempt also still rejects, so it
was not promoted as a library migration or used to alter the sealed byte SDK.

This package is not fully closed or Accepted. E, fn_, push, freeze refs and
publication remain paused. The global-counter boundary and the remaining
source-hidden/storage capabilities need their own concrete resolution; they
are not hidden by changing the existing runtime objectives or the old oracles.

## Incremental review and next-boundary WIP

The user's independent review accepted the incremental corrections, not the
whole package: four selected gates passed **4/4, 104 seconds**, the complete
Outcome script passed, and an additional two-source projection control kept the
local-source escape rejection. Audit:
`/Users/zhyi/GitDP/tokalang/validation/pass-tail-review-20260922.xziRE1/review.md`.
The fixed full baseline above remains **118/121 CTest**; line anchors belonged
to two existing frozen gates. No incremental successes are backfilled.

### Global declaration source correction

Global symbols already had their actual declaration pointer, but omitted
`DeclLoc` in primary/import registration and lexical previews. AccessPath uses
that location, and Stage 0 refuses a root without a valid declaration location.
The correction copies the actual declaration location through those five paths.
It does not add a static/independent flag, change binding permission, introduce
Send/Sync evidence, unify unrelated SymbolIDs or alter import alias state.

After rebuilding tools, the expanded existing tail-contract driver passed
**18/18 cases**, with normal/shadow, object/LLVM and positive execution checks.
Added controls cover global/local same-name drop counters, readonly-global write
refusal, a same-named local counter escaping with E0455, and the unchanged
original async cancellation program. The latter also passed a separate actual
source-hidden compile/link/run against a freshly emitted provider object.

Evidence:
- `validation/rc13-global-source-final-build.log`
- `validation/rc13-global-source-tail.log`
- `validation/rc13-global-source-hidden.py` and `.log`

The entire `permission_005_partial_cede_lifecycle` directory is **not recovered**.
Its directed replay run stops at `fail_shared_record_partial_cede_rejected`:
`new Token(... &drops ...)` reports E04661 IncompleteFacts before the intended
E04632 partial-move rejection (`validation/rc13-global-source-replay.log`). The
original assertion/source is unchanged; this run does not establish when that
remaining defect was introduced. The individual cancellation success cannot
replace the directory-level failure.

### Source-hidden decision, not production activation

Fresh interfaces and controlled ordinary/generic factory probes distinguish
missing bodies from retained, recheckable bodies. An arithmetic-only probe
also demonstrated that a retained ordinary body can be checked while the old
provider object executes instead. Merely retaining more body text is therefore
not the proposed authority fix. The exact observations, limited recommended
execution association, alternative trust boundary and combined matrix are in
`rc13_source_hidden_qualification_proposal.md`.

At that checkpoint no source-hidden proof serialization, body-retention activation
or CodeGen qualification widening had been applied. The concentrated policy
decision was subsequently authorized on 2026-09-23 (implementation below).
Container-return
cases remain recorded above, with no recursive/byte/raw_take expansion. These
are development results, not a new full baseline or package Accepted status.

## Source-hidden execution package: fixed candidate ready for review

Implementation candidate: **`215f225edfa21a717a8fac54bb02cde64b676252`**, following
WIP checkpoints `cb46eda8` and `a4b47072`. Status is **WIP / awaiting combined
review**, not Accepted, frozen or RC13-qualified.

The approved body-backed route is implemented: interface policy `checked-local-v1`
binds specific retained declarations; ordinary bodies and qualified concrete
instances execute locally with internal linkage. Actual selected AST edges,
clone definition origins, generated invokes and real custom-drop declarations
are validated as one dependency closure. A merely present body, an incomplete
helper, an unselected cache probe, an escaped function identity or a symbol
collision cannot substitute for that association. Existing retained templates
receive deferred associations, not unconditional scalar-instance qualification.
Native layout/runtime, refcounts, raw_take, byte-owner facts, E and fn_ are unchanged.

Fixed evidence directory:
`/Users/zhyi/GitDP/tokalang/validation/rc13-source-hidden-215f225e/`.
Reproducible driver:
`/Users/zhyi/GitDP/tokalang/validation/rc13-source-hidden-215f225e.py`.

| Verification | Actual result | Seconds |
| --- | --- | --- |
| Complete tools build | pass | 0.79 |
| Selected CTest | **7/7** | 209.74 (CTest); 209.87 wall wrapper |
| async_suspend_001_return_deps replay | pass | 18.16 |
| callable_001_modes replay | pass | 3.57 |
| ergonomics_002_closure_dependencies replay | pass | 3.46 |
| permission_003_independent_flow replay | pass | 13.82 |
| TKI cache validation | pass | 28.57 |
| Unsafe-TKI anti-forgery | pass | 6.50 |
| Full Outcome body recheck script | pass | 12.86 |
| Thread interface/cache compatibility | pass | 2.09 |

The seven CTests are the new `toka_source_hidden_execution` plus existing
`toka_stage1_indirect_parameter_cede`, `toka_task_result_projection`,
`toka_rc13_tail_contracts`, `toka_call_transfer_shadow_m1`,
`toka_non_call_transfer_shadow_stage0` and `toka_reference_domains`.
No tracked-source changes occurred during any phase. Compiler SHA-256:
`c2e67e1177bd047b6568505dccc1576a442ad01bb91fab602524f532f203f20e`.

Key runtime evidence: interface `+10` yields **14** against the old `+1` provider,
also without that provider. Primed generic function and generic impl providers
cannot override the checked instances; IR asserts internal linkage. Owned capture
sharing cleans once; changing the checked drop body changes the observed cleanup
while still using the old provider. External task sources survive task cleanup;
second-argument mapping/cache reuse does not permit local or task-frame escape.
Wrong/missing policy and definition associations, missing/invalid/recursive
helpers and unavailable private globals reject without object/LLVM artifacts.

The old task-result `source_hidden` expectation was intentionally split: the
retained executable producer is now a real runtime positive; an explicitly
declaration-only producer remains negative. Its rollback counterpart asserts
`TaskResultOriginsUnproven` at `consume(cede owner, task)` in normal, object and
LLVM modes, and no moved/uninitialized follow-on error. No early unrelated failure
is counted as rollback coverage. The interface/cache version is `0.9.9-23`;
version tests retain stale-interface rejection and source fallback/runtime checks.

No full PASS/FAIL/CTest or all-22 refresh was performed in this package. The old
fixed **118/121** CTest and appended-script figures remain historical, not
backfilled from these results; the new CTest is an addition, not a recovery.
The shared-Token early rejection within `permission_005_partial_cede_lifecycle`
and the two container-return directories remain separate. No main merge, push,
freeze or publication occurred. The other worker's RFC remains untouched at
SHA-256 `1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.
