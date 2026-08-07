# RFC: Partial `cede` Lifecycle

**Status:** Bounded direct-record-field and fixed-array constant-index design
contract frozen. P0.2 gives the admitted Sema slice a static projection
PlaceState ledger. P0.3 carries one elaborated `PartialMovePlan` from Sema,
through the local declaration AST, to synchronous CodeGen cleanup; CodeGen no
longer re-derives the direct-field/fixed-array eligibility matrix. Async
composition, typed per-slot drop actions, and TKI semantic witnesses remain
separate closure work. Partial `cede` is not a general projection feature.

**Depends on:** [PlaceState Core](place_state_core_rfc.md), `PERM-STATIC-01`,
`OWN-FLOW-01`, and `OWN-FLOW-02`.

## 1. Boundary

This RFC does not change Handle/Payload authority.  A member, index, or spread
projection remains a Shared-flow source for permission purposes: a fresh
destination must not obtain payload write authority that the direct projection
did not carry.

The separate question is lifecycle: after `cede base.field`, which parts of
`base` remain readable, assignable, and responsible for destruction?

## 2. Current facts

The compiler has a bounded static ledger plus two compatibility/lowering
mechanisms:

- `SymbolInfo::ProjectionFacts` carries `Never`, `Live`, and `Moved` facts for
  each admitted direct field or fixed-array element (up to 64). It follows all
  admitted Sema control-flow snapshots and joins; `InitMask` is derived from
  its definite-`Live` projections for that bounded matrix;
- `PartialMovePlan { projection kind, eligible mask }` is computed once by
  Sema for each eligible local declaration, copied to `VariableDecl`, then
  copied to CodeGen's `VariableScopeInfo`. The plan is the shared static
  eligibility and stable-numbering boundary: Sema derives
  `ProjectionFacts` from its mask, and CodeGen installs a cleanup mask only
  from that same mask. It is elaborated body data, not surface syntax or a
  TKI-exported authority claim; a source-less retained body re-runs Sema and
  constructs its own plan;
- CodeGen `DropFlag` prevents double-dropping a complete local binding.

The first slices install a runtime `i64` drop mask for a local,
compiler-managed record with at most 64 direct fields and for a local fixed
array with at most 64 elements. A direct field or constant array-index `cede`
changes its ledger fact to `Moved`; reassigning that projection restores
`Live`; scope unwinding drops only the runtime-mask's live projections. The
derived static `InitMask` uses the same numbering, so a transferred field or
fixed-array element is rejected on later read while a live sibling or element
remains usable.

An eligible owned `obj.field#.consuming_method()` whose receiver is declared
`cede self` is the method-call spelling of a direct-field transfer: it clears
the same static and runtime bit.  For a unique field whose payload is copied
into callee-owned storage, CodeGen also releases the old source heap slot after
the call captures that payload. Shared, borrowed, and raw views are rejected
at this by-value ABI boundary: they may call ordinary `self#` methods under
their declared payload capability, but cannot transfer their referent through
`cede self`. A fresh `new T(...)#.method()` is another owned source: after the
callee or coroutine factory captures its payload, CodeGen releases the
temporary heap slot. This extension is deliberately limited to the same local
direct-field model; nonlocal and indexed consuming receivers are rejected
rather than bypassing the lifecycle proof.

Several container implementations still use partial transfers under their own
representation invariant (for example, a Vec removes an element from its
logical length before custom `drop` runs).  That is useful existing behaviour,
not a sufficient general language model.

## 3. Normative bounded model

The semantic state is the exact-place fact defined by the PlaceState Core. A
runtime **cleanup-live mask** is only its lowering
for supported resource aggregates; it is not authority and cannot distinguish
never-constructed storage from a constructed value that was moved out.

For an eligible compiler-managed aggregate projection that has non-trivial
cleanup, define:

```text
bit i = 1  projection i owns an armed cleanup obligation
bit i = 0  projection i owns no armed cleanup obligation
```

The mask uses the same stable projection numbering as the static place tree,
but it does not decide whether a projection is readable or transferable. A
trivially droppable projection may need no runtime cleanup bit at all. The
static PlaceState ledger alone records availability:

```text
live projection       = (Constructed, Present)
ceded projection      = (Constructed, MovedOut)
never-constructed     = (NeverConstructed, Present)
```

1. A direct `cede base.field_i`, or an eligible owned local direct-field
   consuming receiver, first completes every fallible preparation and proves
   PAL invalidation for the exact projection. If it writes an existing
   destination, that destination must also satisfy `OWN-FLOW-01`'s canonical
   source/destination disjointness rule: equal, either prefix direction, and
   unknown relations reject before any static-state or cleanup-mask change.
   Disjointness is necessary, not sufficient; both ends must be admitted by
   the frozen capability matrix and share one Sema/CodeGen proof for old-
   destination retirement and source cleanup transfer. Destination capture,
   cleanup-obligation transfer, the `Live -> Moved` static transition, and
   cleanup-bit disarm then form one non-suspending semantic commit. There is no
   cancellation, suspension, callback, or fallible edge inside that commit;
   failure before it leaves the source `Live` and cleanup armed. A genuinely
   fresh destination has no old-destination retirement.
2. A read of `base.field_i` requires the definite constructed/present fact. An
   admitted ordinary repopulation transitions constructed/moved-out back to
   constructed/present and, for a non-trivially droppable projection, arms
   exactly one new cleanup duty. It is not the proposed first-construction
   `init` transition.
3. A whole-value read or whole `cede base` requires every required static
   projection fact to be `Live`; cleanup bits are not a substitute for that
   test. Whole transfer then invalidates the root and transfers any cleanup
   obligations exactly once.
4. Static branch joins form the conservative set of possible place states. A
   field ceded on only one continuing branch is therefore not definitely live
   after the join and remains unavailable until a permitted transition proves
   otherwise. For a non-trivial projection, the runtime cleanup mask retains
   the actual branch's cleanup ownership; it is not a general runtime
   availability witness.
5. Scope exit invokes non-trivial field cleanup only where the corresponding
   cleanup bit is armed, in declaration order compatible with the existing
   aggregate drop convention. Trivial fields require no runtime bit or drop.

The semantic transition is independent of H/P authority:

```text
effective operation = declaration authority ∩ direct-flow ceiling
                      ∩ use-site request ∩ PAL permission
partial cede         = exact-path invalidation
                       + cleanup-mask transition when applicable
```

## 4. Frozen capability matrix

| Projection | Required eligibility | Status |
|---|---|---|
| record direct field | local compiler-managed record, at most 64 direct fields, no explicit custom drop, no shared member that would invalidate the compiler-owned field-drop plan | admitted candidate bounded slice; legacy evidence requires PlaceState requalification |
| fixed-array constant index | local fixed array with at most 64 elements and a statically known in-range index | admitted candidate bounded slice; legacy evidence requires PlaceState requalification |
| owned consuming receiver | whole owned value or the admitted local direct-field subset | admitted candidate bounded slice; exact source-slot evidence required |
| nonlocal or indexed consuming receiver | no complete source-slot/drop-mask proof | rejected |
| dynamic/container index, spread, enum payload, nested projection | no general stable mask/cleanup proof | ordinary safe source rejects; resolver-owned intrinsic or explicit `unsafe` implementation only |
| custom-drop or otherwise opaque aggregate | compiler cannot split the destructor invariant | rejected |

The lowering must reject before CodeGen whenever the exact static eligibility
and runtime cleanup representation do not agree. In particular, checking only
the selected index `< 64` is insufficient when the containing aggregate itself
has no admissible mask.

A consuming method receiver remains available only for owned values and the
direct-field subset. Shared, borrowed, and raw receivers are rejected with
`E04602`; nonlocal and indexed receiver projections are rejected with `E04601`
until they share the complete source-slot and cleanup-mask proof.

The following are not general language guarantees. A resolver-owned intrinsic
may enforce them internally, or an explicit `unsafe` implementation may assume
them inside its audited boundary; ordinary safe source cannot claim a
"library invariant" to enter these cases:

- dynamic array indexes and indexes through container internals;
- spreads;
- enum payload projections;
- aggregates with user-defined `drop` / `@Encap` cleanup;

The synchronous bounded slice does not authorize a partial state to cross
`await`. Such a path must be rejected or held behind a non-default experimental
gate until the separate async/place cleanup bridge proves stable frame storage,
caught-cancellation continuation, and cleanup-before-terminal behavior at the
same revision. Historical cancellation tests are targets for that bridge, not
evidence that it is already closed. The bridge does not widen the unsupported
projection forms above.

This avoids a superficially safe compiler transformation that would either
double-drop a custom container field or silently leak its remaining fields.

## 5. Required implementation and requalification slices

1. **Sema transition (legacy slice, requalification required):** exact direct-
   field and fixed-array constant-index state clearing must be reconciled with
   `Live -> Moved` and reject use-after-partial-cede; an admitted repopulation
   restores `Moved -> Live` without creating `Init` authority.
2. **CodeGen mask (legacy slice, requalification required):** a per-local `i64`
   cleanup mask for eligible non-trivial aggregate projections and fixed arrays
   clears on the same atomic partial-`cede` commit, restores on admitted
   repopulation, and dispatches recursive element drops conditionally at scope
   exit. It never supplies static availability.
3. **Synchronous control flow:** every `if`, `guard`, `match`, loop, return, and
   supported error/unwind edge carries the same PlaceState, direct-flow
   ceiling, PAL, and cleanup facts; a specialized merge may not omit one
   domain. Cancellation belongs to the separate async bridge.
4. **Shared eligibility (P0.3):** Sema creates one
   `PartialMovePlan(DirectField | FixedArrayElement, eligibleMask)` for an
   admitted local. CodeGen consumes that plan to allocate, clear, and restore
   the cleanup mask; it does not separately inspect aggregate shape, custom
   drop, shared members, or array bounds to decide eligibility. Over-limit,
   shared-member, custom-drop, nested, nonlocal, dynamic-index, spread, and
   enum cases receive no plan and reject before lowering. Existing masked-drop
   helpers retain defensive fallbacks for malformed internal state; they are
   not a second admission path.
5. **Synchronous evidence:** recorded legacy slices include fixed-array
   lifecycle/drop-mask work (`b6e37756`, `baac987e`, `b5f9823d`), direct-field
   source-less replay (`5ce0ccc2`), and consuming-field receiver closure
   (`e810ecd7`). That dated evidence covers exactly-once cleanup,
   reinitialization, branch joins, fixed-array constant-index normal exit/
   return unwind/bounded loops, and the recorded negative use-after-move,
   custom-drop, dynamic-index, and nonlocal/indexed consuming-receiver cases.
   It does **not** currently qualify exact/prefix source-destination overlap.
   P-1 must add same-whole, same-field, same-index, ancestor/descendant, and
   unknown-overlap negatives plus disjoint controls, then replay that matrix
   source-backed and source-less before widening eligibility.
   Cancellation-across-`await` cases are evidence only for the later
   async/place bridge. At its recorded slice,
   `permission_005_partial_cede_lifecycle` replayed bounded direct-field and
   fixed-array constant-index lifecycle paths; it must not be cited as current-
   HEAD or overlap-rejection evidence until those gates are actually rerun and
   pass.

H/P declarations, stable field numbering, field graphs, and structural
Copy/drop eligibility are declaration facts recomputed by the TKI importer.
Consuming-body discharge and async frame cleanup are body-derived; they require
source/retained-body recheck or a separately accepted-provenance,
exact-object-bound attestation. A standalone TKI or audit record cannot assert
either into authority or lifecycle acceptance.

The pre-manifest closure level is Level A only: source and TKI consumers may
recompute declaration/call-site facts, but body fulfilment requires source or a
retained canonical body that the consumer rechecks, lowers, and links as its
own object. A separately supplied provider object is outside that proof.
Traditional bodyless `TKI + object` fulfilment is Level B and remains closed
until accepted-provenance, exact-object-bound attestation is qualified.
Historical bodyless execution/replay results are evidence for those recorded
objects, not general fulfilment trust.

The P-1 gate at `b937224a` predates the current targeted repair. At `5cf4b9b2`,
the targeted source and source-less runners revalidated the frozen rows and
added `E04632` rejections for over-limit fixed arrays and shared-member
records. The completion repair at `8d680fea` also aligns Sema's direct member
identity resolution for eligible by-value pattern bindings with CodeGen. The
complete release gate at `8d680fea` requalified this candidate across all
thirteen stages. That evidence does not certify the full PlaceState Core.
P0.3 replaces that prior matched-but-duplicated eligibility implementation
with the declaration-carried plan above; its source and source-less replay
qualification is required at the revision that lands it.

## 6. Exit criterion

Partial `cede` may be described as a general language feature only when each
supported path has matching static liveness and runtime drop evidence.  Until
then, it remains an implementation-limited operation: permission-safe by the
two-mode flow classifier, but lifecycle-guaranteed only for a matrix row whose
complete current-revision Sema, CodeGen, cleanup, replay, and negative gates all
pass. Resolver-owned intrinsics and explicit `unsafe` invariants remain scoped
exceptions and do not promote a safe-language row.
