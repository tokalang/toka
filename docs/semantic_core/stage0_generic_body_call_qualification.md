# Stage 0 Generic-Body Call Qualification

**Status:** audit-only implementation slice. It grants no CodeGen authority
and does not activate the Accepted explicit-call-boundary `cede` behavior.

**Frozen inputs:**

- call transaction v5: `fea743ba2379260187eec64e48d2a1cd341ade0e`;
- non-call Stage 0: `34e9430de86d0a3af09baa42acb51f2d3b748394`.

The independent command

```text
tokac --generic-body-call-qualification=json --check-only source.tk
```

emits:

```text
toka.internal.generic-body-call-qualification / version 1 / audit-only
```

It reuses the frozen call transaction shape with one additional field on each
receipt and transaction: `specialization_identity`. The existing
`--call-transfer-shadow=json` command remains byte-for-byte schema v5 and
continues to suppress generic-body calls.

The envelope also contains `specializations`. Each entry records a stable
identity, `validation=Valid`, `qualification_complete=true`, and its receipt
and transaction counts. Invalid or unchecked specializations never receive a
completion marker.

## Qualification rule

When a selected generic specialization is checked, calls in its body may be
observed only at the exact current speculative depth. Deeper overload
candidate probes remain suppressed. A nested selected generic specialization
opens its own exact-depth body permit.

All body receipts and transactions remain inside the selected generic
qualification store, separate from call-site argument journals:

- `Valid` specialization: publish once;
- valid cache hit: do not recheck or republish the body;
- `Invalid` or `Unchecked`: roll back all body receipts and transactions;
- invalid dependency: propagate failure through the existing generic
  validation frame before any enclosing journal commits.

Rollback removes records by exact specialization identity, not by one global
vector checkpoint. Consequently an invalid outer specialization cannot erase
a valid nested specialization that was already checked and cached, while all
calls belonging to the invalid outer body still disappear.

The specialization identity contains the logical template owner, declaration
coordinate, and recursively encoded semantic type arguments. Nominal type
identity uses logical declaration ownership rather than a source or worktree
path. Named source roots and temporary liability identities inside the body
are scoped by the same specialization identity.

## Qualification boundary

`tools/scripts/test_generic_body_call_qualification_stage0.py` verifies:

- ordinary, method-receiver, and consuming calls in two monomorphizations;
- exactly-once publication across a repeated valid cache hit;
- stable and distinct specialization/source identities without physical
  paths;
- nested generic-body qualification;
- complete rollback after a body semantic error; and
- unchanged normal diagnostics and unchanged call-transfer v5 isolation.

This slice does not make generic-body plans authoritative. CodeGen may consume
them only after a separate fail-closed authority slice proves exact AST-plan
attachment, missing-plan faults, rejected-plan refusal, and source/drop
transition parity.
