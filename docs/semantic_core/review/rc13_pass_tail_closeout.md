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

## Enum payload cleanup dependency correction (incremental candidate)

The independent review of `215f225e` found that the common execution-dependency
walker skipped enum `SubMembers`. Its accepted ordinary/task root could therefore
still call an unassociated Token destructor from the old provider. This was
reproduced before the fix: retained drop text counted 2, but execution counted 1.
Counter-only evidence is preserved in
`validation/enum-cleanup-repro-20260923/`; no invalid-free probe was executed.

Correction: **`55f3ab10e413b23b71566d492e46561a800b3170`**, subsequently Accepted by
the independent incremental review recorded below. Only the common `InterfaceBody.h` traversal and
its regression matrix changed. Multi-payload slots are visited through their
resolved physical `SubMembers`, recursively; unit variants are skipped. Existing
unique/shared and array traversal remains, and raw/reference still stop before
the pointee. No syntax reconstruction, new ownership qualification, interface
policy/version, ABI, runtime or destructor-algorithm change is included.

The same traversal serves exporter body selection and consumer validation.
Regressions check:

- The original Token/i32 Pair against an unchanged old provider: checked drop 2
  executes locally; Empty cleans no payload.
- Two Token slots each clean once (count 4 when each checked hook adds 2).
- Struct → generic enum → unique/shared payloads: unique cleans at task completion,
  a remaining shared owner stays usable, and the last release cleans once.
- Enum raw/reference payloads do not require a pointee-drop body/association and
  leave the pointee live; only the caller's eventual owner cleanup runs.
- Removing the required drop body, association, or both rejects; normal/shadow
  diagnostics and object/LLVM no-artifact checks are retained. Every source-hidden
  check also verifies the provider object's bytes remain unchanged.

Fixed candidate validation:
`/Users/zhyi/GitDP/tokalang/validation/rc13-enum-cleanup-55f3ab10-r2/metadata.json`.
The tools build passed and selected CTest passed **5/5, 174.29 seconds**:
`toka_source_hidden_execution`, `toka_enum_payload_cleanup`,
`toka_stage1_indirect_parameter_cede`, `toka_task_result_projection`, and
`toka_rc13_tail_contracts`. Tracked source hashes remained unchanged during these
phases, including the independent RFC.

The separately preserved original retained-but-unassociated audit interface now
rejects in all four modes; stderr is identical in normal/shadow and no object/LLVM
artifact is produced. An initial validation-wrapper parity assertion mistakenly
compared shadow JSON stdout together with stderr. The stderr-only check was fixed
and those four controls rerun with `validation/rc13-enum-cleanup-original-controls.py`;
the already-passed CTest run was not rerun or relabeled. Raw logs are retained.
Compiler SHA-256:
`d24b502adb57fdce2264ecdc62c0b495ada863d9bed71f24cab52c1e934e0855`.

No full-suite refresh or historical-count backfill; no push, freeze, E, fn_, or
unrelated shared-Token/container work occurred during that validation.

## Source-hidden package Accepted — 2026-09-23

The user accepted **`55f3ab10`** for the bounded source-hidden executable-body
package. Independent evidence:
`/Users/zhyi/GitDP/tokalang/validation/source-hidden-cleanup-acceptance-20260923.IZ5g77/acceptance.md`.
The original missing-body/missing-association cases reject in normal/shadow and
object/LLVM modes; complete association executes the checked drop (count 2)
against the unchanged old provider. Independent CTest **3/3, 86.22 seconds**
included source-hidden execution, indirect parameters and enum cleanup.

This acceptance closes the package; no further features are appended here.
It does not accept RC13, opaque binary qualification or general recursive/container
storage. Full semantic replay is the next measurement, followed by the actual
remaining shared-Token/container cases. Historical full-suite counts remain
unchanged. E, fn_, main integration, push, freeze and publication remain paused.

## Complete replay closeout and fixed candidate — 2026-09-23

Implementation candidate: **`c8e3f3f1ea122a1eb7df364105214674b8843b72` (WIP,
not Accepted)**. The preceding documentation-only commit `631b8188` recorded the
accepted source-hidden package without adding features to it.

### Measured remaining cases and bounded corrections

The first complete replay on `631b8188` returned **51/56**, exit 1, 324.55 seconds.
It was not the previously estimated three-case remainder. Raw evidence is in
`/Users/zhyi/GitDP/tokalang/validation/rc13-replay-after-hidden-20260923/`.
The actual five directories and corrections are:

| Directory | First concrete boundary | Candidate change |
| --- | --- | --- |
| `async_p6_abi_cross_module` | Distinct core/std `__toka_detach_task` definitions collided; later CodeGen continued with incomplete state and crashed. | Rename only std's explicit-detach wrapper to private `detach_live_task`; retain core's reserved handle-drop hook. Stop subsequent CodeGen phases after a reported error. The collision guard remains; a dedicated object/LLVM negative proves exit 1 and no artifact rather than a signal. |
| `handle_004_morphic_handle_patterns` | The needed `Item.drop` body was unavailable and used provider-private global state. | Make its real payload generic (`Item<T>`, exercised as `Item<i32>`) under existing retained-template policy; use one explicit native drop-counter observer shared by provider and checked execution. Preserve runtime counts, double-move rejection and interface roundtrip checks. Do not copy private Toka globals or extend body qualification. |
| `iterator_002_owned_map` | Returning a new wrapper around a raw-backed Vec lacked complete result facts. | Use the Vec whole value as the owning iterator, retaining the public `VecIntoIterator<T>` spelling as a transparent alias. Borrowed iteration stays separate; element order, existing `remove(0)` cost and remainder cleanup are preserved. |
| `permission_005_partial_cede_lifecycle` | Explicit `new Token` with an actual borrowed counter field failed before the intended partial-move rejection. | Carry complete, validated explicit-field origins through dependent unique/shared allocation, binding/PAL registration and return lifetime checks. No independent-temporary proof is manufactured. The original E04632 negative now reaches its intended check. |
| `runtime_002_webhook_resource_graph` | Nested raw-backed owning containers exceeded the supported element proof domain. | Migrate this fixture's rule tree to owned flat preorder text and its exactly-two-bindings schema to two owning fields. Keep the outer dynamic `Vec<Hook>`, original consumer and all 32 rounds of content/layout/remove/reorder/roundtrip/clear assertions. |

The first two were newly exposed relative to the older replay measurement, not
silently folded into the anticipated three. No general recursive-container,
raw_take, byte-storage or source-hidden qualification was added.

The dependent-allocation path remains limited to normally validated scalar
`new` with exact declaration/type, source-explicit fields (not spread/default
completion), original pre-mutation snapshot, complete field plans and an admitted
group. Borrowed fields retain their actual referents and roots;
`TemporaryEligibility` remains `Ineligible`, not dependency-free. Return lookup
uses resolved binding identity, and typed owner selectors preserve dependencies.
Readonly-to-writable reference-field coercion rejects rather than manufacturing
payload permission. Tests cover local/parameter sources, same-name shadowing,
unique/shared cleanup, PAL conflict, rejected-initializer rollback and no artifact.

Vec's source seal was updated only because of the iterator library migration:
`3afa8fbcfbd5cf281119e0744636a5052ab5fd79e08d07f248b49de5438b011f`.
Layout and byte-storage operations did not change. The exact-byte qualification
contract remains bounded; byte-buffer and network regressions were rerun.

The replay runner's optional `replay_support.c` is compiled and linked into both
source-backed and source-hidden executions. Support compilation failure fails
the case, never skips it. This observer replaces the fixture's unavailable private
global, not the ownership behavior or expected cleanup counts.

### Fixed-candidate verification

Complete replay on the committed candidate returned **56/56**, exit 0,
**563.43 seconds**. It was also executed successfully again by the full PASS tail.
Evidence:
`/Users/zhyi/GitDP/tokalang/validation/rc13-replay-converged-c8e3f3f1/metadata.json`.
The preceding selected CTest run was **10/10, 541.22 seconds**; it is not used as a
substitute for the full run below.

Full-run evidence directory:
`/Users/zhyi/GitDP/tokalang/validation/rc13-replay-full-c8e3f3f1/`.
Its `metadata.json`, `comparison.json`, logs and tracked-source manifest bind all
phases to the same candidate and compiler. No implementation changes occurred
during any phase.

| Fixed-candidate command | Actual result | Wall time |
| --- | --- | --- |
| `cmake --build /Users/zhyi/GitDP/tokalang/builds/rc13-integration -j4` | All configured tool targets successful/up to date; exit 0 (not a fresh rebuild) | 0.59 s |
| `bash tools/scripts/test_pass.sh` | **459/459 programs; entire command including all appended checks exit 0** | 1375.22 s |
| `python3 tools/scripts/test_verify_fail.py` | **480/480**, exit 0 | 174.87 s |
| Unfiltered `ctest --output-on-failure -j2` in the same build | **123/123**, zero failed/skipped, exit 0 | 2149.13 s |

The PASS tail completed semantic replay, Outcome recheck, interface/cache checks
and the native incremental-build qualification (3 cycles, 31 modules); its final
result is `All Toka Incremental Build Tests PASSED!`, not merely a green program
count. Compiler SHA-256:
`d972266092f8a50cb642d40ac006e043713da1df0f7e9bbafeb7d033047a9352`.

Compared with the actual `ea7697eb` full run, program/FAIL counts remain
459/459 and 480/480, but the previously failing complete PASS command now succeeds.
The three old CTest failures restored are `toka_call_transfer_shadow_m1`,
`toka_non_call_transfer_shadow_stage0`, and `toka_stage1_indirect_parameter_cede`.
`toka_source_hidden_execution` and `toka_rc13_replay_boundaries` are **two added
tests**, not two recovered failures. There are no newly failing cases or abnormal
exits in the fixed run. Historical full-run numbers have not been backfilled.

The independent RFC remains unmodified by this work, SHA-256
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.
No debugger, main merge, push, publication or new freeze ref was used. E and fn_
remain paused. Green integration is evidence for this complete WIP candidate,
not a self-declared RC13 acceptance; separately recorded release limitations
(including extern aggregate lowering) are not erased by these results.

## Dependent-owner handoff P0 correction — 2026-09-23

The independent review correctly **did not accept `c8e3f3f1`** despite its genuine
green integration run. A shared copy/move or unique move into a second binding
lost the owner's carried borrow metadata: the handoff knew `input`/`local`, but
the subsequent return incorrectly reported `Dependency=None`. Review and original
non-executed dangling probes:
`/Users/zhyi/GitDP/tokalang/validation/rc13-final-review-20260923.S0XcMw/review.md`.
The previous measurements remain historical evidence, not acceptance.

Corrected implementation candidate: **`b3b67e00e8af4a0b0bb2c50bf550caefb6cf6bee`**,
including local WIP `5ad1cc16`; still pending consolidated review, not frozen.
The scope is the already validated whole-owner binding handoff:

- Resolve the checked source binding by identity before installing a potentially
  shadowing target. Preserve both `LifeDependencySet` and `FieldDependencySet`;
  do not replay an initializer or substitute a declared return dependency ceiling.
- After normal checking and the complete planner succeed, install those facts
  only at successful binding-transaction commit, for initialization and whole
  handle replacement. Keep existing ownership, permissions and cleanup behavior.
- Retain the source's existing PAL loan, including exclusivity, in the target's
  scope. Reject a shorter-lived referent; do not retire other owners' obligations.
- Snapshot owner borrow metadata by symbol ID, restore it on rejection and union
  reachable branch/loop dependencies. Failed handoff keeps the original target
  roots and restores source liveness; it does not claim runtime rollback.
- Preserve the same facts through supported transparent `unsafe`, cede, typed
  owner selectors and implicit/ascription wrappers. A known dependent source
  whose handoff cannot be associated is rejected, never silently made independent.

No CodeGen, runtime, ABI/TKI, reference-counting, raw_take or recursive-container
contract changes are included. No permission is derived from these source facts.

### Regression and execution evidence

`toka_rc13_replay_boundaries` now contains **69 named checks**: its existing 12
plus 57 owner-handoff checks, not 57 additional registered CTests. They cover all
three routes (shared copy, shared transfer, unique transfer), source order,
multiple hops, actual caller use, parameter-backed live returns, exact-once
cleanup, replacement cleanup, cross-scope lifetime/PAL retention, failed handoff,
branch/loop joins, same-name binding and transparent-wrapper controls.
Negative check/object/LLVM modes verify the intended diagnostic and no artifact;
normal/shadow diagnostics agree. Legal controls actually run. Return evidence
must retain the real `local` or `input` root, not merely reject for another reason.
Failed assignment's later return must retain `input` and exclude the rejected
RHS's `local`, with no spurious E0438/E0410/E0455.

Original audit files were independently rechecked without changing them or running
the invalid programs. All six direct/copy/move/caller probes reject with E0455,
normal/shadow parity and no object/LLVM artifacts. Preserved results:
`/Users/zhyi/GitDP/tokalang/validation/owner-handoff-originals-u4hyzvkm/`
(final candidate and compiler recorded in `metadata.json`).
The called two-field case now has `Structural` return dependency with both
`input` and `local`, rather than `None`.

During self-check, `unsafe ~owner` exposed an additional path through the same
handoff defect. The first full-run attempt on `5ad1cc16` was deliberately stopped
during PASS and marked incomplete in
`validation/rc13-owner-handoff-full-5ad1cc16/ABORTED.md`; no full result is claimed
for it. The wrapper correction and 24 corresponding positive/negative checks
are included in the final candidate. Final directed matrix: **69/69**; related
CTest: **7/7, 419.96 seconds** (`rc13-owner-handoff-r5.log` and
`rc13-owner-handoff-related-r2.log` under the validation directory).

### Complete fixed-candidate integration

Evidence:
`/Users/zhyi/GitDP/tokalang/validation/rc13-owner-handoff-full-b3b67e00/`.
`metadata.json` records the exact candidate, commands, compiler and unchanged
tracked-source hashes at every phase; `comparison.json` compares the genuine
`c8e3f3f1` full run. No implementation changes occurred during this final run.

| Phase | Actual result | Wall time |
| --- | --- | --- |
| All configured tools build | exit 0, up to date; not a fresh rebuild | 0.74 s |
| Complete PASS command including its entire tail | **459/459**, command exit 0 | 1403.66 s |
| Complete FAIL | **480/480**, exit 0 | 174.11 s |
| Unfiltered CTest | **123/123**, zero failed/skipped, exit 0 | 2222.60 s |

The PASS tail includes **56/56 semantic replay**, and finishes the native build
qualification (3 cycles, 31 modules). The owner-handoff CTest itself passed in
this full run in **182.64 seconds**. No newly failing cases or abnormal exits;
the P0 coverage is newly added to an existing test, not counted as recovery of an
old suite failure. Compiler SHA-256:
`77822eaf493bdf9b5b7600e6a8663ecebd890d1b31754068061bc22afa09ded8`.

The other worker's independent RFC retains SHA-256
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.
No main merge, push, publication, new freeze ref, E or fn_ work. This complete
candidate is ready for the requested incremental review; no peripheral features
are appended before the subsequent controlled main/release-preparation step.
