# Integration closeout after accepted JSON/YAML

Baseline: `2d2cbaf4`; accepted JSON `0f8c5408` and YAML `2d2cbaf4` stay closed.
This is an implementation/validation record, not acceptance of binding or RC13.
No push, PR, existing freeze-ref move, new ABI/TKI contract or recursive proof.

The batch was accepted at `db4385dc` by independent incremental review, including
the three added gates (3/3, 19.16 s); the reviewer did not rerun full suites.
Next priority is the 38 remaining programs whose actual first error was Ring,
not filename-based synchronization/network groups. They are a shared blocker,
not a promise that all 38 programs recover after removing that first error.

Actual checkout: `/private/tmp/toka-b6-dynamic-20260912`, branch
`impl/json-dynamic-index`, accepted starting revision
`db4385dcb02e73b9424fafee185bc11ccc4c71f6`. Directory migration is separate work;
no checkout, older return-matrix branch, or existing freeze ref is moved.

## Implemented shared causes

- Darwin network adapters now construct a complete `sockaddr_in`-compatible
  value and local scalar socket options, instead of pointer arithmetic and
  readonly-to-writable allocation casts. Native declarations remain unchanged.
  A C ABI shim verifies size/offsets/bytes and scalar outputs; actual UDP
  loopback verifies send/receive, peer address/port and `SO_ERROR`.
- Channel-backed host mailbox moved unchanged to `std/task_mailbox`. The core
  executor no longer imports optional channel/sync dependencies. Call sites and
  public module documentation migrate; this does **not** claim mailbox/channel
  failures are fixed. Detached-task collection copies the opaque address under
  the existing queue lock rather than retaining an element reference.
- Current std/thread integration fixtures use the public spawn Result contract,
  owned dyn-fn environments and explicit transfers. The two route runners use
  current semantics only for current-library integration; historical compiler
  fixtures keep historical replay. Condvar receipt/transaction checks still
  require the original unique wait site, found by source content rather than a
  stale line number. The condvar program itself is unchanged.
- Fourteen negative fixtures now reach their intended error without cascades
  through bindings whose initialization was rejected. The new purpose gate
  checks exact error-code sets, normal/shadow parity, no object/IR on rejection
  and an admitted minimal correction. Six snapshots changed after those checks;
  no global blessing or acceptance of an unrelated first error.

## Independently proved compiler defect

A source-visible provider returning a capture-free `fn(cede i32) -> i32`
compiled successfully but its consumer crashed with SIGBUS. Provider IR loaded
an empty closure object into an uninitialized `{env, invoke}` return carrier.
The minimal provider/consumer and original IR are retained with the logs below.

The bounded fix constructs both carrier fields for an actually empty closure,
using permanent empty environment storage and the generated invoke function.
Nonempty/unqualified direct thin-fn closure returns reject; no captured
environment allocation/lifetime extension, dynamic-fn change or TKI expansion.
The original source-visible consumer now runs successfully. Direct, branched
and unsafe-wrapped returns pass the new runtime/IR gate; capturing escape has
no object/IR. Related authority, dyn-fn and return gates pass 4/4.
Source-hidden factory environment metadata remains a separate unresolved issue.

## Measured integration result

Logs: `/private/tmp/toka-integration-closeout.VIO9Zx`.
Baseline logs: `/private/tmp/toka-yaml-integration.OUAvki`.

| Suite | Baseline | Current | Recovered | Added failures |
| --- | --- | --- | --- | --- |
| PASS | 336/451 | 366/451 | 30 | 0 |
| FAIL expectations | 423/473 | 438/473 | 15 | 0 |
| CTest | 83/87 | 88/90 | 2 existing + 3 new passing gates | 0 |

PASS is the full compile/run suite (232.96 s), not only check-only probes.
The fifteen FAIL recoveries are the fourteen purpose migrations plus unchanged
`task_group_cancellation_post_1_0.tk`, which now reaches its expected error after
the dependency fix. No negative unexpectedly passed or exited abnormally.
Incremental complete tool build succeeded. Full CTest completed in 643.88 s,
88/90. The recovered existing tests are `toka_call_transfer_shadow_m1` and
`toka_signature_driven_cede_remaining_routes`. Three new passing gates cover
network adapters, diagnostic purposes and empty-fn returns. The remaining two
are `toka_stage1_indirect_parameter_cede` and `toka_stage1_return_matrix`; their
failures remain recorded, with no skip, oracle update or release exception.
`comparison.json` in the log directory contains all added/recovered/remaining
test names for all three suites. Existing frozen refs and RC13 are unchanged.

## Explicit remaining boundaries

- Ring raw-element extraction is not modified. The proposed raw_take rewrite
  was rejected by execution review over generic slot retirement/remainder
  responsibility. It was not applied or retried through another mechanism.
  Any continuation of that exact approach requires a complete reviewed diff
  and the necessary execution approval; ordinary `unsafe` is not evidence of
  initialized storage. This batch does not extend raw_take admission.
- Remaining return-matrix container/codec failures and source-hidden callable
  metadata remain failures, not newly blessed negative expectations.
- Remaining native storage/container problems are not folded back into the
  accepted thread/sync, JSON or YAML slices.

The remaining 85 PASS failures and 35 FAIL expectation failures are still
release blockers. This batch is a reduction in measured failures, not a
release-ready declaration.

## Ring continuation (library-only candidate)

Ring's raw-retirement proposal was again rejected before application. Rather
than retrying it indirectly, the candidate changes representation to two
complete-value Vecs, with balanced refills. No Ring raw_take/alloc/free remains;
no compiler, Vec, native sync, raw_take or ABI/TKI implementation was changed.
See `docs/std_ring.md` for owner transitions, performance, removed raw-field
interface and destructor-order differences. This is not an Accepted declaration.

The eight runtime matrix cases and two rejecting cases pass. A 400-operation
seeded reference model checks contents/get/len after each operation. Resource,
unique and shared tests verify no early destruction during growth/refill and
exact-once after removal/clear/early return; a separate shared-owner case stays
usable. String pressure, unique and shared-owner executables each report
0 leaks / 0 leaked bytes through macOS leaks (outside the task-port sandbox).
The original conformance Ring resource test also compiles and runs unchanged.

Six related CTests pass 6/6 (78.24 s): Ring, Vec pop, managed elements, iterator
domains, accepted thread/sync, and call-shadow. The final Ring gate was rerun
after ensuring model-test error returns cannot truncate to zero exit status;
it passes 1/1 (26.91 s). The complete incremental tool build also succeeds.

Rechecking exactly the 38 remaining Ring-first-error cases:

- Six now compile and run: cede_exemptions, g07_ring_test,
  g07_test_advanced_containers, g08_sync_mpsc, g09_async_context_smoke_tests,
  g09_context.
- Thirty next fail at `lib/std/net.tk:1201`, readonly raw out_key rebound as a
  writable pointer. This is a newly exposed **actual** shared first error,
  not a Ring fix or a claimed network recovery.
- Two MPSC tests next fail on old `fn#` thread closures/public spawn usage.
  They are still failed positive tests, not intentionally unsupported behavior.

Logs and exact case records: `/private/tmp/toka-ring-closeout.m8aiab`.
The current full FAIL run is 438/473, identical failing names, no newly admitted
negative or abnormal exit. Full PASS is 372/451 (204.32 s): six recovered, zero
added failures, 79 remaining. `comparison.json` records both full-suite deltas
against `db4385dc`'s logs. The full CTest suite was not rerun; only the six
directly related gates above are claimed for this candidate. The prior full
88/90 result remains historical, with its two distinct unresolved failures.
