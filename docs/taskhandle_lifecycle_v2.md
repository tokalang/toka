# TaskHandle Lifecycle Contract v2

[`spec/taskhandle_lifecycle.v2.json`](../spec/taskhandle_lifecycle.v2.json) is
the current machine-readable lifecycle contract for move-only `TaskHandle<T>`.
Its [v2 schema](../schemas/toka.taskhandle-lifecycle.v2.schema.json) is a new
protocol; it does not alter the historical v1 shape.

## Qualification boundary

**Status: `qualified-subset`.** v2 names only the facts exercised by its
redlines and native evidence. In particular, it guarantees cold creation and
cold-drop cleanup, one direct normal-result claim after terminal publication,
explicit `.await?` cancellation capture, and detached drain at default-runtime
shutdown.

It explicitly does **not** promise post-normal-claim cancellation suppression,
general cancel-join-drain, structured-scope close progress, or the full
`PlaceState`/`init`/`cede` async bridge. Unsupported combinations remain
fail-closed.

Each `qualified_guarantees[]` entry has a stable `TH-G*` ID. A conformance
record binds the v2 document’s canonical SHA-256, the exact candidate revision,
and only the evidence that passed at that revision. A green test on another
revision is not transferable evidence.

`tools/scripts/test_taskhandle_lifecycle.py --conformance-output PATH` checks
the contract, compiles and runs its Toka redlines, runs the listed native probe,
and writes that revision-bound record. The release gate retains the resulting
record with its other target evidence.
