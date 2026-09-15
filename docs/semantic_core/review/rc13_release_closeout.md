# RC13 release closeout after G

G remains accepted at `4005a68e`, recorded by `e3102120`. E is not started.
This log does not revise the G RFC or replace full-suite results with targeted runs.

## Shape inference crash recovery

The unchanged `tests/pass/g09_context.tk` previously terminated with SIGSEGV
after five Option return/discard diagnostics. Inferred shape lookup used a
permission-decorated display string as a declaration-map key, and continued
with a null declaration. Resolution now retains the resolved nominal declaration;
missing materialization returns unknown rather than dereferencing null.

After the repair, the same five diagnostics remain at the start, followed by
additional Vec and Context diagnostics: this is not a new earlier rejection
masking the crash. The original source is retained and is still a failed PASS
target, not a negative test. Normal/shadow check-only, object and IR invocations
terminate normally and rejected invocations leave no artifact. The gate permits
future successful compilation instead of freezing the present diagnostics.

`toka_negative_harness_termination` also runs valid inferred, explicitly typed,
and writable expected-type `Box` construction. Latest targeted rerun: 1/1,
17.26 seconds. Earlier current-patch G + termination gate: 2/2, 138.67 seconds.
No full-suite result is claimed for this change.

## Network temporary-result migration

HTTP/WebSocket remove outer `cede` from call/unwrap temporary results, retaining
named-source transfers inside them. Four original controls compile and run:
`g10_http_empty_header_value`, `g18_header_map_lookup_miss`,
`g12_stdx_websocket_malformed_test`, `g12_stdx_websocket_test`.
Their normal/shadow check-only return codes and stderr also match.
The reproducible runner is `tools/scripts/test_release_temporary_results.py`.
The complete HTTP/WebSocket first-error union from the retained triage contains
12 PASS cases including HeaderMap (rather than the initially quoted 11).
All 12 compile and run with exit 0 after migrating two thread callers and the
read-only certificate path bindings. Runtime logs confirm real HTTP/WS/TLS
execution, not an unsupported-network skip. Thread callers still use native
threads and now check spawn, join and worker results. The certificate helper
takes `const char *`; writable pointers were unnecessary.

The 12 cases are explicitly listed in the runner. Initial library-only changes
restored 9/12; the three caller migrations restore the remaining 3/12. Four
controls and the three changed callers have separately checked normal/shadow
parity. This is a targeted cluster result, not a new full PASS/FAIL/CTest run.
Local checkpoints: crash recovery `9bd79b16`, library temporaries `492ee129`.

The remaining Vec cluster includes `ReferenceBindingSelectorUnavailable` on
reference-valued parameters. Do not infer a raw-element migration from a stale
line number alone. Context's concrete failing instances will be investigated
after the network batch, without Option/Vec/raw_take exemptions.

## Context receiver handoff repair

`tests/semantics/context_release/cancel_chain.tk` preserves
`with_cancel<BackgroundContext>`, the actual `Vec<Sender<bool>>` state,
`Option<Receiver<bool>>` returned by `done()`, async start/block_on, cancellation
error, closed-channel receive and repeated cancellation. Normal/shadow checks
match and the compiled program exits 0. The original `test_cancel` extracted
without changing its body also compiles and runs under start/block_on.
No library/compiler changes were required for this cancellation path.

Timeout isolation retains workspace identity. Creating the timeout and obtaining
its receiver pass separately. Starting a worker with that receiver reproduces
the Option return/discard errors and downstream unresolved Vec morphology; simply
removing the await does not resolve them. Starting a worker with the direct
`channel<bool>()` receiver is a passing control. Explicit Receiver<bool> annotation
and a trivial worker body do not resolve the Context-derived receiver failure.
Those preliminary controls did not use the same outer binding writability.
The same-worker three-route matrix corrects that: direct channel, function-returned
Option/unwrap and Context-returned receiver all failed with a writable `rx#`.
The first wrong lookup is `canImplicitlyPassToCede`: its Drop query used the
permission-decorated display string (`Receiver_M_bool#`) instead of the resolved
nominal identity (`Receiver_M_bool`). Missing that cache/map key caused property
analysis to fall back to the generic template and check unresolved T bodies.
The repair resolves the complete type and uses its soul name for this lookup;
it does not change permission, Send, dependency, Drop or raw_take policy.

After repair, all three original writable-binding routes compile and run. The
worker receives a closed notification and exits. Context cancels twice. The
gate also rejects a second consuming handoff of the same binding with E0438
and no object output. Both call and non-call shadow match normal diagnostics.

| Boundary | Observed facts in this matrix |
| --- | --- |
| Return/wrap | Full Option<Receiver<bool>> value; result carries cleanup |
| unwrap | Full Receiver<bool> result; no external dependency roots |
| binding | Receiver<bool># retains nominal instance and current write view |
| worker argument | OwnedValue, MoveOwned, InvalidateSubtree, CalleeAssumesLiability; dependency complete |
| start | StartHandoff, explicit consuming formal/source, no escaping dependency |

Original unmodified `g09_context` now compiles and runs with exit 0, including
timeout (20 ms in the observed run), parent/child cancellation and value query.
This is targeted recovery, not a full-suite rerun. Tests are retained in
`test_context_receiver_handoff.py`. Final three-route plus original-program
CTest: 1/1, 26.02 seconds.
Ordinary caller, direct signature and termination regression gates: 3/3,
31.34 seconds. Other signature-driven method/static, callable/indirect,
multi-argument atomic and remaining-route gates: 4/4, 42.39 seconds.
The termination test's successful-shadow branch was corrected:
main.cpp intentionally forces shadow to check-only, so it must not demand an
artifact even if -c/--emit-llvm was supplied. Normal compilation still must
produce one; rejected compilation must not.

Additional leaks-at-exit inspection timed out; no zero-leak claim is made.
No source library changes or container exemptions were needed.

Discarded lead: a temporary nested-match probe without workspace identity had an
empty destination identity. With proper workspace registration it passes. This
was a probe-environment difference, not proof of a Context assignment regression.
