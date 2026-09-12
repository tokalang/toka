# Integration closeout after accepted JSON/YAML

Baseline: `2d2cbaf4`; accepted JSON `0f8c5408` and YAML `2d2cbaf4` stay closed.
This is an implementation/validation record, not acceptance of binding or RC13.
No push, PR, existing freeze-ref move, new ABI/TKI contract or recursive proof.

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
