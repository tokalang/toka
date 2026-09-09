# Thread/sync closeout checkpoint — not Accepted

Base: `bcc49cb2d436297fef8cd177c9841678c1d14c2d`.
Scope: apply the expressly authorized std/sync writable-storage and in-place
cleanup diff, qualify that subset, and obtain one current integration baseline.
No capture/refcount/A/B protocol or compiler authority change is included.
This checkpoint does not accept its unaccepted binding base.

## Implemented and directed verification

- Mutex/RwMutex allocate and access writable storage using the explicit unsafe
  raw construction with nullable/same-source checking.
- Drop uses `free[1] *p`: destroy the initialized T, then free outer storage once,
  before native lock destruction. Initialization/extent remain unsafe duties.
- Scalar mutation/relock, Mutex/RwMutex resource Drop and remaining shared lock
  owner execute successfully. Readonly writes and live-guard owner discard are
  rejected, with no object or IR artifact.
- An armed, test-only allocator hook checks the allocation size and returns null.
  The actual public Mutex constructor aborts before dereferencing; it does not
  reach the fixture's return-99 failure sentinel. The Darwin hook must reside in
  an inserted dylib; executable-local interposition did not inject the fault and
  was corrected before recording a pass.
- Two normal/shadow diagnostic parity cases and four no-artifact checks pass.
- The existing public thread responsibility suite passes unchanged: 8 parity
  fixtures, 22 executions, source-hidden TKI/object execution, 10 source rejection
  and 10 fault no-artifact checks. This is the previously stated source subset.
- Incremental full build including toka, tokafmt and tokalsp succeeds.

The minimal g09_mutex caller migration now uses make_shared for a shared binding
and explicitly selects the shared capture handle. It remains a failing positive,
not a restored integration test.

## Still blocking thread/sync qualification

1. **Managed-element guard morphology.**
   `sync_managed_storage_pending.tk` is a positive exact-once target, but
   `Mutex<^Token>` / `RwMutex<~Token>` generic impl checking instantiates raw scalar
   guard casts `*^T`/`*~T` and reports E0492. No runtime cleanup claim is made for
   those instances. A storage/guard-view adaptation must preserve the complete
   element morphology; do not relax the raw/managed mixing check.
2. **Opaque lock storage in callable environments.**
   `sync_thread_pending.tk` reports binding IncompleteFacts for a closure owning
   a shared Mutex. Native handle/data Addr fields still lack the required actual
   environment dependency proof. @Send, an owning wrapper or the name Mutex is
   not a substitute for a storage witness. The old g09_mutex case additionally
   exposes a captured shared-handle local-copy TypeIncompatible path.
3. **Remaining public caller migration.**
   Live condvar/once/waitgroup and related thread examples still contain old
   callable representations, capture spellings and direct JoinHandle expectations.
   Restore their successful intent after the shared lock/environment root cause;
   do not blanket update negative or historical oracles.

## One complete PASS/FAIL baseline

On this checkpoint's implementation, without exclude/quarantine flags:

| Suite | Passed | Failed |
| --- | ---: | ---: |
| PASS | 311 / 451 | 140 |
| FAIL | 422 / 473 | 51 |
| Plain CTest | 50 / 55 | 5 |

Plain CTest ran to completion in 702.88 seconds (alongside the PASS/FAIL runs).
Its five failures are:

- call-transfer shadow: live g09_sync_condvar uses the old callable/API shape
  (ActualInvokeModeMismatch, then join attempted on Result).
- method parameter gate: out-of-slice failure no longer reports the historical
  E0438 expected by that fixture; same E04510/E0402 outcome on clean 50e78d16.
- indirect parameter gate: unique_named_requires_cede encounters earlier E04656;
  already reproduced on the clean base.
- return matrix: rebound_reference_unknown encounters earlier E04661;
  already reproduced on the clean base.
- remaining signature-driven routes: old thread_state_runtime source is rejected
  at the public handoff boundary with EnvironmentLifetimeUnproven.

The public thread subset, new sync storage subset, private source, adapter,
raw_take, raw permissions, Vec, enum cleanup and binding dependency targets all
pass in this same full CTest run. No failing target was disabled or skipped.

All 140 PASS failures were independently rechecked for their **first** diagnostic;
all are frontend rejections. No runtime failure is hidden behind this count.
The 51 FAIL mismatches all still reject: 0 unexpected successes and 0 compiler
abnormal exits (32 snapshot mismatches, 19 legacy expected-code mismatches).
They remain failures, not automatically blessable diagnostics.

First-diagnostic clusters (not a claim that every case has a final root-cause fix):

| First rejection | Count | Representative shared cause |
| --- | ---: | --- |
| AccessCapabilityMismatch | 70 | 64 start at sys/macos/net.tk:47: readonly raw formal → integer → writable target; 3 at btreemap.tk:25 |
| IncompleteFacts | 29 | 11 start at serde/json.tk:529 (`result!` assignment); other expression/alias/environment providers |
| RouteIneligible | 16 | 8 hashmap and 6 ring cases still extract raw elements using old `cede raw[index]` |
| TypeIncompatible | 10 | full morphology/captured-handle and source migration cases |
| ElementDependenciesUnproven | 8 | Vec raw_take instantiated with elements outside its proven dependency subset |
| ExplicitCedeRequiresSource | 3 | redundant explicit cede on temporary paths |
| ProjectedHandleRequiresSubroot | 3 | projected owner transfer without an ownership subroot |
| ContradictoryFacts | 1 | bitwise `~a` classified as a handle source |

These are finite integration buckets, not authorization to expand this thread
patch into networking, all raw containers or new ownership rules. The network
cluster must preserve actual raw input permissions; adding unsafe to a known
readonly source is not a valid fix. Container extraction needs its own correct
live-set/remainder handoff, not a NoSourcePlace disguise.

The bitwise and default-argument first errors reproduce unchanged on clean
50e78d16. The method out-of-slice, indirect-unique and return-reference historical
gate conflicts also reproduce there. This verifies those representatives as base
debt, not every failure in this table.

## Reproduction

Use this worktree as cwd and its source lib as TOKA_LIB, with the matching build's
bin/tokac as TOKAC. Run `test_pass.py` (CORES=4), `test_verify_fail.py` (no BLESS),
and plain CTest. The new directed test is `toka_std_sync_storage_subset`.

Local full logs and the 140-row first-diagnostic inventory are retained under
`/private/tmp/toka-sync-closeout.ATe0wv/` (`build.log`, `pass.log`, `fail.log`,
`ctest.log`, `pass-first-errors.jsonl`). No oracle, frozen ref or remote is changed.
No further full run is started merely to rediscover these same buckets.
