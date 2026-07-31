# `@Encap` Slice 4 Copy/Dup audit

Slice 4 is available only through `--encap-epoch=v4`.  It includes the Slice
2 policy and Slice 3 lifecycle gates, then switches Copy/Dup semantics to the
new compiler-owned facts.

## Implemented boundary

- Transparent scalar shapes obtain a structural Copy proof by recursively
  checking their complete direct-value field graph.
- A governed capsule remains move-only unless its defining module provides one
  empty `impl T@Copy {}` request.  The request is accepted only after every
  field proves Copy and no custom drop or ownership-bearing field exists.
- Generic definitions derive a stable `CopyRecipe`: `Always`,
  `Never(reason)`, `All(requirements)`, or `Dependent(fact)`.  An explicit
  generic `@Copy` request must prove every `All` requirement from its exact
  type pattern and active `@Copy` bounds; an unbounded or unresolved domain is
  rejected at the declaration, before any concrete instantiation.
- Copy proof is three-state (`Unknown`, `ProvenCopy`, `ProvenNonCopy`) and
  fail-closed.  A direct-value cycle is recorded as non-Copy; pointer and
  reference leaves terminate the proof without expanding the pointee.
- `@Copy` is a compiler marker: it has no method/vtable registration.  A
  proven Copy type gets the intrinsic Dup capability, so a user `@Dup` on the
  same type is rejected as an overlap.
- Generic policy, Copy, and Dup impls cover the nominal type's complete
  parameter domain; specialization is not part of this model. Generic Dup
  coherence runs at declaration time, so any user provider that overlaps, or
  cannot be proved disjoint from, an intrinsic Copy witness is rejected without
  relying on instance-registration order.
- A user `@Dup` is local to the nominal definition and must contain exactly
  one public, non-consuming, non-generic `dup(self) -> Self` method.
- `[dup value]` closure capture is explicit: a proven Copy value is copied
  directly, while a non-Copy value requires a validated local `@Dup` provider
  and invokes it exactly once when the closure is built.
- A generic `@Copy` or `@Dup` impl is materialized only for concrete
  instantiations whose bounds hold. The instance receives its own policy and
  Copy/Dup facts after the template domain has already been validated.
- New user `= delete`, `@Clone`, and `@Drop` declarations are rejected in v4.
  An ordinary method named `clone` is still permitted but has no special
  ownership or lowering meaning.  Legacy trusted-system declarations remain
  accepted until the Slice 6 library migration.

## Regression evidence

Run:

```sh
cmake --build build --parallel 2
python3 tools/scripts/test_encap_slice4_audit.py
```

The audit covers a transparent structural Copy, a valid explicit capsule
`@Copy`, a capsule without that request, a resource-bearing Copy request,
declaration-time rejection of an unbounded or specialized generic Copy domain,
nested generic recipes, specialized generic policies, generic Copy/Dup and
transparent generic Dup overlap, duplicate generic providers, explicit `dup`
closure capture and its single provider call, removed delete/facet syntax, and
an ordinary `clone` method.
