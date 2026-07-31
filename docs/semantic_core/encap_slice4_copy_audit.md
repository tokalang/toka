# `@encap` Slice 4 Copy/Dup audit

Slice 4 is available only through `--encap-epoch=v4`.  It includes the Slice
2 policy and Slice 3 lifecycle gates, then switches Copy/Dup semantics to the
new compiler-owned facts.

## Implemented boundary

- Transparent scalar shapes obtain a structural Copy proof by recursively
  checking their complete direct-value field graph.
- A governed capsule remains move-only unless its defining module provides one
  empty `impl T@Copy {}` request.  The request is accepted only after every
  field proves Copy and no custom drop or ownership-bearing field exists.
- Copy proof is three-state (`Unknown`, `ProvenCopy`, `ProvenNonCopy`) and
  fail-closed.  A direct-value cycle is recorded as non-Copy; pointer and
  reference leaves terminate the proof without expanding the pointee.
- `@Copy` is a compiler marker: it has no method/vtable registration.  A
  proven Copy type gets the intrinsic Dup capability, so a user `@Dup` on the
  same type is rejected as an overlap.
- A user `@Dup` is local to the nominal definition and must contain exactly
  one public, non-consuming, non-generic `dup(self) -> Self` method.
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
`@Copy`, a capsule without that request, a resource-bearing Copy request, a
Copy/Dup coherence conflict, removed delete/facet syntax, and an ordinary
`clone` method.

## Deferred to following slices

The Slice 5 TKI v2 format will serialize Copy/Dup facts and make imported
opaque types fail closed without a verified witness.  Generic `CopyRecipe`,
full generic-domain coherence, `[dup ...]`, and the remaining capture-syntax
changes are tracked by the RFC's later integration work.
