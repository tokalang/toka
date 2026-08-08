# RFC: PlaceState Core

**Status:** Proposed internal semantic-core contract. P0.1 establishes typed
whole-place facts, P0.2 adds bounded direct-projection facts, and P0.3 makes
the synchronous cleanup boundary consume the same elaborated projection plan
as Sema. It changes no source syntax by itself. Language RFCs such as delayed
initialization may depend on it only after the implementation gates in this
document are satisfied for their declared capability slice.

**Purpose:** Define one exact-place state model shared by Sema flow analysis,
PAL, ownership transfer, synchronous CodeGen cleanup, TKI, and source-less
replay. A separately qualified async/place cleanup bridge applies the same
model to coroutine frames and terminal cancellation. This RFC is the candidate
production-language contract for the internal model; it is not a general
user-visible typestate system until its declared slice is accepted and
qualified.

**Depends on:** declaration-backed H/P authority, canonical PAL access paths,
and explicit ownership transfer.

## 1. Required distinction

Allocated storage, a live value, and storage whose value has been moved out are
not the same fact. In particular, the compiler must not infer first-construction
authority merely because storage currently contains no live value.

For each supported exact place, the concrete state is the following product:

```text
ConstructionOrigin = NeverConstructed | Constructed
Availability       = Present | MovedOut
ConcretePlaceState = ConstructionOrigin x Availability
```

Only three product combinations are meaningful while the place remains in its
lexical storage domain:

| Concrete name | Construction origin | Availability | Meaning |
|---|---|---|---|
| `Never` | `NeverConstructed` | `Present` | Storage identity is present, but no `T` has ever been constructed there. |
| `Live` | `Constructed` | `Present` | A live `T` is owned at the place. |
| `Moved` | `Constructed` | `MovedOut` | A `T` existed and its ownership has since left the place. |

`NeverConstructed + MovedOut` is invalid. `Moved` is never normalized to
`Never`, even though neither state permits value-level reads or drops. Retiring
the storage at lexical scope exit removes the place from the ledger; it is not
a fourth reusable state.

This is the bounded factorization proposed by this RFC. Existing `InitMask`,
moved-path sets, drop flags, and drop masks are implementation projections to
be reconciled with it, not independent semantic authorities.

## 2. Flow facts and the bounded `Maybe` predicate

A concrete execution has exactly one concrete state for an exact place. Static
analysis carries a **non-empty set** of possible concrete states:

```text
PlaceFact(p) : nonempty finite set ConcretePlaceState
```

A singleton is a definite fact. Any non-singleton is a joined or uncertain
`PlaceFact`; it is not a concrete state or a fourth enum case. The reserved
term `Maybe` means exactly `{Never, Live}` under the delayed-initialization RFC
and only inside the lexical `init` block that owns that observation. Other
joins, such as `{Live, Moved}`, are uncertain availability facts, are not called
`Maybe`, and do not acquire initialization authority or the right to use
`place is uninit`.

The empty set is analysis bottom for unreachable control flow and is never a
fact attached to a reachable place. A contract handoff is likewise not a
concrete PlaceState: it is an access quarantine and outstanding obligation over
one exact place while its pre-state is being transferred to a callee.

### PS-FLOW-01: transfer and join

Each operation maps every admitted input concrete state to its concrete
post-state. At a continuing CFG join, the fact for a place is the union of the
post-state sets from all reachable predecessors:

```text
join(F1, ..., Fn)(p) = F1(p) union ... union Fn(p)
```

Reads, borrows, moves, ordinary writes, direct initialization, and cleanup are
allowed only when every concrete state in the current fact satisfies that
operation's precondition. A narrowing predicate may partition a fact only when
an owning language RFC specifies the runtime discriminator and lexical escape
rules. Arbitrary testing of compiler state is not permitted.

Break, continue, return, match/guard exits, loop back-edges, and any caught
cancellation outcome that continues in the current function must use the same
transfer and join definitions. A specialized CFG path must not snapshot only
`InitMask` while omitting move, authority-flow, PAL, or cleanup facts. An
unhandled terminal cancellation has no continuing CFG join; its frame cleanup
is governed by the separate bridge in Section 6.1.

## 3. Exact places and bounded capability

An exact place has a canonical declaration identity plus a supported projection
path. Path identity and overlap come from PAL; a value expression, temporary,
unknown raw target, or non-canonical alias is not made exact merely because
CodeGen can compute an address for it.

The first capability matrix is intentionally bounded:

| Place form | State tracking | Initial policy |
|---|---|---|
| stable whole local | whole-place | admitted |
| direct named field of an eligible local record | exact projection | admitted only when Sema and cleanup consume one `PartialMovePlan` with the same field numbering and eligibility mask |
| constant index of an eligible local fixed array | exact projection | admitted only when Sema and cleanup consume one `PartialMovePlan` with the same element numbering and eligibility mask |
| nested projection, dynamic index, spread, enum payload, nonlocal root, custom-drop aggregate, or representation beyond the supported mask | none until proved | reject before lowering |

Eligibility is one semantic predicate consumed by Sema, synchronous CodeGen
cleanup, TKI replay, and diagnostics. In the P0.3 synchronous slice, Sema
materializes it as `PartialMovePlan { kind, eligibleMask }` on each eligible
local declaration or fresh pattern binding; CodeGen uses that plan rather than
implementing a parallel shape predicate. A source-less retained body recomputes
the plan during its own Sema pass. The later async/place bridge must consume
the same predicate rather than widening it. Increasing a mask width or
accepting a new AST shape is a capability expansion and requires new source
and source-less evidence.

For an aggregate, the root is available to a whole-value operation only when
all required owned projections are `Live`. A partial move changes the selected
projection to `Moved`; it does not change a sibling and does not re-root H/P
authority. Runtime cleanup may encode these projection facts as a mask, but a
zero bit alone cannot say whether the logical origin is `Never` or `Moved`.

## 4. Authority is separate from state

PlaceState answers whether a value exists and how it came to be absent. It does
not grant an operation. An operation is admitted only when both its state
precondition and its existing authority conditions hold:

```text
admitted operation
  = PlaceState precondition
  x operation-specific transition authority
  x direct-flow ceiling
  x use-site intent
  x PAL permission
  x capability-matrix eligibility
```

The transition-authority factor is selected by the operation: H or P for the
applicable ordinary rebind/write, unique ownership-transfer authority for a
move, or the separate one-transition `InitAuthority` for first construction.
A factor that is inapplicable to an operation contributes no additional grant;
it is not silently substituted by another authority sort. In particular,
direct `Never -> Live` construction of an immutable binding requires explicit
`init` intent and `InitAuthority`, not a `#`, H, or P write grant. H/P retain
their existing meanings: H controls handle identity rebinding, and P controls
payload writes. Ownership transfer and repopulation remain subject to their
ordinary ownership and morphology rules. No state transition fabricates H, P,
`Init`, an alias, a borrow, or a longer lifetime.

PAL checks the same canonical path whose state changes. Overlapping active
borrows block invalidating transitions. Disjoint proven paths may proceed
independently; unknown overlap fails closed.

## 5. Normative transitions

The following names describe internal semantic transitions. Surface syntax is
owned by the RFC that requests a transition.

### PS-TRANS-01: first construction (`init`)

```text
Never --init--> Live
```

It requires explicit `InitAuthority` over the exact place, exclusive PAL
permission, complete compatible construction, and eligibility in the active
capability slice. It is invalid from `Live`, `Moved`, or a non-singleton fact.
After success, only the place's ordinary declared H/P and morphology remain.

### PS-TRANS-02: repopulation after move

```text
Moved --repopulate--> Live
```

Repopulation is not first initialization. It requires the ordinary authority
needed to replace that kind of binding or projection, plus PAL and capability
eligibility. An immutable place that lacks that ordinary authority remains
`Moved`. Repopulation must never use an `init` contract or recreate its
one-transition authority.

### PS-TRANS-03: ordinary replacement

```text
Live --replace/write/rebind--> Live
```

This requires the applicable H or P authority and PAL permission. Resource
replacement must retire the previous value exactly once at the operation's
defined commit boundary. Ordinary assignment is invalid from `Never`; whether
it can repopulate `Moved` is decided explicitly by the binding/projection's
ordinary morphology and never by conflating `Moved` with `Never`.

### PS-TRANS-04: ownership transfer

```text
Live --move/cede--> Moved
```

The transition commits only after the destination or callee has captured the
value according to its ABI contract. It invalidates that exact source once,
disarms its cleanup obligation once, and preserves unrelated sibling state.
When the destination is an existing place, the two-mode permission-flow
contract must first prove the canonical source and destination `Disjoint`;
equal, either prefix direction, and unknown relations reject before old-
destination retirement or either PlaceState/cleanup transition. Rejected or
non-committed transfers leave the source and destination facts and cleanup
ownership unchanged.

### PS-TRANS-05: drop and storage retirement

A value drop is defined only for `Live`. Retiring a `Live` resource executes its
drop plan exactly once and then removes the place from the ledger. Retiring
`Never` or `Moved` storage removes the place without executing a `T` drop.
Cleanup must not turn either absent state into `Live` or into a reusable
first-construction state.

## 6. Cleanup correspondence

For every compiler-managed resource place in a concrete execution:

```text
cleanup_armed(p) <-> state(p) = Live and p owns the cleanup obligation
```

The flag or mask is initialized false for `Never`, becomes true only after a
successful `init` or repopulation commit, remains true across ordinary live
writes, becomes false after a successful move, and is consumed exactly once by
drop. Allocation of typed storage alone never arms cleanup.

When a static fact contains both live and absent states, CodeGen must carry a
private runtime discriminator whose true cases correspond exactly to `Live`.
The discriminator is an implementation witness for cleanup and a specifically
authorized state predicate; it is not a public value sentinel or a runtime
double-init fallback.

Normal scope exit, explicit return unwinding, and panic/unwind paths supported
by the synchronous runtime must consult the same logical cleanup obligation.

### 6.1 Separate async/place cleanup bridge

Async conformance is a later composition of this RFC with the normative async
TCB contract; it is not a completion prerequisite for the synchronous
PlaceState base. The bridge must prove all of the following at one revision:

1. suspension preserves the exact static fact and its runtime cleanup
   discriminator in the coroutine frame;
2. a caught cancellation outcome such as `.await?` is an ordinary continuing
   CFG edge and does not run terminal frame cleanup; when an awaited child is
   retained, that edge becomes visible only after the TCB's shielded
   cancel-join-drain obligation is discharged;
3. unhandled cancellation and cold cancellation destroy only armed frame-local
   obligations before terminal publication, have no continuing CFG join, and
   never enter the non-suspending terminal cleanup callback while an async
   child-cleanup obligation remains armed;
4. normal return commits a live return place or temporary into the task's
   `ReadyLive` result obligation, disarms the transferred local cleanup, cleans
   the remaining frame locals, and only then publishes terminal completion;
5. a multi-child winner disposition arms exactly one live internal winner
   temporary before `ResolutionCleanup` suspension. Normal construction/return
   transfers it and disarms that cleanup; any later cancellation edge that
   suppresses the normal outcome—unhandled propagation or an explicit no-value
   capture—typed-drops and disarms it exactly once before finalization or
   continuing CFG. Neither path repeats the child result's private claim; and
6. frame memory remains retained until every live result/internal temporary is
   either transferred to one consumer or destroyed with its typed drop plan,
   all registrations are inactive, and the worker/final-suspend/terminal-
   publisher frame-access pins have been released after their last access and
   the runtime has irreversibly retired the frame-access state.

Until this bridge is qualified, a feature RFC may close a synchronous
PlaceState slice but may not claim that its state or partial projections can
survive suspension or terminal cancellation.

## 7. Source and source-less agreement

Only contractual pre/post transitions cross a function or module boundary;
private local CFG facts do not. A signature may eventually carry, for example,
an unconditional `Never -> Live` init contract or an outcome-indexed
transition. The interface representation must bind each transition to the same
parameter/result identity and type used by source checking.

For every exported PlaceState contract:

1. source compilation emits a structured, versioned semantic fact rather than
   an unverified display string;
2. TKI import reconstructs the same precondition, postcondition, path identity,
   operation-specific authority handoff, distinct named discharge-obligation
   kind, and cleanup consequence;
3. consumers operating under the same provider-proof profile accept and reject
   the same programs;
4. malformed, missing, stale, or authority-amplifying facts fail closed; and
5. cache identity changes whenever a transition-relevant fact changes.

Declaration-recomputable facts are recomputed and compared by the importer.
Provider fulfilment has two explicit profiles:

- **Level A:** source-backed compilation and source-less declaration replay
  with a retained canonical body both recheck the body and must agree. The
  consumer lowers that exact checked body and links only its own resulting
  object; it cannot pair the proof with a provider-supplied object.
- **Level B:** traditional bodyless `TKI + object` parity requires a later
  accepted-provenance, exact-object-bound attestation and must agree with Level
  A once enabled.

Before Level B exists, a bodyless provider fails closed. That conservative
rejection is an intentionally restricted profile, not a claim that ordinary
bodyless TKI already has positive parity with source. The manifest layer is not
a prerequisite for Level-A declaration/signature replay, and an interface must
never self-assert body fulfilment into safe authority.

## 8. Implementation and verification gates

### P0.1: whole-place fact boundary (implemented, not a closure claim)

The first implementation slice gives each whole local a `PlaceStateFact`, a
non-coercible representation of the three concrete states and their CFG join.
Sema snapshot, restore, branch/loop/match merge, contract completion, and
call-candidate rollback carry that fact rather than an untyped state mask. The
fact API intentionally admits only state construction, explicit internal
bottom, and union; it exposes no implicit conversion back to an integer mask.

This is a representation boundary, not the unified ledger required below.
P0.1 alone neither treats an unarmed cleanup bit as a `Never` state nor claims
a projection construction-origin fact, shared eligibility object, source-less
exported PlaceState contract, or async/place bridge.

### P0.2: bounded projection ledger (implemented, not a closure claim)

The admitted local direct-record-field and constant fixed-array-index forms
now carry `ProjectionPlaceFacts`: one `PlaceStateFact` per stable projection
number. Direct `cede` and the admitted consuming field receiver commit
`Live -> Moved`; direct reassignment commits the projection back to `Live`.
The ledger is captured, restored, and unioned through `if`, `guard`, loop,
`for`, `match`, break, and continue flow. For a tracked projection,
`InitMask` is now a compatibility liveness view derived from the ledger, so a
`{Live, Moved}` join cannot accidentally become a definite live bit.

P0.2 deliberately preserves the existing CodeGen drop-mask lowering and its
separate eligibility checks. It does not yet make Sema and CodeGen consume one
structured eligibility fact, bind a runtime cleanup mask to the static ledger
at one shared commit boundary, export projection facts through TKI, or support
the async/place bridge. Those are P0's remaining closure work.

### P0.4a: central exact-fact carrier (in progress)

`ExactPlaceFacts` now packages a whole `PlaceStateFact`, an elaborated
`PartialMovePlan`, and its admitted `ProjectionPlaceFacts` on `SymbolInfo`.
The central `AnalysisState` capture/merge path carries that one object, so
break/continue and the loops that consume those snapshots no longer
independently merge whole and projection state. Its model gate covers
projection transitions, joins, legacy-liveness derivation, and fail-closed
plan mismatches.

This remains an in-progress migration. Other explicit CFG snapshots still
copy legacy compatibility views and are not yet a single join authority;
eligibility handoff and CodeGen cleanup remain as specified above. P0.4a
therefore does not widen the surface or satisfy a P0 closure gate.

The first P0.4b CFG slices move ordinary `if`/`else`, `guard`, conditional
`loop`, and `for` capture, restore, reachable-branch selection, and join to
the same `ExactPlaceFacts` value. They preserve the PAL and compatibility
transactions alongside that value; break/continue use the central
`AnalysisState` path. The remaining match, rollback, and outcome-arm paths
remain explicitly unqualified.

A language RFC may claim a PlaceState slice implemented only when all relevant
gates are green at the same revision:

1. **Unified Sema fact:** one snapshot/restore/join path carries construction
   origin, availability, direct-flow ceilings, move state, and PAL identity
   through every admitted CFG edge.
2. **Shared eligibility:** Sema and CodeGen call the same bounded capability
   predicate; every unsupported form is diagnosed before lowering.
3. **Commit correctness:** initialization, repopulation, replacement, transfer,
   and drop update logical state and runtime cleanup at the same defined commit
   boundary.
4. **Synchronous cleanup:** ordinary exit, early return, loop exit, and
   panic/unwind where supported drop exactly the concrete live projections and
   no absent projection.
5. **Authority:** operation-specific H/P, unique-transfer, or `InitAuthority`,
   direct-flow ceilings, PAL conflicts, null narrowing, and state preconditions
   are checked as separate sorts and compose without implicit elevation.
6. **Level-A interface:** Parser/AST where applicable, Sema, TKI export/import,
   cache identity, and retained-body source-less replay agree on every exported
   transition; a bodyless provider fails closed until the separately qualified
   Level-B attestation layer exists.
7. **Evidence:** the acceptance matrix has positive, negative, join,
   re-entry/repetition, over-limit, unsupported-projection, and exactly-once
   cleanup tests.

The first gate for delayed initialization is whole stable locals. The first
gate for partial `cede` is the explicitly eligible direct-field and constant
fixed-array-index matrix. Passing one gate does not silently authorize another.
Passing the synchronous gates also does not close the async/place bridge in
Section 6.1.

## 9. Non-goals

This RFC does not add `deinit`, general typestate, user-visible lifetime
parameters, dynamic session types, implicit clone/borrow, authority casts,
unrestricted partial moves, or algebraic effect handlers. It does not prescribe
one physical mask representation. It requires every chosen representation to
refine the same logical PlaceState contract.
