# RFC: Outcome Contracts

**Status:** Proposed post-`init` extension. This document records a bounded
candidate semantic contract, its frozen first-slice surface spelling, and its
acceptance gates. It does not claim current parser, TKI, or implementation
support.

**Depends on:** a qualified current baseline, the shared PlaceState Core,
whole-place synchronous `init` P1, and structured return contracts.

## 1. Purpose

An Outcome Contract makes one caller-visible exact-place post-state depend on
the directly returned nominal result discriminator. Its first use is fallible
construction:

```toka
fn try_read(init out: Packet) -> Result<(), IoError>
outcomes:
    Ok  => out: init
    Err => out: uninit
{
    ...
}
```

The contract is not an ordinary mutation effect. It transfers the unique
`InitAuthority(out)` to the callee and establishes one distinct conditional
`InitDischargeObligation(call, out, variant -> post-state)`. The callee proves
one post-state for every normal result variant and returns a latent branch
witness that the caller must consume before it can use the affected place. A
`Never` branch returns the same authority while discharging this call's
conditional proof duty; a `Live` branch consumes the authority. Any enclosing
lexical or function obligation remains linked to the selected post-state and is
not silently discharged by an `Err => Never` branch. PlaceState by itself never
creates `InitAuthority`.

`init` and `uninit` are source-level post-state atoms in this block. They map
to the internal `Live` and `Never` states, respectively; the latter names are
not source spelling.

## 2. First-slice boundary

The first slice accepts only:

- exactly one outcome-governed whole-place construction formal, using the same
  whole-place eligibility as `init` P1 but a distinct AST/signature kind;
- actual arguments that are whole, stable, caller-owned local places;
- a direct nominal `Result` or directly declared nominal enum return type;
- exhaustive transitions keyed by that result's immediate variant identity;
- immediate consumption by a recognized `match`, discriminant guard, or
  direct error-propagation form; and
- synchronous calls and synchronous callee bodies.

It rejects fields, elements, dereferences, temporaries, globals, captures,
dynamic projections, nested result discriminators, structural/tagless unions,
and an affected place that is aliased or PAL-non-invalidatable.

## 3. Frozen first-slice surface syntax

The `outcomes:` block is a function-declaration contract block, analogous in
placement to `effects:`:

```toka
fn try_read(init out: Packet) -> Result<(), IoError>
outcomes:
    Ok  => out: init
    Err => out: uninit
{
    ...
}
```

It has these fixed first-slice rules:

- It occurs at most once, after the function signature and before the body.
  If both declaration contract blocks occur, canonical source and TKI order is
  `effects:` followed by `outcomes:`.
- Each entry has the direct nominal result variant on the left of `=>` and an
  exact outcome-governed formal followed by `: init` or `: uninit` on the
  right. These state atoms are contract grammar, not ordinary expressions.
- The presence of this block turns the one listed `init` formal into the
  distinct outcome-governed construction contract. Without it, `init out`
  retains its existing unconditional `Never -> Live` normal-return meaning.
- Calls retain the ordinary explicit handoff spelling: `try_read(init packet)`.
  There is no `outcome init` call marker. The returned direct discriminator,
  immediately consumed under Section 7, selects the post-state.
- `=>` deliberately differs from the `effects:` route operator `<-`:
  an outcome variant selects a state for a place, whereas an effects route
  declares an escaping return dependency on a source path.

For example, a caller uses the same direct construction spelling and lets the
recognized match establish the branch fact:

```toka
auto packet = uninit: Packet

match try_read(init packet) {
    Ok  => consume(packet)
    Err => recover()
}
```

## 4. Place-state foundation

The interface-visible PlaceState factorization is:

```text
ConstructionOrigin = NeverConstructed | Constructed
Availability       = Present | MovedOut
```

The concrete states are named by their complete product points:

```text
Never = NeverConstructed + Present
Live  = Constructed      + Present
Moved = Constructed      + MovedOut
```

An Outcome Contract in this slice requires this pre-state:

```text
Never
```

and may produce only these caller-visible post-states:

```text
Never
Live
```

`Maybe` and contract handoff remain compiler analysis facts. They are not
exported object states and cannot appear in an interface postcondition. A
result discriminator may temporarily determine which definite state applies,
but the program must consume that fact at the immediate control-flow boundary.

`Moved` remains distinct from `Never`. An outcome transition cannot restore
construction authority to a place that previously contained a value and was
moved.

Selecting a post-state never grants H/P, borrow, escape, or lifetime authority.
After witness consumption, the place is still governed by its declaration,
direct-flow ceiling, Encap policy, and PAL state. Outcome Contracts refine
existence and cleanup responsibility; they are not authority-upgrade contracts.

## 5. Dedicated semantic representation

Outcome state changes use a dedicated typed representation rather than
return-dependency `effects:` routes:

```text
OutcomeTransition = {
  Subject: OutcomeInitFormalRoot,
  Pre: Never,
  Cases: [
    { Variant: StableVariantId, Post: Never | Live }
  ]
}
```

`Subject` is a formal-root identity, not source text. `Variant` is a nominal
variant identity from the direct return type, not a string comparison against
a printed name. Cases must be exhaustive, non-overlapping, and deterministic.

Return dependencies remain orthogonal. A function may have both dependency
routes and an Outcome Contract, but neither representation is inferred from or
encoded inside the other.

Returning `Result` or an enum never weakens an ordinary unconditional `init`
parameter by inference. Parser/AST and the signature IR must represent an
outcome-governed construction formal as a distinct contract kind and attach its
`OutcomeTransition` explicitly. It may reuse the whole-place eligibility and
handoff operations of `init` P1, but it is not an ordinary `init` formal with a
silently conditional postcondition. Absent the distinct contract, every
ordinary return remains subject to the unconditional `Never -> Live` contract.

## 6. Callee obligations

### OC-CALLEE-01: Entry handoff

An outcome-governed construction formal enters the callee as an exclusive
handoff of the unique `InitAuthority(p)` plus a distinct conditional
`InitDischargeObligation` owned by this call. Neither is a `T` value. The callee
may initialize the place, forward the authority to a compatible construction
contract while retaining/delegating its own branch proof, or inspect place
state only as allowed by the `init` RFC. It may not read, borrow, capture, cede,
return, or pass the formal as an ordinary value before it is Live.

### OC-CALLEE-02: Variant-indexed exit proof

Every reachable ordinary return must identify one direct result variant and
prove the declared post-state for the single governed place on that edge.

For example:

```text
return Ok(...)  requires out == Live
return Err(...) requires out == Never
```

A return whose variant is unknown, indirect, erased, or computed through an
untracked representation is rejected. A `never` path has no normal result
variant and therefore creates no post-state claim.

### OC-CALLEE-03: No silent weakening

The implementation may not return a failure while leaving a place Live when
the failure case declares `Never`, or return success with the place
uninitialized when success declares `Live`. It must use a different explicit
contract if its states differ.

## 7. Caller obligations and the latent branch witness

### OC-CALLER-01: Call precondition

The actual place must be exactly `Never`, the caller must own its unique
`InitAuthority(p)`, and PAL must permit exclusive handoff of that exact root.
The call transfers that authority and establishes the call's conditional
`InitDischargeObligation`. If an enclosing lexical/function obligation exists,
the call delegates its construction step and links that outer duty to the
OutcomeWitness; it does not transfer a nonexistent obligation from a bare local
or consume the enclosing duty. During the call, the caller has no access to the
place.

### OC-CALLER-02: Latent witness

Returning from the call does not immediately choose one unconditional place
state. The result temporary carries a non-user-visible witness:

```text
OutcomeWitness(CallIdentity, ResultIdentity, ExactPlaceIdentity)
```

The witness binds the returned discriminator and the caller's exact place.
It cannot be copied, duplicated, fabricated, detached from that result,
serialized as a value, or attached to another place.

Until the witness is consumed, Sema records an `OutcomePending(p)` quarantine:
the possible state set is `{Never, Live}`, neither value access nor a new
construction handoff is permitted, the unique `InitAuthority(p)` is branch-
linked rather than available unconditionally, and any enclosing discharge
obligation waits on the same witness. CodeGen uses the returned
nominal tag as the private cleanup discriminator: a `Live` case has the
appropriate runtime cleanup ownership, while a `Never` case has none. This is
not the lexical `Maybe` predicate and does not permit `p is uninit`.

### OC-CALLER-03: Immediate consumption

The witness must be consumed immediately by one of these forms:

1. a direct `match` on the call result;
2. a direct discriminant guard recognized by Sema; or
3. direct error propagation under Section 9.

The result may not first be stored in an ordinary local, returned, captured,
placed in an aggregate, passed to another call, or hidden behind a generic or
dynamic abstraction. This is intentionally stricter than ordinary Result use.
The call and its immediate discriminator consumption form one semantic
expression with no intervening user operation or suspension. Any nonlocal exit
is rejected unless it is the direct propagation form in Section 9; lowering
must nevertheless preserve the returned tag long enough to perform the correct
state-dependent cleanup on every supported compiler/runtime unwind.

At entry to each matched branch, Sema applies only that variant's declared
post-state. More generally, each recognized continuation has a statically
known `VariantSet`. Sema may apply one post-state to that continuation only when
every variant in the set declares the same post-state. A wildcard, or-pattern,
guard-false complement, or propagated failure set mixing `Never` and `Live`
must be split further before place access or is rejected in the first slice.
A later join with different branch states is valid only when the existing
PlaceState rules can contain and discharge the difference inside the place's
active lexical `init` block. Otherwise all continuing branches must converge to
the same definite state or have no common continuation.

That mixed-state join has an explicit lowering commit. Before the result
temporary or its nominal tag is destroyed, witness consumption atomically
transfers the selected branch state into the lexical block's private
initialization discriminator. On a `Never` branch it restores the same
`InitAuthority(p)` to the block's conditional authority slot and arms no `T`
cleanup; on a `Live` branch it records the authority consumed and transfers the
one required live cleanup obligation/bit (or none for a trivial `T`) to the
block-owned cleanup plan. The enclosing
lexical `InitDischargeObligation` remains active and later requires `Live` at
normal block fallthrough. Only after this state/authority/cleanup handoff may
the OutcomeWitness and result tag die and the joined fact become the block's
ordinary `Maybe = {Never, Live}`. If lowering cannot perform this
non-suspending commit with the exact block discriminator, the first slice
rejects the mixed join rather than retaining an unbound cleanup fact.

## 8. Authority, contract, and cleanup conservation

The proof uses three distinct linear sorts:

```text
InitAuthority(p)
InitDischargeObligation(id, p, required_postcondition)
CleanupObligation<T>(p)
```

For the single governed place and every result variant:

- call entry transfers exactly one `InitAuthority(p)` and establishes exactly
  one conditional obligation for this call; a bare local had no pre-existing
  contract obligation to transfer, while an enclosing lexical or function
  obligation delegates its construction step and remains linked and active;
- forwarding an unconstructed place transfers that same authority to the
  nested construction contract and links the outer proof duty to its result; it
  does not create a second authority or a `T` cleanup obligation;
- each ordinary callee return discharges this call's conditional obligation by
  proving the declared variant/post-state pair;
- a `Never` return owns no `T` cleanup and returns exactly the same
  `InitAuthority(p)` to the caller's selected branch; it does not discharge an
  enclosing obligation that still requires `Live`;
- a transition to `Live` consumes `InitAuthority(p)` exactly once and, when `T`
  has a non-trivial DropPlan or ResourceContract, creates exactly one
  `CleanupObligation<T>(p)` at the live place;
- `cede` of a `Live` value transfers its value ownership and cleanup obligation
  exactly once; it never transfers construction authority or satisfies an
  unrelated construction contract;
- an ordinary live exit retains cleanup ownership at exactly one live place;
  and cleanup consumes it exactly once.

No variant may duplicate or lose any of these items, manufacture
`InitAuthority` from a PlaceState, confuse a boundary proof duty with value
cleanup, drop an unconstructed value, forget a Live resource, or publish a
state inconsistent with the runtime live/drop mask. A type with unsupported
custom-drop, projection, or resource behavior is rejected before lowering.

This conservation rule applies while the lexical storage and any contract/
witness handoff remain active. Legal lexical retirement of a bare `Never`
local consumes its remaining `InitAuthority` together with the place identity
without dropping `T`, but only when no `InitDischargeObligation` or pending
OutcomeWitness still names that place. A callee handoff or pending witness
cannot use storage retirement to escape or discard its proof duty.

## 9. Error propagation and `?`

Toka's current error-propagation spelling is postfix `!`. This RFC does not
introduce `?`; if a later syntax proposal adds `?`, it must obey the identical
semantic rule below.

A direct propagation operator may consume an OutcomeWitness only when:

1. the operator statically identifies one continuing success variant and the
   propagated failure variant or variants;
2. each continuing or propagated `VariantSet` is homogeneous in its declared
   post-state, unless the lowering preserves distinct edges until after each
   variant's transition is applied;
3. the success continuation applies the corresponding success post-state;
4. every propagated return edge applies the corresponding failure post-state;
5. the enclosing function's own `init` or Outcome obligations accept those
   states on each propagated edge; and
6. error conversion does not erase the source result variant before its
   transition has been selected.

Thus propagation may make `out` Live on the continuing path while leaving it
`Never` on an early failure return. It may not be used inside a
function with an unconditional `init out` obligation if that failure edge
would return before initializing `out`.

## 10. Result discard

A result carrying an unconsumed OutcomeWitness cannot be discarded, including
by an expression statement, ignored binding or standalone `_` that does not
inspect the tag, implicit Unit conversion, or temporary destruction.
Discarding the result would discard the only static evidence selecting the
place's post-state.

An exhaustive recognized `match` is different: its wildcard/or-pattern arm
does consume the discriminator witness, and is admitted exactly when that
arm's statically computed `VariantSet` is homogeneous as required by Section
6. It is not a general permission to ignore the call result.

The first slice rejects standalone discard even when all declared branches
happen to have the same post-state. A later simplification may erase a provably
branch-independent transition, but it is not part of this RFC.

## 11. TKI and separate-compilation gates

Outcome Contracts are semantic interface data, not comments or diagnostic
evidence. Adoption requires:

1. a versioned structured TKI representation that reconstructs typed
   `OutcomeTransition` records;
2. stable formal, nominal type, variant, and exact-place identities;
3. exporter/importer round-trip without source-text path interpretation;
4. importer recomputation of signature-derived well-formedness, variant
   exhaustiveness, authority limits, and obligation conservation;
5. provider-body verification for every callee return edge; and
6. source/source-less equality within each enabled provider-proof profile for
   caller acceptance, rejection, place state, diagnostics, and cleanup
   lowering.

An unbound or ordinary third-party TKI cannot establish body-derived
fulfilment merely by asserting a transition. Support therefore has two
explicit completion levels:

1. **Level A, source/body-rechecked:** source-backed providers and source-less
   providers retaining a canonical body are checked directly. The consumer
   lowers that exact checked body and links only its own generated object; a
   provider-supplied object is not covered. This level does not depend on the
   Semantic Manifest.
2. **Level B, object-attested source-less:** a bodyless provider may be used
   only after the separate manifest RFC defines an accepted provenance and
   exact-object-bound attestation for the same checked obligation. Otherwise it
   fails closed.

Adding or changing an Outcome Contract changes the semantic interface digest
and invalidates dependent caches. The interface-format version changes only
when the structured encoding or schema changes.

## 12. Required evidence

Before implementation is described as supported, the conformance matrix must
cover:

- source-backed and Level-A retained-body-rechecked source-less positive
  `Ok`/`Err` construction, plus Level-B object-attested parity only after that
  profile is separately enabled;
- exact conservation of `InitAuthority(p)` on `Never`, its one-time consumption
  on `Live`, and independent discharge of call/enclosing contract obligations;
- every callee mismatch between returned variant and place state;
- use before witness consumption and use under the wrong variant;
- witness storage, escape, copy, capture, argument passing, and discard;
- direct match, direct guard, and propagation success/failure edges;
- three-or-more-variant enums whose wildcard, guard complement, or or-pattern
  is accepted only for a homogeneous post-state `VariantSet`;
- enclosing unconditional-`init` rejection on a propagated failure;
- branch joins inside and outside an active lexical `init` block, including
  result-tag destruction immediately after the atomic handoff to the block's
  private state/cleanup discriminator;
- pending-witness quarantine and state-dependent exceptional cleanup;
- Copy, move-only, structural-drop, and supported resource-wrapper outputs;
- stale, malformed, incomplete, duplicated, and forged TKI transitions; and
- exact agreement between static state and runtime cleanup counts.

## 13. Non-goals

This RFC does not add:

- field-wise or indexed outcome initialization;
- nested-result or arbitrary predicate postconditions;
- async completion, cancellation, timeout, or task-frame transitions;
- general typestate, session types, or protocol capabilities;
- algebraic effects, effect rows, or handlers;
- a reuse or reinterpretation of return-dependency `effects:` syntax;
- `deinit` or reconstruction after `Moved`;
- user-visible proof values or lifetime parameters; or
- trust in arbitrary interface assertions or foreign/native code.
