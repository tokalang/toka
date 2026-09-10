# RwMutex unlock follow-up to 9c36b72f — Accepted

Accepted revision: `7ef33d9d73e6d604281aec5b116391e98e5592d5`.
The user's final independent review passed the original counterexamples and
3/3 incremental CTest in 153.94 seconds, closing the remaining P1. The agreed
thread/sync slice is now closed; see [the acceptance record](thread_sync_acceptance.md).

This revision addresses only the reviewed RwMutex omission. The shared-slot P1
remains closed and is not reopened. No new unlock protocol, runtime ABI, capture
or refcount behavior is introduced.

## Fix

`rejectNativeSyncUnlock` previously filtered guard owners to `Mutex` alone.
The explicit admissible kind set now includes `RwMutex`. Both read and write
guards (and Results that may contain either) therefore use the existing exact
owner identity, resolved unlock declaration, liveness and rejection-rollback
checks. No distinction based on payload write permission is used to excuse a
read guard's unlock responsibility. Other native kinds are still excluded.

The production change is confined to that kind check. Existing temporary-guard
tracking, expression checkpoints and state propagation are reused unchanged.

## Incremental verification

Unmodified reviewer files were replayed with explicit workspace coordinates:

- `rw_double.tk` and `rw_read_double.tk`: normal/shadow diagnostics match;
  `ActiveGuardOwnsUnlock` rejects both object and IR emission.
- `rw_single.tk`: actual native unlock count remains **1**.

The new `toka_native_sync_rw_unlock` gate covers:

- Fifteen rejection/parity/rollback cases: read/write direct, qualified call,
  shared alias, moved guard, pending Result, temporary guard/Result, and two read
  guards with one explicitly dropped while the other remains live.
- Six check-only controls for released/out-of-scope guards and different owner
  instances. These do not claim that unlocking an unlocked POSIX object is a
  valid runtime operation.
- Four native-counted runtime controls: write once = 1, read once = 1, two read
  guards = 2, write dropped then read acquired = 2. The existing portable unlock
  counter hook now includes rwlock entry points; Linux link wrapping is updated.

Only the RwMutex gate, existing native witness gate and public thread/sync
closeout gate are selected for final related CTest. Logs are retained in
`/private/tmp/toka-rw-unlock-review.wHxHIo`. No full suite is rerun and no oracle
or earlier full-run record is rewritten. These are implementation-run records;
the final independent acceptance is recorded above.

Final related CTest passed **3/3 in 154.59 seconds** (`related.log`); the RwMutex
gate itself took 84.56 seconds. Incremental compiler/tools build and
`git diff --check` passed. That candidate was `7ef33d9d`; its later acceptance
does not change these measured results or imply a push.
