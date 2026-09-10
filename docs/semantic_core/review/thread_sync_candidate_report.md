# Thread/sync integration — Accepted; historical candidate evidence

Final status: the agreed slice was accepted at
`7ef33d9d73e6d604281aec5b116391e98e5592d5` and locally frozen. See the
[authoritative acceptance record](thread_sync_acceptance.md) for scope and the
distinction between the old full run and subsequent directed verification.
The candidate chronology and measured results below are retained unchanged.

Review update: `31843d5c` received Revisions requested for guard/unlock lifetime
and repeated shared-slot replacement. The subsequent bounded fixes and directed
revalidation are recorded in [the P1 follow-up](thread_sync_318_p1_fixes.md).
The full-run numbers below are the historical 31843d5c candidate evidence; they
have not been overwritten or represented as a new full run.

The original candidate implementation/report were recorded at `31843d5c`, based on `e90f360e`. The delivery
boundary is unchanged: thread/sync integration, not all binding or the cede RFC.
No push, PR, Actions, frozen-ref movement or interface/ABI change is included.

## Delivered responsibility chain

- The original shared-Mutex worker program runs unchanged. Condvar, Once and
  WaitGroup examples run with their original success assertions. Once performs
  three attempts with one initializer; WaitGroup waits for three workers and
  joins them. Native/composite allocation failure and exact-once cleanup gates
  remain active.
- Complete `^/~` handles can be replaced through qualified native guard slots.
  RHS preparation precedes old-element cleanup; shared copies retain before
  release, explicit transfers do not add a retain. Remaining shared owners
  remain live. No payload store substitutes for a handle store.
- Read-only binding/guard/pointee, active derived borrow, overlap, wrong
  morphology and invalid RHS reject. Source/slot state rolls back, with no
  E0438/E0410 introduced by the rejected assignment. Missing/mismatched plans
  cannot produce an object or IR artifact.
- Capture discovery re-elaborates inferred nested managed references and
  explicit-new bindings without duplicating type wrappers or changing the
  unique/shared conversion rules. Managed replacement uses its actual slot
  type as RHS context, not the enclosing callable annotation.

Details: [managed-slot implementation and matrix](managed_slot_closeout.md).

Final directed gates:

| Gate | Result |
| --- | --- |
| Public thread/sync closeout | 21/21 runtime/parity/denial checks, no skips |
| Shared parameter ABI (including added original regression) | 24 passed, 0 failed, plus carrier IR assertions |
| Managed-slot | 5 runtime/parity, 7 rejection/rollback, 12 fault no-artifact checks |
| Post-regression focused CTest | 3/3, 152.59 seconds |

The existing public thread responsibility gate also passed in the full run:
8 strict-parity programs, 22 runtime executions (including 15 fault/timing
modes), source-hidden TKI/object execution and its rejection/fault checks.
This does not claim expanded captured-thin-callable or general mutable-dynamic
qualification beyond the already accepted first-batch contract.

## Exactly one full comparison against `eedd0bf0`

External **Debug** compiler/test/tool build succeeded. This was not a fresh
four-platform release qualification. One complete PASS/FAIL/CTest batch was
run, with no exclusions or oracle changes:

| Suite | Baseline | Observed full run |
| --- | --- | --- |
| PASS | 311/451; 140 failed | 314/451; 137 failed; 284.68 seconds |
| FAIL expected diagnostics | 422/473; 51 mismatches | 422/473; the same 51 mismatches |
| CTest | 50/55; 5 failed | 60/65; the same 5 targets failed; 620.40 seconds |

The PASS list initially contained four recoveries and one added failure:

- Recovered: `g09_mutex.tk`, `g09_sync_condvar.tk`, `g09_sync_once.tk`,
  `g09_sync_waitgroup.tk`.
- Added: `g08_morphology_constraint_domains.tk` passed Sema but crashed at runtime.
- The other **136** remaining PASS failures are baseline failures with the same
  first diagnostic code/message and source file (line shifts excluded).
- All 51 FAIL mismatches remain rejected: no unexpected pass or abnormal
  compiler exit. Their mismatch membership is unchanged; no snapshots were
  blessed or bulk-migrated.

### Added runtime regression was fixed after the full run

The shared-parameter reception correction exposes the real shared carrier
address. However `&'value` still projected its payload when its validated result
type promised `&~T`. The caller then interpreted the payload address as a
carrier. CodeGen now selects the carrier for this exact morphic shared borrow;
ordinary `&shared` payload borrowing, ABI and refcount behavior are unchanged.

The **unchanged original PASS fixture** subsequently passed its normal harness
run, **1/1**. It is now a mandatory shared-parameter regression, with IR proving
that the shared specialization returns the caller carrier, not a payload
projection. Shared-parameter, managed-slot and public thread/sync CTests then
passed **3/3** on the corrected implementation.

Therefore no *known unclosed* added failure remains. Do **not** report 315/451
as a second measured full run: the only measured full result is 314/451, followed
by the recorded targeted correction. The resulting tracked PASS debt is 136
baseline cases, with four recovered cases. No second full suite was run.

### Existing CTest blockers retained, not repaired here

1. `toka_call_transfer_shadow_m1`: historical ordinary-cede replay still stops
   at the condvar source. Its error progressed from `ActualInvokeModeMismatch`
   to `EnvironmentLifetimeUnproven`; normal current-source condvar runs pass.
   This is an existing failed target/case with a changed diagnosis, not a claim
   that the historical protocol is now qualified.
2. `toka_stage1_method_parameter_cede`: the same out-of-slice rollback diagnostic
   expectation remains the first blocker.
3. `toka_stage1_indirect_parameter_cede`: the same
   `unique_named_requires_cede.tk` expectation remains the first blocker.
4. `toka_stage1_return_matrix`: the same `rebound_reference_unknown.tk` atomic
   E0455 expectation remains the first blocker.
5. `toka_signature_driven_cede_remaining_routes`: the same `thread_state_runtime.tk`
   source remains rejected with `EnvironmentLifetimeUnproven`.

These are the baseline's five failed targets; no failed target was added. Tests
after a harness's first blocker are not counted as proven passes. Broader
network/container/binding and historical-protocol debt has not been silently
pulled into this patch or reclassified as a new language restriction.

## Evidence and reproduction

Retained baseline directory: `/private/tmp/toka-sync-closeout.ATe0wv`.
Current logs: `/private/tmp/toka-thread-sync-final.VYn6ba`:

- `pass.log`, `fail.log`, `ctest.log`: original full outputs, unchanged;
- `full-comparison.json`: set-level added/recovered/remaining comparison;
- `pass-fail-comparison.json`: remaining PASS first-diagnostic comparison;
- `morphology.ll`: pre-fix bad IR; `morphology-focused.log`: original fixture's
  post-fix 1/1 run;
- `focused-after-regression.log`: post-fix 3/3 CTest;
- `final-tools-build.log`: final compiler-dependent tool rebuild.

`tools/scripts/compare_thread_sync_baseline.py` reproduces the comparison from
the saved logs. Its optional compiler argument performs only read-only frontend
classification of remaining failed PASS cases; it neither reruns whole suites
nor changes inputs/oracles. The original full-run regression remains visible
in the JSON instead of being overwritten with the follow-up result.

The original candidate was submitted for independent review and subsequently
corrected. Acceptance of the final thread/sync revision is recorded separately;
it is not a full Stage 1 completion claim, whole-binding freeze or release
qualification.
