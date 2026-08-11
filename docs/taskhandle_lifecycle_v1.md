# TaskHandle Lifecycle Contract v1

The versioned document in
[`spec/taskhandle_lifecycle.v1.json`](../spec/taskhandle_lifecycle.v1.json)
is the machine-readable lifecycle boundary for a move-only `TaskHandle<T>`.
Its schema is
[`schemas/toka.taskhandle-lifecycle.v1.schema.json`](../schemas/toka.taskhandle-lifecycle.v1.schema.json).

## Qualification status

**Status: `qualified-subset`.** This is not a claim that the default runtime
implements the entire async TCB model. A tool must read the JSON
`qualification` object before using an operation guarantee as a repair premise.
The object separates current qualified scope, observed current behavior, and
normative targets that are deliberately not yet qualified.

| Current qualified subset | Explicitly unqualified target |
| --- | --- |
| Cold creation does not run a task body; cold-handle cleanup runs before cancellation publication. | Post-normal-claim cancellation suppression. |
| A direct await can make one normal result claim after terminal publication. | Cancel-join-drain across every await, `TaskScope`, `race2`, and `select2` path. |
| `.await?` explicitly captures cancellation without fabricating `T`. | Task-wide cleanup aggregation and structured-scope close progress. |
| Detached work drains its result obligation before default-runtime shutdown. | Exact `PlaceState`/`init`/`cede` cleanup across await and terminal cancellation. |

The current direct-await behavior is important: **once normal result claim is
established, a later parent cancellation request preserves normal delivery.**
It does not suppress that delivery. The native
`toka_async_completion_subscription` probe is the evidence for this behavior;
it is intentionally the opposite of the deferred post-normal-suppression
target.

The existing operation and result-state records therefore describe only the
qualified subset. They do not authorize a tool to infer a structured cleanup
barrier, broad child-cancellation inheritance, or a full async/place bridge.
Unsupported combinations must remain fail-closed rather than being modeled as
best-effort cleanup.

## Qualified lifecycle facts

- An async call creates a cold `created` task.
- `.start`, `.await`, and `.wait` can activate a cold task without inlining its
  body; repeated activation does not start a second execution.
- `pending -> ready-live -> taken` carries one normal result disposition;
  canceled completion has no readable payload.
- Dropping a cold handle reclaims its frame without executing its body.
- Dropping or detaching activated work transfers the result obligation to the
  runtime's bounded detached drain.

`ready-live` means a payload commit has occurred, not that an arbitrary
consumer may read it. The qualified direct path has one private claimant after
terminal publication; this is not a claim about every structured join or
cancellation interleaving.

## Tool use and evidence

For an async repair, inspect the `qualification` object first, then use only
the listed redline or native evidence relevant to the proposed change. Do not
assume that cancellation produces a zero `T`, that dropping a cold task starts
it, that a later cancellation suppresses an established normal result, or that
`block_on` may leave runtime-owned detached work behind.

`tools/scripts/test_taskhandle_lifecycle.py` validates the JSON shape,
compiles/runs every listed source redline, and executes every listed native
evidence target. It is a regression gate for this qualified subset, not proof
that the broader TCB matrix is closed. The remaining closure work is tracked
in [`async_place_contract_closure_v1_execution_plan.md`](async_place_contract_closure_v1_execution_plan.md).
