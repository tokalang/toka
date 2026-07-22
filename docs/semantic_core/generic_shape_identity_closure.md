# FZ-3-R02 generic shape identity closure

Status: `Complete`

## Risk

Generic template lookup was lexical, but instantiated-shape and generic-impl
caches used the unqualified template spelling. Two modules declaring
`Box<T>` could therefore share one instantiated layout or bind one module's
generic methods to the other module's shape.

## Closed rule

Generic instances and generic impls are identified by the resolved template
declaration. An unambiguous template keeps its existing source-name identity;
only a collision receives the deterministic module-scoped codegen identity.
This is an internal correctness repair and does not change generic syntax,
source names, diagnostics, or the same-version TKI policy.

## Evidence

- `generic_shape_001_module_identity` runs different local and imported
  `Box<i32>` layouts and methods in both source-backed and source-less TKI
  modes, then rejects cross-module argument passing with `E04571`.
- `generic_shape_module_identity` changes the imported generic layout,
  requires source-hash invalidation and TKI regeneration, preserves the local
  same-named generic, and rejects access to the removed imported member with
  `E0417`.
- The complete semantic replay and cache suites pass with 18/18 and 13/13
  cases respectively.

The change is deliberately limited to generic declaration, instance, and impl
identity. It does not introduce package-stable ABI names or cross-version TKI
compatibility.
