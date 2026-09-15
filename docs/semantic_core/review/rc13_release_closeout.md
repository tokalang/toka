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

## Network temporary-result migration (in progress)

HTTP/WebSocket remove outer `cede` from call/unwrap temporary results, retaining
named-source transfers inside them. Four original controls compile and run:
`g10_http_empty_header_value`, `g18_header_map_lookup_miss`,
`g12_stdx_websocket_malformed_test`, `g12_stdx_websocket_test`.
Their normal/shadow check-only return codes and stderr also match.
The reproducible runner is `tools/scripts/test_release_temporary_results.py`.
The complete historical network cluster is still being qualified; four controls
are not a full-suite recovery count.

The remaining Vec cluster includes `ReferenceBindingSelectorUnavailable` on
reference-valued parameters. Do not infer a raw-element migration from a stale
line number alone. Context's concrete failing instances will be investigated
after the network batch, without Option/Vec/raw_take exemptions.
