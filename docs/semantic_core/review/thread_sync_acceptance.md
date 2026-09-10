# Thread/sync integration — Accepted and locally frozen

Status: **Accepted** — limited to the agreed thread/sync integration slice.
Accepted revision: **`7ef33d9d73e6d604281aec5b116391e98e5592d5`**.
Authority: the user's final independent acceptance of that revision; the
remaining RwMutex unlock P1 was closed, with no new blocker found in that review.
Local freeze tag: **`freeze/thread-sync-7ef33d9d`**, targeting the accepted
implementation revision, not the later documentation-only acceptance commit.

This is the authoritative closeout record. Earlier candidate/WIP/revisions-
requested notes are retained as history and do not supersede this acceptance.
The acceptance commit changes documentation only and retains `[skip ci]`.
No push or PR is authorized or performed by this closeout.

## Accepted scope

- The agreed std/thread first-batch source qualification and A/B responsibility
  handoff: environment/result cleanup, spawn failure, join/detach/drop, and
  the previously specified failure/timing matrix. Existing qualification
  boundaries are preserved, not expanded by this acceptance.
- Mutex/RwMutex/CondVar and Once/WaitGroup integration: exact owner/storage and
  native child qualification, guard lifetimes, permitted access/replacement,
  construction-failure cleanup and exact-once terminal cleanup.
- The original shared-Mutex worker path and migrated condvar/Once/WaitGroup
  success scenarios, including the agreed managed-slot replacements of complete
  unique/shared handles, repeated shared replacement and remaining-owner life.
- Same-owner explicit unlock rejection while a Mutex or RwMutex read/write
  guard, possibly guard-bearing Result, or tracked temporary guard remains live.
  Exact resolved declarations/owner identity and rejection rollback are retained;
  no new manual-unlock protocol is introduced.

The slice is closed. Further work is not to be appended under thread/sync
closeout merely because it is found nearby.

## Evidence: preserve full baseline versus later directed verification

One full comparison was run against `eedd0bf0`; the following are the measured
historical results, **not a new full run at the accepted revision**:

| Suite | eedd0bf0 baseline | Single full comparison |
| --- | --- | --- |
| PASS | 311/451 | 314/451 |
| FAIL diagnostic expectations | 422/473 | 422/473; same 51 mismatches |
| CTest | 50/55 | 60/65; same 5 failed targets |

That comparison recovered four PASS cases and exposed one added runtime crash.
The crash (`g08_morphology_constraint_domains.tk`) was subsequently fixed and
passed its unchanged original harness case, followed by 3/3 focused CTest.
The tracked remaining PASS debt is 136 baseline cases. **315/451 is not reported
as a measured second full run.** No negative-test oracle was bulk rewritten.

The later guard/unlock and repeated-slot P1 corrections passed 7/7 related
CTest (implementation run: 188.82 seconds). RwMutex coverage was then corrected
at the accepted revision:

- Original read/write double-unlock cases reject consistently in normal/shadow
  modes, producing neither object nor IR.
- Original single-unlock control still counts exactly **1** native unlock.
- Implementation's final incremental CTest: **3/3**, 154.59 seconds.
- User's final independent incremental CTest: **3/3**, **153.94 seconds**.
- The user's final review closed the remaining P1 and found no new blocker.

The final acceptance is based on that full-baseline-plus-directed-revalidation
history. No new full suite or release-qualification run is claimed by this
documentation-only closeout.

Evidence details and retained log locations:

- [Full candidate/baseline report](thread_sync_candidate_report.md)
- [Guard and repeated-slot P1 corrections](thread_sync_318_p1_fixes.md)
- [Accepted RwMutex correction](thread_sync_rw_unlock_followup.md)
- [Managed-slot implementation and matrix](managed_slot_closeout.md)

## Explicitly outside this acceptance

This does **not** accept the entire binding slice, all Stage 1 routes, the cede
RFC, or release qualification. Remaining generic/morphic/identity routes,
other non-call activation, receiver/callable work, protocol cleanup, and broader
network/container/binding debt require their own scheduling and scope. The
historical five CTest blockers and 51 diagnostic mismatches are not declared
fixed. No follow-on implementation is authorized by this recording step.
