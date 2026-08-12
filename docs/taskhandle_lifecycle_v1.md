# TaskHandle Lifecycle Contract v1 (historical)

[`spec/taskhandle_lifecycle.v1.json`](../spec/taskhandle_lifecycle.v1.json) and
[`schemas/toka.taskhandle-lifecycle.v1.schema.json`](../schemas/toka.taskhandle-lifecycle.v1.schema.json)
are retained byte-compatible with the `v1.0.0-rc.1` public protocol. They are
not a current conformance claim and must not be used as a repair premise.

## Supersession and erratum

The historical v1 redline entry for
`tests/pass/g11_async_cancel_join_test.tk` said that it proved “completion
subscription is safe.” It does not subscribe to or await terminal completion;
that assertion is withdrawn. The v1 shape cannot be amended in place because
its schema explicitly rejects additional fields.

Current, qualified lifecycle facts are published only in
[TaskHandle Lifecycle Contract v2](taskhandle_lifecycle_v2.md). Consumers must
read its `qualification` object and an exact-revision conformance record before
treating any guarantee as evidence.
