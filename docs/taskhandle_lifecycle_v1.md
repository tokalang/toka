# TaskHandle Lifecycle Contract v1

The versioned contract in
[`spec/taskhandle_lifecycle.v1.json`](../spec/taskhandle_lifecycle.v1.json)
is the machine-readable source for the lifecycle of a move-only
`TaskHandle<T>`. Its schema is
[`schemas/toka.taskhandle-lifecycle.v1.schema.json`](../schemas/toka.taskhandle-lifecycle.v1.schema.json).

It freezes the facts an AI tool needs before proposing an async repair:

- an async call creates a cold `created` task;
- activation by `.start`, `.await`, or `.wait` must not inline the body;
- cancellation has no fabricated `T` payload and propagates through `.await`;
- dropping a cold handle reclaims its frame, while dropping/detaching active
  work transfers lifetime responsibility to the runtime;
- executor shutdown drains active detached work; and
- a normal result is taken once through `pending -> ready-live -> taken`.

This contract describes public lifecycle behavior, not the private C struct
layout or every scheduler-internal transition. `preparing` and the wait
registry remain implementation mechanisms; the public contract is the stated
operation, resource, and result obligation.

## Tool use

For a cancellation, ownership, or task-lifetime edit, inspect the contract and
run only the redline programs relevant to the proposed change. A tool must not
assume that cancellation yields a zero `T`, that dropping a cold task starts
it, or that `block_on` may leave runtime-owned detached work behind.

`tools/scripts/test_taskhandle_lifecycle.py` validates the frozen JSON shape
and compiles/runs every listed redline program under a timeout. The runtime
implementation and broader ABI evidence remain documented in
[`async_runtime_p5_spec.md`](async_runtime_p5_spec.md) and
[`async_runtime_p6_abi_baseline.md`](async_runtime_p6_abi_baseline.md).
