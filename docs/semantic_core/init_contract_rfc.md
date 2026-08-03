# RFC: Delayed Initialization Contracts

**Status:** `uninit` is implemented. A narrow `init local = value` scaffold is
implemented for a whole immutable plain local with a definite, never-constructed
state; it preserves the spelling through TKI and retained-body source-less
replay. It is not activation of `init` P1. Sections labelled frozen record
design decisions beyond that scaffold, not current public behavior.

**Scope:** A delayed-initialization contract proves a transition of an exact
storage place from `Uninitialized` to `Initialized`. It is neither an optional
value nor an ordinary write permission. P1 is local, synchronous, and
whole-place only, including an explicit lexical promise block; the active
scaffold implements only its direct local-construction subset. Field-wise and
async extensions require their own evidence.

**Depends on:** the construction-origin and availability model in
[place_state_core_rfc.md](place_state_core_rfc.md), plus declaration-backed H/P
authority and canonical PAL path identity. In this RFC, `Uninitialized` and
`Initialized` are source-facing names for definite PlaceState facts; they do not
define a second state machine.

### Current scaffold boundary

The implemented direct form recognizes contextual `init local = expression`,
permits only an eligible immutable plain local, and rejects a moved, already
initialized, or otherwise ineligible recognized target. It reuses the existing
initialization lowering only after that check. It has positive,
repeated-construction-negative, and retained-generic-body TKI replay coverage.
Its whole-place state set is captured and unioned at the existing `if`, `guard`,
`match`, `loop`, `for`, `break`, and `continue` joins, so `Never | Live` is the
private `Maybe` fact rather than a second spelling of an all-zero `InitMask`.

It deliberately does **not** yet make the full P1 contract public: legacy
ordinary assignment remains available for writable, handle, reference, and
projection initialization paths outside this scaffold; field-wise cleanup and
general exact-place authority are not yet represented; `init place { ... }`,
`place is uninit`, and `init` formals are not implemented. The next
implementation slice must close those gaps before claiming P1 activation.

## 1. Motivation

Toka already records a bounded definite-initialization approximation in
`InitMask`, merges it through control flow, and uses it for static rejection of
reads, ordinary borrows, escapes, and transfers from an uninitialized path.
`uninit` makes that fact visible in source, but the representation does not by
itself distinguish never-constructed storage from a moved-out place or prove
runtime cleanup. An ordinary assignment also neither expresses nor safely
transfers the obligation to make a never-constructed place live.

The missing capability is intentionally analogous to `cede`, but it has the
opposite state transition:

```text
cede:    Live caller-owned value -> Moved
init:    Never caller-owned place -> Live
```

`init` is an authority over one state transition of one place. It is not
payload write permission (`#`) and it must never be inferred from a mutable
binding.

## 2. Terms and state model

An **exact place** is a source-level, addressable storage path with canonical
PAL identity and a PlaceState fact. `InitMask` may encode a bounded projection
of that fact, but is not its semantic definition. An exact place is not a value
expression, temporary, arbitrary pointer, or ordinary borrow.

The source-facing names in this RFC map to PlaceState as follows:

| Name in this RFC | PlaceState meaning |
|---|---|
| `Uninitialized` | definite `Never = NeverConstructed + Present` |
| `Initialized` | definite `Live = Constructed + Present` |
| `Maybe` | the non-singleton analysis fact `{Never, Live}` owned by an active lexical `init` block |
| contract handoff | an access quarantine and outstanding obligation, not a concrete PlaceState |

The relevant source permissions are:

| State | Permitted operation |
|---|---|
| `Uninitialized` | explicit `init`, or handoff to an `init` contract |
| `Maybe` | only `place is uninit` inside its active lexical `init` block |
| `Initialized` | ordinary reads, borrows, moves, and writes allowed by the binding's ordinary morphology |
| contract handoff | no caller use until the callee's declared post-state is applied |

`Maybe` is not a concrete state or enum case; it is the bounded non-empty state
set above and is not a public object state. It exists only inside the lexical
`init` block that owns it. On every normal return from a valid `init` call, the
handoff quarantine is discharged and the caller receives the definite `Live`
post-state; on a diverging path no post-state is required.

`Moved = Constructed + MovedOut` is deliberately not an `Uninitialized`
state. It never satisfies an `init` precondition. If ordinary morphology permits
repopulation, that distinct `Moved -> Live` transition is governed by the
PlaceState Core and ordinary H/P/PAL rules, not by this contract.

## 3. Frozen direction

### IC-01: `uninit` establishes typed empty storage

`uninit` remains the lower-case source spelling. Its type must come from the
RHS, consistent with the frozen `auto` rule:

```toka
auto value = uninit:i32
auto user = uninit:User
```

`auto value: i32 = uninit` is not admitted. `auto` has no declaration-side
type annotation; `uninit:i32` is the complete type-bearing initializer.

The binding's ordinary morphology remains meaningful after initialization, but
does not decide whether the first initialization is allowed. Thus an ordinary
immutable binding is valid:

```toka
auto value = uninit:i32
init value = 100
println(value)

// Rejected: `value` is now an ordinary immutable binding.
value = 300
```

All local binding forms whose declaration denotes stable storage may begin as
`uninit`; this includes handles when their binding itself is a storage place.
It does not make an arbitrary expression, a borrowed view without backing
storage, or a raw-pointer target an `init` place. Existing PAL alias and
authority checks remain in force.

### IC-02: initialization is an explicit state transition

The proposed direct form is:

```toka
init place = expression
```

It is valid only when `place` has the definite `Uninitialized`/`Never` fact and `expression`
produces a complete compatible value. It is the only direct source operation
that makes an `uninit` place live. Plain `place = expression` is an ordinary
assignment and must be rejected while `place` is uninitialized.

The transition is deliberately permitted once even for a binding that will be
read-only after initialization. It does not grant a general `#` payload-write
right, and it does not allow a second initialization.

No runtime "double-init panic" is part of the safe-language design. If a
reachable control-flow path can execute `init place = ...` after that exact
place is already initialized, the program is rejected statically. `init` does
not imply `break`: a loop may continue when its flow facts prove that no second
initialization can occur. In the ordinary search-loop case, `break` is simply
the proof that avoids a reachable repeat.

### IC-03: state permissions and lexical exit

`Uninitialized` is not "an invalid value". It is a storage place that has not
yet constructed a `T`. It owns storage identity, one direct construction
transition, and the ability to hand that transition to an `init` contract; it
does not own any value-level right over `T`.

| Operation | `Uninitialized` | `Maybe` | `Initialized` |
|---|---|---|---|
| `init place = value` | allowed | rejected until narrowed | rejected |
| `callee(init place)` | allowed | rejected until narrowed | rejected |
| `init place { ... }` | allowed | rejected | rejected |
| `place is uninit` | rejected as statically redundant | allowed in its owning block | rejected as statically redundant |
| read, borrow, member/index access, ordinary argument, `cede`, capture, return | rejected | rejected | ordinary rules apply |
| ordinary assignment, payload write, handle rebind, copy, or `dup` | rejected | rejected | ordinary morphology rules apply |

The distinction in the first three rows is intentional. A `Maybe` fact grants
no value operation because not every state in `{Never, Live}` satisfies one,
and it grants no direct construction because one member may already be live.
The only way to obtain a definite `Uninitialized` fact from this set is the true
arm of `place is uninit`.

A sound implementation keeps three linear facts distinct:

```text
InitAuthority(p)                       // permits one Never -> Live transition
InitDischargeObligation(id, p, required_postcondition)
                                           // a named boundary's proof duty
CleanupObligation<T>(p)                // owns destruction of an actually Live T
```

`InitAuthority` exists for an eligible `Never` place and may be transferred to
a construction callee or consumed by first construction. An
`InitDischargeObligation` exists only because an explicit lexical block,
unconditional `init` formal, or Outcome Contract creates that proof duty; an
outer obligation may be suspended/delegated through a nested call but is not
duplicated or silently discharged by it. A `CleanupObligation<T>` exists only
after construction of a value that requires cleanup and follows ordinary value
ownership. Neither of the first two is a `T` cleanup duty.

A bare local such as `auto value = uninit:T` may end an ordinary lexical scope
while still `Uninitialized`. It owns `InitAuthority(value)` but no
`InitDischargeObligation`. Lexical retirement consumes that authority together
with the place identity. Nothing has constructed a `T`, so CodeGen must emit no
`T` drop. This is not an undischarged contract. It is the same cleanup principle
used for a moved or otherwise absent path: destroy only an actually live value.

The mandatory-discharge surfaces are deliberately narrower:

| Exit | Required state |
|---|---|
| ordinary local scope exit | `Initialized`, `Uninitialized`, or `Moved`; drop only `Initialized`/`Live` |
| normal fallthrough from `init place { ... }` | `Initialized` only |
| ordinary return from a function owning `init` parameter | `Initialized` only |
| path that does not reach a local block's fallthrough (`return`, `panic`, `never`) | no local-block post-state; any enclosing function contract still applies |

`Maybe` may never leave its owning `init` block by normal fallthrough,
capture, ordinary argument passing, cross-block merge, or suspension.

### IC-04: flow proof is the only way to use the completed value

Every read of the place, and every ordinary exit from the scope that needs the
place to be live, requires the `Initialized` state on all reachable paths.
Branches, `match`, `guard`, `loop`, `for`, `break`, and `continue` use the
existing exact-path merge machinery; `init` must not introduce a parallel
definite-assignment checker.

In particular, `uninit` is not a value and cannot be compared:

```toka
// Rejected: reads `value` and treats `uninit` as a runtime sentinel.
if value == uninit { ... }
```

A place-state predicate is the only permitted observation form inside the
active lexical block that owns the `Maybe` fact:

```toka
init value {
    // ... control flow may have initialized `value` ...
    if value is uninit {
        init value = fallback
    }
}
```

`value is uninit` is neither equality nor an ordinary expression that reads
`value`. Its left operand is parsed and checked as an exact place. It is
permitted only as a control-flow condition, not as a first-class `bool` that
may be stored, returned, or escaped. The true branch narrows the place to
`Uninitialized`; the false branch narrows it to `Initialized`.

It deliberately does not reuse `guard`. A nullable `guard` proves path
presence and requires its failure branch to diverge. An initialization-state
test has two continuing branches whose merge must establish a new state, so
overloading `guard` would give one keyword incompatible control-flow meanings.

### IC-05: `init` is a two-sided parameter contract

The proposed parameter and call syntax is:

```toka
fn setup_user(init user: User) {
    init user = User(name = "Alice", age = 18)
}

auto my_user = uninit:User
setup_user(init my_user)
```

An `init` parameter is not a `User` value. Entry receives the caller's unique
`InitAuthority(place)` and establishes one unconditional
`InitDischargeObligation(formal, place, Live)` over the exact place with a
definite `Never` fact. Within the callee, the authority may only be:

- discharged with `init user = ...`;
- forwarded explicitly to another matching `init` parameter; or
- used in narrowly specified structural initialization operations that prove
  the whole place initialized.

It cannot be read, borrowed as `User`, returned, captured, ceded, or passed as
an ordinary `User` argument. Forwarding transfers the same authority and
delegates the construction step to the nested contract; the forwarding
function's own unconditional discharge obligation remains active. Its normal
return is valid only after the nested result proves the same place `Live` and
thereby satisfies that outer post-state. This gives arbitrary nesting a static,
compositional guarantee without duplicating an authority or cleanup duty.

Every reachable ordinary return from an `init` function must leave each `init`
parameter initialized. A function that can return an ordinary failure before
initialization cannot declare an unconditional `init` parameter. The bounded
conditional form is specified separately by
[`outcome_contract_rfc.md`](outcome_contract_rfc.md); it must not silently
weaken this unconditional guarantee.

### IC-06: `init place { ... }` is a lexical promise boundary

The language should also expose the obligation directly at a complex local
construction site:

```toka
auto user = uninit:User

init user {
    if cached {
        init user = load_cached()
    } else {
        setup_user(init user)
    }
} // Every normal fallthrough path has made `user` Initialized.

println(user)
```

The block does not create a second kind of assignment or a second construction
authority. Its target must already be an exact place with a definite
`Uninitialized`/`Never` fact and one `InitAuthority`; block entry adds one
lexical `InitDischargeObligation(block, place, Live)`. Its body still uses
`init user = value` or `callee(init user)` to consume/delegate the authority and
prove the required state. The closing brace is a local proof boundary: every
path that reaches it normally must have made the target initialized and
discharged that block obligation.

This includes runtime-dependent construction paths. A block may temporarily
contain the analysis fact `Maybe = {Never, Live}`, but it must resolve that
fact internally, normally through
`if user is uninit { init user = fallback }`; `Maybe` may not escape through
the block's normal fallthrough.

`return`, `panic`, and another `never`-producing path do not reach the code
after the block and therefore do not need to invent a value merely to satisfy
the lexical promise. The initial implementation rejects `break` or `continue`
that exits across an `init` block; this keeps an unfinished obligation from
being silently carried to an outer control-flow target. A later relaxation
requires explicit target-state merge evidence.

This surface is not needed to make the dataflow algorithm sound, but it is
needed for a readable and auditable language: it puts a multi-branch
construction promise next to the code that discharges it and localizes the
diagnostic to the closing brace rather than a later use.

### IC-07: ordinary morphology and initialization authority are separate

The `init` contract bypasses neither PAL nor the normal permission system. Its
`InitAuthority` grants exactly one construction transition; its separate
`InitDischargeObligation` states what a named boundary must prove. Neither
authorizes later replacement, payload mutation, handle rebinding, alias escape,
or lifetime shortening. After a successful transition, the binding returns
immediately to its normal declared authority.

This is why `init user: User` has no mandatory `#`: the parameter's authority
comes from the explicit `InitAuthority` handoff denoted by its contract rather
than from a mutable binding spelling.

### IC-08: `Uninit<T>` is distinct raw storage

`uninit:T` is an initialization state of a source place. `Uninit<T>` remains
the raw/container storage wrapper with `T`'s physical layout and construction /
drop bypass behavior. Neither is an ordinary readable `T`, and neither
implicitly converts to the other.

### IC-09: keyword policy

`uninit` remains a reserved expression keyword. `init` is contextual: special
only in its contract parameter, contract argument, or statement grammar.
Existing protocol names such as `@Default::init` therefore remain valid.

## 4. Deliberately deferred decisions

1. Async `init` contracts and whether an outstanding obligation may cross
   `.await`, cancellation, or task-scope exit. The first implementation must
   reject outstanding `init` obligations at suspension.
2. Implementation and activation of the bounded branch-dependent contract in
   `outcome_contract_rfc.md`; it remains outside `init` P1.
3. Field-wise/array-element `init` contracts, aggregate completion proofs, and
   the corresponding source-less TKI representation.
4. Diagnostics and error-code allocation.

## 5. Coherence, completion boundary, and state observation

The core design is internally coherent because every surface form has one
state effect over the same exact place:

| Surface | Required input state | Normal post-state |
|---|---|---|
| `init p = value` | definite `Never` (`Uninitialized`) | definite `Live` (`Initialized`) |
| `f(init p)` where `f` has an `init` parameter | definite `Never` (`Uninitialized`) | definite `Live` (`Initialized`) |
| `init p { ... }` | definite `Never` (`Uninitialized`) | definite `Live` (`Initialized`) on normal fallthrough |

Ordinary assignment is intentionally absent from this table: it never accepts
`Never`. It operates on `Live`, or performs the separate `Moved -> Live`
repopulation transition only where ordinary morphology, H/P, PAL, and the
bounded capability matrix explicitly permit that operation. It never substitutes
for `init`. The same separation makes immutable construction and nested
forwarding consistent rather than special cases.

The design is complete enough for a first `init` implementation at the local
state level. Outcome Contracts, field-wise initialization, and async remain
subsequent slices. It deliberately does not become a reversible-lifecycle
proposal.

### Frozen state predicate and runtime representation

The familiar search-and-fallback shape keeps its runtime-dependent state inside
the lexical proof boundary:

```toka
auto value = uninit:i32
init value {
    for item in items {
        if item == target {
            init value = item
            break
        }
    }
    // `value` is Live on one runtime path and Never on another.
    if value is uninit {
        init value = fallback
    }
}
// `value` is definitely Initialized here.
```

Inside the block, the compiler's ordinary definite-initialization analysis may
call the resulting set `{Never, Live}` `Maybe`, but cannot answer a source-level
test without more representation.
`value == uninit` remains invalid because it reads uninitialized storage and
pretends that `uninit` is a value.

`value is uninit` is frozen as the dedicated place predicate. For each local
that reaches a `{Never, Live}` join and whose state is observed, CodeGen carries
a private initialization flag beside the storage, splits flow on that flag, and
elides it again once the state becomes definitely initialized. The true arm can
safely use `init value = ...`; the false arm can safely read `value`.

The compiler still rejects a possibly repeated `init` outside a narrowing true
arm. The flag is a representation for the declared state test and cleanup, not
a fallback runtime double-init panic.

For resource values, the same private flag gates cleanup on every synchronous
exceptional exit supported by P1. This is precisely why state observation
belongs in this RFC and cannot be smuggled in as equality syntax.

A later Outcome Contract may feed a mixed `{Never, Live}` branch join into this
block only by consuming its result witness and atomically transferring the
selected PlaceState, conditional `InitAuthority`, and any required cleanup bit
to this exact block-owned discriminator before the result tag is destroyed.
Without that lowering handoff, the mixed join is rejected rather than treated
as an ordinary `Maybe` fact.

## 6. Implementation gates

Implementation is gated on the P-1 baseline qualification ordered by
[semantic_contract_evolution_roadmap_rfc.md](semantic_contract_evolution_roadmap_rfc.md)
and the PlaceState contract in
[place_state_core_rfc.md](place_state_core_rfc.md).
`InitMask` alone must not be treated as proof that a place was never
constructed rather than constructed and later moved.

The initial Level-A implementation must provide Parser, Sema, PAL, CodeGen,
TKI, and retained-body source-less replay agreement for all of the following.
Signature contracts are declaration-recomputed on import. Callee fulfilment
must come from compiled source or a retained canonical body that the consumer
can recheck, lower, and link as the object generated by that same trusted
compile action. A retained body cannot justify a separate provider object.
Traditional bodyless `TKI + object` positive parity is a separate Level-B gate:
it fails closed until the semantic-manifest layer supplies an accepted-
provenance, exact-object-bound attestation path.

- immutable and writable local bindings can start as `uninit:T`;
- only `init place = expression` can initialize the local, exactly once;
- `InitAuthority`, each named `InitDischargeObligation`, and live-value cleanup
  are represented and conserved separately; a bare `Never` local may retire
  its authority with storage, while an active block/formal/witness obligation
  cannot;
- all reads and every operation or exit that requires a live value reject a
  fact containing `Never` or `Moved`;
- branch and loop facts distinguish definitely initialized, definitely
  uninitialized, moved-out, and conflicting re-initialization paths;
- `place is uninit` observes an exact place without reading its storage,
  narrows its two branches correctly, and may not escape as a `bool` value;
- an `init place { ... }` block rejects every normal fallthrough path that
  leaves its target incomplete, and rejects nonlocal loop transfers in its
  first implementation;
- an `init` parameter cannot be used as `T`, but may be correctly forwarded;
- an `init` callee is rejected if any reachable ordinary return leaves the
  target incomplete;
- a successful call updates the caller's exact path to initialized;
- aliases, borrows, `cede`, captures, raw boundaries, and suspension cannot
  bypass the obligation; and
- `Uninit<T>` retains its separate allocation and drop behavior.

Runtime-`Maybe` locals additionally require correct private flag allocation,
narrowing, and synchronous exceptional cleanup.

### Future async extension gate (not part of P1)

P1 rejects an outstanding `init` handoff or `Maybe` fact at suspension. A
future async slice may relax that rule only after the async/place cleanup bridge
preserves the fact and private discriminator in the coroutine frame, treats a
caught cancellation as continuing control flow, and performs state-dependent
cleanup before unhandled terminal cancellation is published.

Logical reset remains an initialized object's explicit domain operation such as
`device.reset#()` or `device.close#()`. A future need for FFI in-place storage
reuse or explicit destruction must be proposed as a separate lifecycle RFC,
with `@Encap` drop, borrow, cancellation, and cleanup evidence; it does not
reserve `deinit` as an `init` keyword today.
