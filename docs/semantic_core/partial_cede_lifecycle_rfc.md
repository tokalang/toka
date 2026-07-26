# RFC: Partial `cede` Lifecycle

**Status:** Initial direct-field slice implemented; required work remains
before partial `cede` becomes a general language guarantee.

**Depends on:** `PERM-STATIC-01`, `OWN-FLOW-01`, and `OWN-FLOW-02`.

## 1. Boundary

This RFC does not change Handle/Payload authority.  A member, index, or spread
projection remains a Shared-flow source for permission purposes: a fresh
destination must not obtain payload write authority that the direct projection
did not carry.

The separate question is lifecycle: after `cede base.field`, which parts of
`base` remain readable, assignable, and responsible for destruction?

## 2. Current facts

The compiler already has two useful but separate mechanisms:

- `SymbolInfo::InitMask` tracks per-member initializedness for shape values
  (up to 64 members), checks member reads, and intersects state at control-flow
  joins;
- CodeGen `DropFlag` prevents double-dropping a complete local binding.

The first slice now installs a runtime `i64` drop mask for a local,
compiler-managed record with at most 64 direct fields.  A direct field `cede`
clears its bit; reassigning the field restores it; scope unwinding drops only
the live fields.  Static `InitMask` uses the same numbering, so the transferred
field is rejected on later read while a live sibling remains usable.

Several container implementations still use partial transfers under their own
representation invariant (for example, a Vec removes an element from its
logical length before custom `drop` runs).  That is useful existing behaviour,
not a sufficient general language model.

## 3. Proposed model

For a compiler-managed aggregate with `N <= 64` direct fields, define a
runtime-and-static **live mask**:

```text
bit i = 1  field i is initialized and remains owned by the aggregate
bit i = 0  field i is unset or has been ceded
```

It is the same field numbering used by `InitMask`.

1. A direct `cede base.field_i` first satisfies PAL invalidation for the exact
   projection, then clears bit `i`.
2. A read of `base.field_i` requires bit `i = 1`; an assignment initializes it
   again and sets bit `i = 1`.
3. A whole-value read requires all declared field bits; a whole `cede base`
   requires all bits and then invalidates the root exactly once.
4. Branch joins intersect live masks.  A field ceded on only one continuing
   branch is therefore unavailable after the join until it is reinitialized.
5. Scope exit drops only fields whose live bits are set, in declaration order
   compatible with the existing aggregate drop convention.

The semantic transition is independent of H/P authority:

```text
effective operation = declaration authority ∩ direct-flow ceiling
                      ∩ use-site request ∩ PAL permission
partial cede         = exact-path invalidation + live-mask transition
```

## 4. Deliberate initial limit

The initial implementation supports only direct named fields of local,
compiler-managed, non-custom-drop record shapes.  It rejects a direct field
transfer from a local aggregate with an explicit `drop`; it retains the current
library-invariant treatment for the following until each has its own proof:

- dynamic array indexes;
- spreads;
- enum payload projections;
- aggregates with user-defined `drop` / `@encap` cleanup;

The same direct-field model is valid across `await`: the mask is a coroutine
frame local, and cancellation now executes scope unwinding before publishing
the canceled result.  This is evidenced only for the direct-field slice; it
does not widen the unsupported projection forms above.

This avoids a superficially safe compiler transformation that would either
double-drop a custom container field or silently leak its remaining fields.

## 5. Required implementation slices

1. **Sema (implemented):** exact direct-field `InitMask` clearing rejects
   use-after-partial-cede and permits a reinitialization to restore the field.
2. **CodeGen (implemented):** a per-local `i64` drop mask for eligible
   aggregates clears on direct partial `cede`, restores on direct assignment,
   and dispatches field drops conditionally at scope exit.
3. **Control flow:** preserve the mask through `if`, `guard`, `match`, loops,
   return unwinding, and error propagation.
4. **Eligibility:** diagnose custom-drop cases and retain index, spread, and
   enum projections outside the generic record algorithm.
5. **Evidence:** positive exactly-once cleanup, reinitialization,
   branch-join, and cancellation-across-`await` coverage plus negative
   use-after-move and explicit-custom-drop rejection are in the conformance
   suite. Add source-less replay before widening eligibility.

## 6. Exit criterion

Partial `cede` may be described as a general language feature only when each
supported path has matching static liveness and runtime drop evidence.  Until
then, it remains an implementation-limited operation: permission-safe by the
two-mode flow classifier, but lifecycle-guaranteed only where a library
invariant or a dedicated conformance case proves it.
