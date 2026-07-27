# RFC: Partial `cede` Lifecycle

**Status:** Direct-field and fixed-array constant-index slices implemented;
required work remains before partial `cede` becomes a general language
guarantee.

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

The first slices install a runtime `i64` drop mask for a local,
compiler-managed record with at most 64 direct fields and for a local fixed
array with at most 64 elements. A direct field or constant array-index `cede`
clears its bit; reassigning that projection restores it; scope unwinding drops
only the live projections. Static `InitMask` uses the same numbering, so a
transferred field or fixed-array element is rejected on later read while a live
sibling or element remains usable.

An eligible `obj.field#.consuming_method()` whose receiver is declared
`cede self` is the method-call spelling of a direct-field transfer: it clears
the same static and runtime bit.  For a unique field whose payload is copied
into callee-owned storage, CodeGen also releases the old source heap slot after
the call captures that payload.  This extension is deliberately limited to the
same local direct-field model; nonlocal and indexed consuming receivers are
rejected rather than bypassing the lifecycle proof.

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

1. A direct `cede base.field_i`, or an eligible local direct-field consuming
   receiver, first satisfies PAL invalidation for the exact projection, then
   clears bit `i`.
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

The initial implementation supports direct named fields of local,
compiler-managed, non-custom-drop record shapes and constant indexes of local
fixed arrays of at most 64 value elements. It rejects a direct field transfer
from a local aggregate with an explicit `drop`, and rejects a dynamic resource
array index in ordinary code.  A consuming method receiver is implemented only
for the direct-field subset; nonlocal and indexed receiver projections are
rejected with `E04601` until they share the full source-slot and drop-mask
proof. It retains the current library-invariant
treatment for the following until each has its own proof:

- dynamic array indexes and indexes through container internals;
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

1. **Sema (implemented):** exact direct-field and fixed-array constant-index
   `InitMask` clearing rejects use-after-partial-cede and permits a
   reinitialization to restore the projection.
2. **CodeGen (implemented):** a per-local `i64` drop mask for eligible
   aggregates and fixed arrays clears on supported partial `cede`, restores on
   direct assignment, and dispatches recursive element drops conditionally at
   scope exit.
3. **Control flow:** preserve the mask through `if`, `guard`, `match`, loops,
   return unwinding, and error propagation.
4. **Eligibility:** diagnose custom-drop cases and retain index, spread, and
   enum projections outside the generic record algorithm.
5. **Evidence:** positive exactly-once cleanup, reinitialization,
   branch-join, and cancellation-across-`await` coverage plus negative
   use-after-move, explicit-custom-drop, and nonlocal/indexed consuming
   receiver rejection are in the conformance
   suite. Fixed-array constant-index coverage now includes normal scope exit,
   reinitialization, return unwinding, `if`/`match`-join liveness, bounded-loop
   cleanup, and cancellation across `await`; dynamic resource index rejection
   prevents an untracked lifecycle path.
   `permission_005_partial_cede_lifecycle` replays the direct-field and
   fixed-array constant-index paths source-less before widening eligibility.

## 6. Exit criterion

Partial `cede` may be described as a general language feature only when each
supported path has matching static liveness and runtime drop evidence.  Until
then, it remains an implementation-limited operation: permission-safe by the
two-mode flow classifier, but lifecycle-guaranteed only where a library
invariant or a dedicated conformance case proves it.
