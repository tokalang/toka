# RFC: Explicit `cede` Source Semantics and Call Boundaries

**Status:** Revisions requested. Fourth-round acceptance candidate. This document
authorizes no implementation, source migration, interface-key change, CI run,
merge, tag, or release. Work may begin only after an explicit review changes
this status to an accepted implementation boundary.

**Target:** Toka 1.0 source-transfer and call-boundary ownership semantics, if
accepted.

**Draft baseline:** `3d32808a9f34e1fdf9c4c36dac9facc5284a0ac2`.

**Prospective supersession:** on activation only, this RFC supersedes the
caller-spelling portion of
[`RC9 Signature-Driven Call Transfer ADR`](rc9_signature_driven_call_transfer_adr.md)
and revises [`OWN-CEDE-001`](rule_matrix.md#own-cede-001-cede-formals-are-signature-driven-transfer-obligations).
On activation it also deletes the result-qualifier rule
[`OWN-CEDE-002`](rule_matrix.md#own-cede-002-cede-return-types-require-explicit-transfer-at-return-sites)
and replaces it with the source-side return matrix in Section 5.
Those documents remain the historical and currently implemented contract until
all acceptance gates in this RFC pass.

## 1. Decision boundary

The revised proposed Toka 1.0 rules are:

> `cede place` is a destructive read of the exact source place written after
> `cede` and always makes its liveness region unavailable. Payload and handle
> spellings are distinct `SourceView`s over an ownership root, not independent
> liveness roots: hats are semantic, not decorative. Copy changes how the
> outgoing value is produced; it does not cancel invalidation.

> A selected `cede` formal requires every existing source-place actual to spell
> `cede`. An eligible expression with `NoSourcePlace` is passed bare; its value
> is consumed or copied without a caller place to invalidate.

> User-written `cede` has exactly one purpose: source invalidation. It therefore
> requires an admitted existing source. A destination contract never makes
> `cede NoSourcePlace` valid, and function result types do not carry `cede`.

The shorter design slogan is:

> **Named source: write `cede`, source becomes unavailable. No source place:
> pass bare.**

For returns, the corresponding slogan is:

> **`return` describes the destination; `cede` describes the source.**

The visible handshake introduced here is intentionally a call-boundary rule;
the exact-source and liveness-region invariants of `CedeExpr` are language-wide.
This is not a claim that `cede` is the only visible invalidation operation in
the language. Existing forms such as error propagation through `result!`,
direct unique-handle transfer, explicit return transfer, aggregate transfer,
and consuming captures retain their own visible operators and contracts.

Call boundaries are strict contract handshakes. A non-`cede` formal rejects an
explicit `cede` actual with `E04640` and commits no state change. A caller that
intends to end a value after one final ordinary call writes the two operations
explicitly, for example `inspect(value)` followed by the standalone expression
statement `cede value`.

For a unique binding such as `auto ^buf = new BigBuffer()`, however, `buf` is
the payload view and `^buf` is the unique owning-handle place. Transferring or
discarding that owner must therefore spell `cede ^buf`. The spelling
`cede buf` must never be documented, fixed, planned, or lowered as destruction
of `^buf`; it names the wrong source layer.

The RFC restores caller-visible ownership loss without deleting the internal
planning required for temporary cleanup transfer, Copy lowering, obligation
tracking, PAL validation, atomic multi-argument rejection, or fail-closed
CodeGen.

### 1.1 User-facing model

Writing code:

> **A `cede` formal receives a named source as `cede source`; preserve the
> source's payload/handle spelling, so a unique owner is `cede ^source`. A
> temporary, literal, or other proven `NoSourcePlace` expression is passed
> bare.**

Reading code:

> **If the current call contains `cede source`, that exact source becomes
> unavailable. If an accepted call passes `source` bare, that call does not
> invalidate the source.**

Here, “exact” includes hats: `cede ^buf` invalidates `^buf`, while an occurrence
of bare `buf` remains a payload operation while the owner is live. After
`cede ^buf`, every payload, member, and index view whose reachability depends
on that owner is unavailable.

The second statement is about PlaceState availability. It does not promise
that a mutable parameter leaves the payload unchanged, or that independent PAL
and lifetime restrictions disappear after the call. Other visible language
operations such as `result!`, direct unique transfer, return transfer, and
standalone `cede` discard retain their own invalidation rules.

Examples:

```toka
// Ordinary by-value source.
consume(cede resource)       // hand over the named source; resource dies here
consume(resource.dup())      // hand over an independent temporary; resource lives
consume(Resource::new())     // source-less owned temporary
consume(42)                  // source-less Copy value
inspect(resource)            // ordinary handshake; this call keeps the place live
inspect(cede resource)       // E04640; mismatch commits no invalidation
cede resource                // explicit terminal discard at this statement

// Unique-owned handle source.
auto ^buf = new BigBuffer()
process(buf)                  // ordinary payload access; ^buf remains live
cede ^buf                     // discard the owner handle; release BigBuffer once
// process(buf)               // error: payload is unreachable through dead owner
// cede buf                   // not owner destruction: buf is the payload view
```

## 2. Motivation

Toka presents explicit resource semantics as a core language property. A call
that makes a named local or exact projection unavailable while showing no
caller-side transfer marker weakens that claim in three places:

- a human reviewer cannot identify the invalidation from the call expression;
- an agent must resolve the callee before it can know whether the next use is
  legal; and
- diagnostics and evidence must explain a source mutation that was absent from
  the caller spelling.

RC9 accepted signature-driven implicit call transfer after proving that the
resolved formal could drive the existing move and cleanup machinery. That work
remains valuable infrastructure, but the caller-spelling policy is no longer
the desired Toka 1.0 surface. In particular, spelling must not depend on whether
a named generic actual later proves Copy. This RFC changes that policy
prospectively while preserving the proven transfer, cleanup, PAL, obligation,
and CodeGen boundaries.

## 3. Terms and semantic facts

### 3.1 `NamedSourcePlace`

A `NamedSourcePlace` is an existing caller place that can be represented by the
current exact-place model and whose state can become unavailable. It includes:

- an admitted whole local;
- an admitted `cede` function parameter;
- an admitted direct-field projection; and
- an admitted fixed-array constant-index projection.

It does not widen partial-transfer eligibility. Dynamic/container indexes,
unsupported nested projections, aliases, nonlocal places, custom-drop partial
aggregates, and any place without the existing lifecycle proof remain rejected.

Eligibility is route-specific rather than a permanent property of a syntax
shape:

```text
EligibilityContext
    Argument | Receiver | Standalone | Return | Assignment | Initialization
    | Aggregate | MatchBinding | ClosureCapture

PlaceEligibility(context)
    Eligible | Ineligible(reason) | Indeterminate
```

For example, an existing proof that admits a fixed-array constant index as a
call argument does not automatically admit that projection as a consuming
receiver, standalone discard, return, or capture. Each route must prove its
own type, exact-path, cleanup-mask, dependency, PAL, and CodeGen contract.

Every admitted place has an ownership/liveness root, an exact access path, and
a written view. Views do not own independent `PlaceState`. In particular:

- `value` names the payload/value place of an ordinary binding;
- for `auto ^buf = new BigBuffer()`, `buf` names the dereferenced payload view
  while `^buf` names the unique owner-handle place;
- `&view`, `*ptr`, and `~shared` likewise name their corresponding handle
  identities, subject to their independent ownership, capability, lifetime,
  and identity contracts; and
- a field or projection retains both its exact path and its selected
  payload/handle layer; and
- payload, member, and index views reached only through a handle depend on that
  handle root remaining live.

Consequently `cede ^buf` may invalidate the unique owner handle. `cede buf`
does not mean “find an owning handle behind this payload and destroy it.” It is
rejected for a payload view reached through an owning handle: moving that
referent out would leave the owner and its Drop liability incoherent. No
implicit dereference, re-hatting, or owner discovery may change the source
selected by an explicit `cede`. Ordinary direct-value bindings remain eligible
as `cede value`.

The transfer origin and liveness effect are separate facts:

```text
TransferOrigin = (OwnershipRoot, ExactPath, SourceView)
LivenessEffect = InvalidateRegion(OwnershipRoot, ExactPath, ReachabilityClosure)
```

For `cede ^buf`, the transfer origin records `HandlePlace(Unique)` exactly,
while `InvalidateRegion` makes the owner root and every view derived solely
through it unavailable. Therefore this is always rejected:

```toka
auto ^buf = new BigBuffer()
take(cede ^buf)
inspect(buf) // use through invalid owner root
```

Conversely, an active borrow of `buf`, `buf.member`, or another overlapping
derived payload view prevents `cede ^buf` during plan validation. The transfer
cannot commit until PAL proves that no live dependency requires the root.
For a partial direct-value transfer, the invalidated region is the admitted
subtree rather than an unrelated ancestor. For a shared owner, transferring
one handle invalidates that source binding and its derived views but does not
claim that other independent shared owners or the allocation are dead.

Evidence identifies an ownership root with a deterministic semantic
declaration coordinate, not an in-process `SymbolID`, pointer, or allocation
counter. The v3 identity is derived from a versioned tuple equivalent to:

```text
SemanticRootId = (
    canonical module key,
    enclosing declaration semantic key,
    lexical binding declaration coordinate,
    binding name and morphology
)
```

The exact serialization is frozen with the Evidence schema. It must be stable
for the same source, independent of compilation order, process, target, and
source-backed versus imported-provider resolution. `NoSourcePlace` has no
fabricated root ID; compiler temporaries use deterministic AST coordinates only
inside their separate transfer-plan evidence.

### 3.2 `NoSourcePlace`

`NoSourcePlace` is a value-producing expression with no caller place whose
availability can be changed. It says nothing by itself about ownership, Copy,
referent identity, dependencies, or cleanup. `makeAccessPath()` is not its
classifier: a borrow construction such as `&value` may carry a referent path
while still having no independently invalidatable source binding. Source
category must be derived from explicit binding/place identity, expression
role, referent/dependency facts, and route context together.

The semantic model must keep these facts separate:

```text
CedeSyntaxPurpose
    SourceInvalidation

SourceCategory
    NamedSourcePlace | NoSourcePlace | Indeterminate

OwnershipRoot?
ExactAccessPath?

SourceView
    DirectValuePlace
    | DereferencedPayloadPlace(hat-kind)
    | HandlePlace(hat-kind)
    | Indeterminate

ReachabilityClosure
    RootAndDependentViews | ExactSubtree | BindingAndDependentViews
    | None | Indeterminate

OwnershipKind
    PlainValue | OwnedValue | UniqueOwnerHandle | SharedOwner
    | BorrowedView | RawIdentity
    | CallableIdentity | OwnedCallable
    | Indeterminate

ReferentPlace?
DependencyRoots[]
WholeOwnedTemporaryEligibility
    Eligible | Ineligible | Indeterminate
```

A borrowed expression, raw identity, place alias, dependency-bearing
expression, or incompletely described generic result does not become an owned
temporary merely because it has `NoSourcePlace`.

### 3.3 `WholeOwnedTemporary`

A `WholeOwnedTemporary` is the admitted `NoSourcePlace` subset whose complete
payload and cleanup liability can be transferred to an admitted destination,
including a selected `cede` formal or return. It uses a validated
compiler-internal temporary transfer plan, not a user-semantic `CedeExpr`. The
plan may share lowering machinery with cede but has no explicit `cede` spelling
and grants no implicit source-place invalidation.

Passing `resource.dup()` bare is valid only when `dup()` produces a complete,
independently owned, dependency-free `NoSourcePlace` result. The original
`resource` stays live; the new temporary is consumed. Conceptually:

```text
CompilerTemporaryTransferPlan(
    value = resource.dup(),
    source = NoSourcePlace,
    destination = CalleeParameter
)
```

The source language contains no synthetic `cede`; the compiler temporary cannot
be named or used after the call.

### 3.4 Copy proof

Copy classification has exactly three semantic states:

```text
ProvenCopy | ProvenNonCopy | Indeterminate
```

Absence of a visible drop hook is not a Copy proof. Generic and source-hidden
calls must use the same proof source as source-backed calls.

Copy proof changes only payload production:

```text
cede copy_place     = CopyValue + InvalidateRegion(copy_place)
cede noncopy_place  = MoveOwned + InvalidateRegion(noncopy_place)
```

Both source places become unavailable. A bare Copy read outside a required
`cede` position remains an ordinary `CopyValue + KeepLive` operation.

These equations assume that `copy_place` and `noncopy_place` are already the
exact admitted direct-value sources. More generally, source invalidation uses
the source's reviewed `InvalidateRegion(root, path, closure)` plan. These
equations do not authorize lowering `cede buf` as owner invalidation of
`^buf`.

### 3.5 Cede obligation

Call-boundary spelling and callee completion are related but distinct facts:

```text
CedeObligation
    None | Outstanding | Discharged
```

A selected `cede` formal creates an `Outstanding` obligation in its callee
parameter. Forwarding through `cede`, returning/storing through an admitted
explicit transfer, or another reviewed terminal sink changes that obligation
to `Discharged`. Ordinary reads, borrows, formatting, or copies do not discharge
it. Any reachable function exit with `Outstanding` retains `E0474`.

`cede` invalidates its source independently of Copy proof. When the source is
itself an obligation-bearing parameter, the same operation also transfers or
discharges that obligation. This is why the following generic wrapper has one
uniform spelling for Copy and NonCopy instantiations:

```toka
fn outer<T>(cede value: T) {
    inner<T>(cede value)
}
```

The exact-view rule is a user-written `CedeExpr` axiom, not a call-only
convention. The same
`TransferOrigin`, `InvalidateRegion`, obligation, and no-re-hatting rules apply
when `CedeExpr` feeds an argument, receiver, standalone statement, return,
assignment, initialization, aggregate member, match binding, or closure
capture. A route may impose stricter eligibility, but it may not reinterpret
the source view. Compiler-synthetic temporary transfer has no `CedeExpr` source
role and is authorized only by its separate validated `NoSourcePlace` plan.

### 3.6 Proposed semantic rule identities

These identifiers are stable review handles. They enter `rule_matrix.md` only
if the RFC is accepted and qualified.

| Rule ID | Proposed status | Operation class | Decision summary |
| --- | --- | --- | --- |
| `OWN-CALL-EXPLICIT-001` | Core guarantee | `CedeObligation`, `OwnershipTransfer`, `Invalidation` | `cede place` records the exact written transfer origin and invalidates its reviewed liveness region; a selected `cede` formal requires it for every source-place actual |
| `OWN-CALL-COPY-001` | Core guarantee | `OwnershipTransfer`, `Invalidation` | Copy selects `CopyValue`; it never overrides invalidation requested by `cede` |
| `OWN-CALL-TEMP-001` | Conservative rejection | `OwnershipTransfer` | a bare actual for a selected `cede` formal is limited to a proven safe `NoSourcePlace` value; owning temporaries transfer cleanup exactly once |
| `OWN-CALL-GENERIC-001` | Conservative rejection | `CedeObligation`, `InterfaceReplay` | generic spelling depends on source category and selected formal, not Copy; every unresolved lowering fact rejects before commit/CodeGen |
| `OWN-CALL-ATOMIC-001` | Core guarantee | `OwnershipTransfer`, `Invalidation` | receiver and all arguments prepare and validate together, then commit once or not at all |
| `OWN-CEDE-VIEW-001` | Core guarantee | `SourceView`, `Invalidation` | every `CedeExpr` route preserves the written view, shares ownership-root liveness, and forbids implicit de-hatting/re-hatting |
| `OWN-RECEIVER-MORPH-001` | Core guarantee | `Receiver`, `InterfaceReplay` | `self` reuses ordinary parameter morphology, permissions, `cede`, trait, and TKI rules |
| `OWN-RETURN-SOURCE-001` | Core guarantee | `Return`, `OwnershipTransfer`, `Invalidation` | return type describes the result; a named source uses its required visible transfer, a `NoSourcePlace` result is returned bare, and function results cannot be `cede`-qualified |

The shared compiler inputs are the selected formal, caller spelling, ownership
root, exact path/view, reachability closure, referent, eligibility context,
value category, Copy proof, ownership/drop facts, destination, obligation
before/after, capabilities, and dependencies. Interface replay supplies only
the provider contract; caller-local analysis must reproduce the same final
decision. Primary diagnostics, implementation areas, and test classes are
specified in Sections 7–13; no diagnostic number is allocated by this draft.

## 4. Proposed normative matrix

The selected formal determines whether the callee receives a cede obligation.
The presence of a source place determines caller spelling. The actual's exact
payload/handle view must also match the selected formal. An explicit `CedeExpr`
accepted by the selected call contract records exactly its written source view
as transfer origin and invalidates the corresponding liveness region. An
ordinary formal or view mismatch rejects that expression before invalidation.

| Selected formal and actual | Caller spelling | Value production | Source disposition | Proposed result |
| --- | --- | --- | --- | --- |
| non-`cede` formal + place/value | bare | `BorrowCapture` or ordinary value flow | `KeepLive` | allow under existing rules |
| non-`cede` formal + any actual | explicit `cede` | none | no state change | reject with `E04640`; callee did not accept ownership/obligation |
| `cede` formal + any transferable matching `NamedSourcePlace` | bare | none | no state change | reject; caller must write `cede`, independent of Copy proof |
| `cede` formal + `ProvenNonCopy` owning/shared matching `NamedSourcePlace` | explicit `cede` | `MoveOwned` or `TransferShared` | `InvalidateRegion(root, path, closure)` | allow; exact written view is the transfer origin; callee obligation becomes `Outstanding` |
| `cede` formal + `ProvenCopy` value matching `NamedSourcePlace` | explicit `cede` | `CopyValue` | `InvalidateRegion(root, path, closure)` | allow; exact written view is the transfer origin; callee obligation becomes `Outstanding` |
| matching `cede ~formal` + named shared handle | explicit `cede ~source` | `TransferShared` | invalidate that binding and dependent views | allow; transfer one shared-owner token, without claiming allocation death |
| matching `cede *formal` + named raw identity | explicit matching hatted `cede` | `CopyIdentity` or reviewed identity transfer | invalidate that binding and dependent views | allow under raw rules; never acquire or destroy referent ownership |
| `cede &formal` or `cede &self` | any | none | no state change | reject: `&expression` constructs a borrow rather than selecting an existing reference binding |
| handle `cede` formal + payload-view actual, or payload `cede` formal + handle-view actual | bare or explicit | none | no state change | reject view mismatch; never infer or erase a hat |
| explicit `cede` of a dereferenced payload reached through an owning handle | explicit `cede` | none | no state change | reject; transfer/discard the owning handle explicitly instead |
| `cede` formal + admitted non-Copy `WholeOwnedTemporary` | bare | `ConsumeTemporary` | `NoSourcePlace` | allow; callee assumes cleanup and obligation |
| `cede` formal + `ProvenCopy + NoSourcePlace` value | bare | `CopyValue` | `NoSourcePlace` | allow; callee obligation becomes `Outstanding` |
| any formal + `NoSourcePlace` expression | explicit `cede` | none | `NoStateChange` | reject: `cede` requires an existing source place |
| owning `cede` formal + borrowed/raw identity | bare or explicit | none | no state change | reject; use the corresponding hatted borrow/identity `cede` contract |
| any route + unresolved source/ownership/Copy/liability fact | bare or explicit | none | no committed change | reject fail-closed before CodeGen |

### 4.1 Contract handshake

At a call boundary, caller and callee must agree. A named actual spells `cede`
only when the selected formal is `cede`. The source result is then invariant:

```text
cede NamedSourcePlace
    => TransferOrigin(root, exact path, written view)
     + InvalidateRegion(root, exact path, reachability closure)
```

For example:

```toka
fn inspect(value: Resource) {}
fn consume(cede value: Resource) {}

inspect(resource)          // borrow/capture; resource stays live
inspect(cede resource)     // E04640; resource remains live
consume(cede resource)     // resource invalidated; callee assumes obligation
consume(resource)          // error: named source omitted required cede
```

If the caller wants an ordinary final inspection followed by destruction, it
writes both contracts:

```toka
inspect(resource)
cede resource
```

The same handshake preserves handle morphology:

```toka
fn inspect(data: BigBuffer) {}
fn consume(cede ^owner: BigBuffer) {}

auto ^buf = new BigBuffer()
inspect(buf)            // payload contract; ^buf remains the owner
consume(cede ^buf)      // handle contract; ^buf becomes unavailable
// consume(cede buf)    // error: payload view cannot satisfy cede ^owner
```

If no callee should receive the owner, `cede ^buf` is the terminal-discard
form. Neither call resolution nor standalone discard may reinterpret
`cede buf` as `cede ^buf`.

This prevents a call such as `log_task(cede task)` from silently discarding a
task merely because the caller mistook a borrow API for an ownership-taking
one. Changing an API from `cede T` to ordinary `T` is intentionally
source-breaking at its callers because the ownership handshake changed.

### 4.2 Copy decision

This RFC proposes a hard 1.0 rule:

- a bare ordinary read of a proven `@Copy` source is `CopyValue + KeepLive`;
- `cede copyValue` is `CopyValue + InvalidateRegion(copyValue)`;
- a selected `cede` formal requires `cede` for every named Copy source just as
  it does for every named non-Copy source; and
- a `ProvenCopy + NoSourcePlace` value such as a literal is passed bare and has
  no source place to invalidate.

This retains the useful rule locked by `copy_explicit_invalidates.tk`: Copy is
a property of value production, not immunity from an explicit destructive
read. The following is therefore valid and intentionally rejects the second
line:

```toka
auto num = 42
consume(cede num)
inspect(num) // use after cede
```

If the caller intends to preserve an owning source while satisfying a cede
formal, it must construct an independent temporary:

```toka
consume(resource.dup())
```

The original resource remains live; the new `dup()` result is consumed.

### 4.3 Borrowed, raw, and callable identity priority

The `NoSourcePlace` exemption does not grant transfer authority to a borrowed
temporary or raw identity. These expressions retain their current independent
checks:

- referent/place identity;
- payload and handle capability ceilings;
- lifetime/member dependencies;
- unsafe provenance;
- execution-boundary restrictions; and
- PAL conflicts.

Classification order is normative:

1. determine `OwnershipKind`;
2. `BorrowedView`, `RawIdentity`, and non-owning `CallableIdentity` enter
   `CopyIdentity`/borrow rules even when the generic Copy query also says
   `ProvenCopy`;
3. `OwnedValue`, `SharedOwner`, and `OwnedCallable` then use Copy proof to
   select `CopyValue`, `MoveOwned`, or `TransferShared`; and
4. any unresolved classification rejects.

This prevents raw/reference/function/dyn-fn identities from simultaneously
matching contradictory `CopyValue` and `CopyIdentity` rows. An explicit `cede`
may invalidate only an admitted transferable binding identity; it never grants
ownership of a referent or bypasses dependency/PAL rules. An owning `cede`
formal must reject borrowed/raw actuals and require an appropriate separate
contract.

### 4.4 Standalone `cede` discard

Toka needs no separate user-facing `drop(value)` function to end a named
source. The existing expression grammar already parses:

```toka
cede value
```

as:

```text
ExprStmt(CedeExpr(VariableExpr(value)))
```

There is no new `CedeStmt`. The proposed frozen semantics are:

```toka
auto ^buf = new BigBuffer()
process(buf)
cede ^buf // exact unique owner-handle discard
```

1. prepare and validate the exact source, Copy/ownership class, PAL state,
   obligation state, and Drop liability;
2. reject `NoSourcePlace`, a payload/handle view mismatch, unsupported
   projection, active-borrow conflict, or indeterminate liability before
   mutation;
3. atomically invalidate the reviewed liveness region and produce one
   anonymous value from the exact written transfer origin;
4. because no destination receives it, the statement/full-expression assumes
   its cleanup liability;
5. discharge any carried cede obligation to this terminal discard sink; and
6. drop the produced value exactly once at statement completion.

For Copy, the physical payload may be produced with `CopyValue`, but the source
place still becomes unavailable and no destructor is required. For an owned or
shared resource, the statement performs the corresponding destructor/release.
Standalone `cede ~shared` releases that source's one shared-owner token;
`cede *ptr` invalidates and ends that raw binding identity without destroying
the referent. `cede &view` rejects because `&view` constructs a borrow temporary
rather than selecting an independently invalidatable reference binding. The
remaining capability, dependency, borrow, and raw-safety rules stay in force.
For an admitted partial place, it must use the same exact-place and cleanup-mask
plan as every other partial `cede`.

The current compiler already has the Parser/AST shape and expression-statement
drop path. `tests/conformance/ownership/unused_owned_result_lifecycle.tk`
records an existing exactly-once resource-drop example. RFC activation must
retain that behavior and add an explicit `cede ^owner` statement fixture while
bringing both value and handle forms under the same validated semantic plan,
Copy, obligation, PAL, partial-place, TKI/evidence, and fail-closed coverage as
call-boundary transfer. A negative `cede payload` fixture must prove that the
compiler does not silently destroy a hidden owner handle.

## 5. Return source semantics and generic forwarding

### 5.1 Delete function-result `cede`

Function result syntax describes result type, morphology, effects, and
dependencies. It does not describe where the callee obtained the result.
Accordingly, this RFC deletes result-side `cede` rather than reinterpreting it:

```text
fn(A) -> cede T      becomes invalid; migrate source to fn(A) -> T
dyn fn(A) -> cede T  becomes invalid; migrate source to dyn fn(A) -> T
```

This is a source and function-type identity change, not a permanent parser
alias. After activation the old spelling receives a focused migration
diagnostic and does not enter the type system. These distinct forms remain:

```text
fn(cede A) -> T  // consuming parameter contract
cede fn(A) -> T  // consuming callable source/receiver contract
```

Return source semantics are:

| Return expression | Value production | Source disposition | Result |
| --- | --- | --- | --- |
| `return named_copy` | `CopyValue` | `KeepLive` until ordinary scope exit | allow |
| `return cede named_copy` | `CopyValue` | `InvalidateRegion` | allow; explicit destructive read |
| `return named_noncopy` | none | `NoStateChange` | reject; named non-Copy source requires visible transfer |
| `return cede named_noncopy` | `MoveOwned` | `InvalidateRegion` | allow; destination assumes cleanup liability |
| `return temporary()` | `ConsumeTemporary` or `CopyValue` | `NoSourcePlace` | allow; result/caller assumes any cleanup liability |
| `return cede temporary()` | none | `NoStateChange` | reject; no source exists to invalidate |
| `return ^owner` | intrinsic unique move | invalidate owner root and dependent views | allow; `^` is already the visible unique-transfer operator |
| `return cede ^owner` | none | `NoStateChange` | reject as redundant; unique return has the single canonical spelling `return ^owner` |
| `return ~shared` / `return cede ~shared` | shared copy or shared-token transfer | keep live or invalidate the selected shared binding | apply the same explicit-source distinction |
| borrowed/raw return | identity flow | dependency/PAL/raw decision | decided by result morphology, dependencies, and safety rules |

A `cede` parameter still carries an `Outstanding` obligation. Returning that
exact parameter through an admitted destructive read may discharge or transfer
the obligation even when substitution proves its payload Copy:

```toka
fn forward<T>(cede value: T) -> T {
    return cede value
}
```

Returning an unrelated temporary does not discharge another parameter's
obligation:

```toka
fn wrong<T>(cede value: T) -> T {
    return make<T>() // E0474 remains for value
}
```

An ordinary owning parameter remains a capture/borrow contract. Writing
`return cede parameter` cannot manufacture transfer authority that its formal
did not grant. A generic bare `return value` must prove Copy or another
non-invalidating flow; `return cede value` instead requires an admitted exact
source and chooses `CopyValue`, `MoveOwned`, `TransferShared`, or identity
transfer only after substitution.

`OWN-CEDE-002` is deleted on activation and replaced by
`OWN-RETURN-SOURCE-001`. The current `E0464`/`MissingCedeReturn` meaning is
removed. Separate diagnostics cover an illegal result qualifier, a bare named
non-Copy source, explicit `cede NoSourcePlace`, redundant `return cede ^owner`,
and missing source-transfer authority.

The implementation must not delete a global `Type::IsCede` bit mechanically if
that storage also represents parameter `cede` or consuming callable state. It
must first split those roles into structured parameter contract, callable
source contract, and result type fields; only the result-side field/identity is
removed. TKI and function-type equality cease storing or comparing return
cede-ness, and the compiler-interface key changes.

### 5.2 Generic decision

Generic caller spelling depends on source category and the selected formal,
not on Copy proof:

```toka
fn outer<T>(cede value: T) {
    inner<T>(cede value)
}
```

This spelling is valid for both Copy and NonCopy instantiations. After
substitution:

- Copy selects `CopyValue + InvalidateRegion(value)`;
- NonCopy ownership selects `MoveOwned + InvalidateRegion(value)`;
- the outer obligation is discharged/transferred and the inner parameter
  begins `Outstanding`; and
- borrowed/raw/indeterminate instantiations reject.

Generic classification therefore remains necessary for payload, identity,
drop, dependency, and CodeGen selection, but it no longer chooses whether a
named caller writes `cede`. A symbolic generic plan may record unresolved
lowering requirements during definition checking; no unresolved
`ValueProductionDisposition`, `SourceDisposition`, `DestinationDisposition`,
obligation transition, or Drop liability may commit or reach CodeGen.
Substitution and source-less replay must resolve every fact or reject
fail-closed.

## 6. Receiver and callable syntax

### 6.1 Receiver morphology reuses parameter morphology

`self` is the method's first parameter with its payload type implicitly fixed
to `Self`. It does not have a second ownership grammar. Every morphology,
permission position, and `cede` rule admitted for an ordinary parameter is
admitted and interpreted identically for `self`:

```toka
fn read(self)                 // equivalent shape: fn read(value: Self)
fn write(self#)               // equivalent shape: fn write(value#: Self)
fn consume(cede self)         // equivalent: fn consume(cede value: Self)
fn consume(cede ^self)        // equivalent: fn consume(cede ^value: Self)
fn consume(cede ^self#)       // unique owner plus payload-write permission
```

Other admitted handle morphologies and H/P permission combinations follow the
ordinary parameter grammar rather than a receiver-only table. A `cede *self`
contract may transfer and invalidate that raw binding identity when the
corresponding ordinary parameter contract is valid, but it never acquires
ownership of the referent. A `cede ~self` transfers one shared-owner binding;
it does not assert that other shared owners or the allocation are dead.

`&expression` is different: it is target-aware borrow construction, not an
unambiguous selector for an existing reference binding. This RFC therefore
rejects both `cede &parameter` and `cede &self`. Ordinary `&parameter` and
`&self` borrow contracts remain available. A future explicit reference-binding
identity selector may enable cede for both parameters and receivers together,
but receiver syntax may not invent that authority alone.

This is a prospective Parser extension. The current declaration Parser rejects
hatted `self`; activation explicitly authorizes changing Parser, method
resolution, trait matching/conformance, TKI import/export, Evidence, and
CodeGen receiver morphology. These components must use the same morphology
representation and validation rules as ordinary parameters. No receiver-only
inference may convert `cede self` into `cede ^self` based on the caller.

The declaration/call handshake is exact:

```toka
impl Value {
    fn consume(cede self) {}
}

impl BigBuffer {
    fn consume(cede ^self) {}
    fn consume_mut(cede ^self#) {}
}

(cede value).consume()       // matches cede self
(cede ^buf).consume()        // matches cede ^self
(cede ^writable).consume_mut() // matches cede ^self#
```

`(cede owner).consume()` cannot substitute for `(cede ^owner).consume()` merely
because ordinary method lookup can dereference a handle to find payload
methods. Conversely, a method declared `cede self` cannot accept a hatted
owner actual. A missing `cede`, extra `cede`, morphology mismatch, or missing
H/P capability rejects before mutation.

The receiver matrix therefore becomes:

| Selected receiver formal | Receiver source | Required spelling | Result |
| --- | --- | --- | --- |
| ordinary `self`/morphology | matching named source | bare matching view | ordinary capture; source remains live |
| ordinary `self`/morphology | explicit `cede` | — | reject contract mismatch; no state change |
| `cede self` | direct-value named source | `(cede value).method()` | produce value and invalidate its liveness region |
| `cede ^self` | unique-owner named source | `(cede ^owner).method()` | move owner, invalidate root and dependent views, create/transfer obligation |
| `cede ~self` or `cede *self` | corresponding matching handle identity | same explicit hatted `cede` grouping | invalidate that source binding under its shared/raw contract; never amplify referent ownership |
| `cede &self` | any expression | — | reject: `&` constructs a borrow and is not an existing reference-binding selector |
| any `cede` receiver | bare matching named source | — | reject: visible `cede` required |
| any receiver | wrong payload/handle morphology | — | reject: never infer, add, erase, or replace a hat |
| admitted source-less receiver | eligible matching temporary | bare expression | copy/consume without caller source invalidation |
| any receiver | explicit `cede NoSourcePlace` | — | reject: no source place exists to invalidate |

The existing expression Parser can already form a method object whose root is
a `CedeExpr`; the hatted form conceptually retains
`CedeExpr(UnaryExpr(Caret, VariableExpr(owner)))`. There is no `ParenExpr`, and
parentheses are not semantic authority. AST cloning must preserve all semantic
children and any source-fidelity flags, while every exporter/pretty-printer
must insert parentheses from expression precedence so the following AST always
round-trips as `(cede ^owner).method()` even if `HasParens` is absent:

```text
MethodCallExpr(
    Object = CedeExpr(UnaryExpr(Caret, VariableExpr(owner)))
)
```

An ordinary final method use followed by destruction remains two explicit
operations on consecutive statements: `value.method()` followed by
`cede value` for a direct value, or `owner.method()` followed by
`cede ^owner` for a unique owner.

### 6.2 General callable-expression invocation

Consuming a named callable uses:

```toka
(cede callable)(arguments)
```

This spelling requires a general postfix invocation carrier. Activation
authorizes a Parser and AST change equivalent to:

```text
InvokeExpr(
    Callee: Expr,
    Arguments: [Argument]
)
```

Postfix `(...)` invocation must accept any semantically callable expression,
including a grouped `CedeExpr`, closure, function value, member result, and
generic callable. Existing name-based `CallExpr` paths may be migrated or
adapted internally, but they must converge on the same selected-formal,
whole-call planner, TKI, Evidence, and CodeGen contract; string-only callee
identity cannot be the final authority for expression invocation.

`cede callable(arguments)` parses as `CedeExpr(InvokeExpr(...))`, while
`(cede callable)(arguments)` parses as
`InvokeExpr(CedeExpr(callable), ...)`. Parser, cloning, and exporters must
preserve this AST distinction and regenerate the required parentheses by
precedence. Semantically, the first form rejects because an invocation result
is `NoSourcePlace`; no destination can turn it into source invalidation. The
second form is admitted only when `callable` is an exact existing source and
the consuming-callable contract matches. Signature inference may not
reinterpret one spelling as the other.

Bare source-less callable temporaries remain eligible only when ownership,
environment, dependencies, and cleanup are proven. Async receivers, partial
direct-field receivers, owning callables, and source-hidden methods must
converge on the same matrix before activation.

## 7. Unified planning and atomic commit

The receiver change cannot be implemented as a late syntax check around the
current state-mutating `CedeExpr` path. Today, checking a `CedeExpr` may update
PAL and PlaceState before final method selection and argument validation. The
new contract requires one receiver-plus-arguments transaction, including
contract-mismatch rejection before any `cede` mutation.

Standalone `cede` uses the same prepared source/obligation/liability facts but
has a statement-end discard destination rather than a selected callee.
Return, assignment/init, aggregate, match, and closure-capture `CedeExpr`
routes use the same dimensions under their own `EligibilityContext`.

The proposed semantic pipeline is:

```text
resolve destination and, when applicable, callable and selected formals
    -> prepare receiver and argument facts without global mutation
    -> derive spelling/source/ownership/copy/obligation/dependency facts
    -> plan every payload, source, obligation, and cleanup transition
    -> validate type, capability, PAL, aliases, dependencies, effects,
       execution boundaries, exact-place eligibility, and drop liability
    -> if any item rejects: commit nothing
    -> otherwise commit all source invalidations and liability transfers once
    -> hand one validated plan to CodeGen and evidence
```

### 7.1 Required prepared facts

At minimum, every planned source/value flow contributes:

```text
ActualResolvedType
FormalResolvedType?
FormalMorphology = DirectValue | Handle(hat-kind) | Indeterminate
FormalAccessCapabilities
SourceCategory = NamedSourcePlace | NoSourcePlace | Indeterminate
OwnershipRoot?
ExactAccessPath?
SourceView = DirectValuePlace | DereferencedPayloadPlace(hat-kind)
             | HandlePlace(hat-kind) | Indeterminate
ReachabilityClosure
ReferentPlace?
DependencyRoots[]
CopyProof = ProvenCopy | ProvenNonCopy | Indeterminate
OwnershipKind
WholeOwnedTemporaryEligibility
CedeObligationBefore = None | Outstanding | Discharged
DropLiability
AccessCapabilities
SurfaceSpelling = Bare | ExplicitCede | IntrinsicUniqueMove
CedeSyntaxPurpose = None | SourceInvalidation
PlanOrigin = UserSource | CompilerSynthetic
EligibilityContext
PlaceEligibility(EligibilityContext)
SelectedFormalContract = None | Ordinary | Cede
```

### 7.2 Required plan dimensions

The implementation may retain current AST elaboration or adopt a structured
side table, but the logical plan must preserve these independent dimensions:

```text
PlanOutcome
    Admitted | Rejected(reason)

ValueProductionDisposition
    None | BorrowCapture | CopyValue | CopyIdentity
    | MoveOwned | TransferShared | ConsumeTemporary

TransferOrigin
    None | ExistingSource(root, exact path, exact view) | NoSourcePlace

SourceDisposition
    NoStateChange | KeepLive
    | InvalidateRegion(root, exact path, reachability closure)
    | NoSourcePlace

DestinationDisposition
    None | CalleeParameter | Receiver
    | Assignment | Initialization | Return | AggregateMember
    | MatchBinding | ClosureCapture | StatementEndDiscard

DropDisposition
    None | SourceRetainsLiability | CalleeAssumesLiability
    | DestinationAssumesLiability
    | StatementEndAssumesLiability
    | SharedLiabilityIncremented | NoLiability

ObligationDisposition
    None | CreateForCallee | TransferToCallee
    | DischargeToReturn | DischargeToStorage
    | DischargeToStatementDiscard | Preserve

CedeObligationAfter
    None | Outstanding | Discharged
```

Spelling is an input to plan admission, not value production or destination.
`StatementEndDiscard` is a destination, so it combines independently with
`CopyValue`, `MoveOwned`, or `TransferShared`. A rejected plan records
`PlanOutcome = Rejected(reason)`, `SourceDisposition = NoStateChange`, and no
committed Drop or obligation transition; it never overloads a transfer enum to
mean rejection.

### 7.3 Atomicity requirements

1. Explicit `cede` receiver and arguments are prepared without committing
   global PAL/PlaceState changes.
2. All receiver and argument types are checked before any invalidation commits.
3. All pairwise alias/PAL relations are validated as one call.
4. A failed later argument leaves earlier explicit and planned sources live.
5. Equal, ancestor, descendant, active-borrow, and unprovable source conflicts
   reject without cleanup-mask or drop-liability mutation.
6. A handle-root invalidation conflicts with every active payload/member/index
   borrow whose reachability depends on that root.
7. A successful call commits each exact liveness region at most once.
8. Obligation before/after is committed with the same transaction as source
   and cleanup state; no rejected call may discharge an obligation.
9. A non-`cede` formal receiving an explicit `cede` rejects before source,
   obligation, or cleanup mutation.
10. A payload/handle view mismatch rejects before mutation; validation cannot
   infer, add, remove, or replace a hat.
11. Nested candidate/overload/generic probes discard all prepared state when
   not selected.
12. Source-backed and source-less resolution produce the same final plan.
13. Every non-call `CedeExpr` route uses the same exact-view and liveness-region
    invariants even when its destination and eligibility differ.

Existing rollback guards may participate, but rollback after observable
mutation is not a substitute for a plan-first authority boundary when a pure
prepared fact is available.

## 8. What implementation may remove and must retain

### 8.1 Remove before freezing the final qualification SHA

- the branch that accepts a bare `NamedSourcePlace` for a `cede` formal and
  synthesizes `InvalidateRegion`;
- the matching default-allow W0409 policy for accepted bare named moves; and
- batching code whose sole purpose is committing multiple implicit named-place
  invalidations.

Removal is part of the behavior implementation, not post-qualification
cleanup. After removal, the branch must freeze one candidate SHA and run every
activation/release gate on that exact SHA. Any later cleanup commit invalidates
the evidence and requires the full gate again.

### 8.2 Retain

- `ConsumeTemporary` planning and Drop-liability handoff;
- a structured compiler-synthetic temporary transfer plan distinct from
  user-semantic `CedeExpr`;
- Copy and identity classification;
- obligation before/after classification and generic lowering requirements;
- PAL and pairwise multi-argument validation;
- explicit multi-argument atomic commit;
- partial-place lifecycle eligibility and cleanup-mask correspondence;
- async, extern, indirect `fn`/`dyn fn`, `@Callable`, dynamic-trait, static,
  generic, TKI, and execution-boundary routing;
- CodeGen fail-closed protection for every `CedeExpr` destination; and
- destination-specific fault injection proving that a missing or mismatched
  validated plan emits no executable artifact.

Every admitted user `CedeExpr` must carry one Sema-validated plan whose root,
exact path, source view, syntax purpose, value production, destination, source
disposition, Drop disposition, and obligation transition match the CodeGen
request. CodeGen may not reconstruct source authority from the expression or
type. A missing, stale, destination-mismatched, or otherwise inconsistent plan
fails closed before artifact emission. Argument/receiver may retain `E0761`;
return, assignment, initialization, aggregate, match, closure capture, and
standalone routes may use separately reviewed diagnostics, but none may fall
back to best-effort lowering.

The implementation must not delete all implicit AST representation merely
because implicit named-place invalidation is removed.

## 9. Diagnostics and migration

### 9.1 Primary diagnostics

- A bare named argument to a `cede` formal uses `E04570` or its
  explicitly reviewed successor, independent of Copy proof.
- Method arguments retain `E04509` or converge on the ordinary code only after
  a diagnostic-compatibility review.
- A bare named receiver selected by any `cede self` morphology requires a
  dedicated diagnostic or an explicitly reviewed reuse; the RFC does not
  reserve a number.
- Receiver/argument morphology mismatch must print both required and supplied
  views, preserve hats in any fix, and commit no state change.
- `cede` of a dereferenced owning payload must explain that the payload view is
  not independently movable and point to the explicit owner handle spelling.
- An active derived borrow that blocks owner transfer must name both the borrow
  path and the ownership root that would become unavailable.
- Explicit `cede` on `NoSourcePlace` requires a diagnostic explaining that
  there is no source place to invalidate.
- Function-result `cede` syntax receives a migration diagnostic to remove only
  that qualifier; it is not entered into function-type identity.
- Bare named non-Copy return, redundant `return cede ^owner`, and return from a
  source lacking transfer authority receive distinct source-side diagnostics.
- An unresolved plan requires a fail-closed diagnostic naming the missing
  source/ownership/Copy/identity/liability fact.

Callee-side `E0474`, source-side return transfer, partial-move diagnostics, PAL
diagnostics, and fail-closed CodeGen diagnostics remain independent.

### 9.2 Fixes

A machine-applicable insertion of `cede` is allowed only when the compiler has
already proved:

- one exact source place;
- the exact payload/handle source view and required formal view;
- the selected formal is `cede`;
- the syntax can be rewritten without precedence ambiguity; and
- no multi-argument or receiver grouping judgment is hidden by the fix.

The fix must preserve hats rather than reconstruct a payload name: a unique
formal receiving `^source` is fixed to `cede ^source`, never `cede source`.
Receiver fixes must produce `(cede value).method()` for a direct-value source
or `(cede ^owner).method()` for a unique owner source, never
`cede value.method()` and never a de-hatted spelling.
Alias, borrow, partial-place, generic-indeterminate, and multi-argument failures
must not offer a speculative automatic rewrite.

Return migration may mechanically rewrite a parsed result type
`-> cede T` to `-> T` while preserving the complete result morphology, effects,
and dependencies. A bare named non-Copy return receives `return cede source`
only after exact-source and transfer-authority proof. Redundant
`return cede ^owner` is fixed to `return ^owner`. No fix may turn a temporary
return into `return cede temporary()`.

### 9.3 W0409 transition

`W0409` is the migration inventory for currently accepted implicit
`InvalidateRegion` plans. During implementation it may remain available as an
audit mode. After activation, the corresponding bare named call is an error,
so W0409 no longer describes an accepted core-language path.

W0409 alone is not a complete migration inventory: current bare named Copy
actuals may use `CopyValue + KeepLive` and emit no implicit-move warning, but
the revised signature rule still requires `cede` when their selected formal is
`cede`. The pre-implementation census must therefore combine W0409 results with
resolved-formal/source-category discovery. Only genuine `NoSourcePlace`
expressions remain bare.

## 10. Test migration baseline

At draft baseline, `tests/semantics/signature_driven_cede_direct` contains:

- 80 `.tk` fixtures;
- 53 files that emit at least one W0409 under
  `--warn-implicit-call-move`; and
- 74 total W0409 diagnostics.

The number 74 is a warning count, not a test count. No bulk conversion of 74
tests is authorized.

The tracked `.tk` baseline also contains 75 `-> cede` occurrences across 44
files. Production-library use is limited to five function/callable result
occurrences in `lib/std/thread.tk`; the remaining migration is concentrated in
tests and fixtures. These are source-migration counts, not evidence of an ABI
change. The census must be regenerated immediately before implementation.

Fixture migration must preserve each test's independent purpose:

| Fixture class | Required migration |
| --- | --- |
| pure bare named-move acceptance probe | become a focused caller-side rejection expecting `E04570`/reviewed equivalent |
| eligible source-less temporary or literal | remain positive and bare; retain CopyValue versus ConsumeTemporary coverage |
| named proven Copy actual selected by `cede` formal | add explicit `cede`, remain positive, and require later use-after-cede rejection |
| explicit Copy invalidation such as `copy_explicit_invalidates.tk` | remain positive and preserve source invalidation |
| borrowed/raw identity or already explicit independent path | retain its separate identity/borrow decision and original test purpose |
| unique owner plus derived payload/member/index borrow | require owner transfer rejection before mutation and use-after-owner rejection after a successful transfer |
| runtime fixture whose purpose is transfer/drop behavior | add explicit `cede` only at named invalidating calls; keep runtime qualification |
| multi-argument alias/borrow/atomic fixture | add explicit `cede` to intended transfer arguments and continue proving all-or-nothing failure |
| mixed runtime plus use-after-move fixture | split or locally rewrite; do not turn the entire file into compile-fail |
| argument/receiver E0761 fixture | retain missing-plan CodeGen fail-closed purpose |
| return/assignment/init/aggregate/match/capture/standalone fault fixture | inject missing and mismatched validated plans per destination and require no artifact |
| TKI/source-less fixture | retain source/TKI plan parity purpose |
| Evidence fixture | update schema expectations without discarding caller/callee/return stages |
| hatted `self` declaration/call | cover Parser, trait conformance, source/TKI resolution, H/P permissions, mismatch rejection, runtime, and exporter round-trip |
| callable-expression invocation | distinguish `(cede callable)(args)` from `cede callable(args)` across Parser, clone, exporter, TKI, Sema, and CodeGen |
| non-call `CedeExpr` route | independently cover return, assignment/init, aggregate, match, closure capture, and standalone eligibility without source-view reinterpretation |
| old function-result `cede` syntax | migrate `fn`/`dyn fn` result spelling to unqualified result type while preserving parameter/callable cede; reject old syntax after activation |
| return source matrix | cover Copy keep/invalidate, named NonCopy reject/move, temporary bare/rejected cede, intrinsic unique move/redundant cede, shared, borrowed/raw, generic, and obligation cases |
| async/extern/callable/dynamic/static/generic route | retain route-specific runtime and atomicity coverage |

A repository-wide W0409 plus resolved-`cede`-formal/source-category census must
be regenerated immediately before implementation begins. Counts outside this
80-fixture directory are discovery data, not frozen migration scope.

## 11. Evidence, TKI, and cache boundary

### 11.1 Evidence

`toka.cede-obligation-evidence` v2 is bound to the RC9 caller-implicit contract
and must not be silently reinterpreted. It remains available only through an
explicit historical replay mode. It is a hard protocol mismatch when requested
or consumed as current 1.0 evidence. Activation requires v3 (or an explicitly
reviewed separately named protocol) that can express:

```text
boundary_kind  = argument | receiver | standalone | return
                 | assignment | initialization | aggregate
                 | match_binding | closure_capture
plan_origin    = user_source | compiler_synthetic
surface_spelling = bare | explicit_cede | intrinsic_unique_move
spelling_reason = ordinary | formal_required | source_less_exempt
                  | explicit_transfer | standalone_discard
selected_formal = none | ordinary | cede
actual_resolved_type = type
formal_resolved_type = type | none | indeterminate
formal_morphology = direct_value | handle(hat_kind) | none | indeterminate
formal_access_capabilities = capability_set | none | indeterminate
cede_syntax_purpose = none | source_invalidation
source_category = named_source_place | no_source_place | indeterminate
ownership_root  = semantic_root_id | none | indeterminate
exact_path      = path | none | indeterminate
source_view     = direct_value_place
                  | dereferenced_payload_place(hat_kind)
                  | handle_place(hat_kind) | indeterminate
reachability_closure = root_and_dependent_views | exact_subtree
                       | binding_and_dependent_views | none | indeterminate
ownership_kind  = plain_value | owned_value | unique_owner_handle | shared_owner
                  | borrowed_view | raw_identity
                  | callable_identity | owned_callable | indeterminate
copy_proof      = proven_copy | proven_noncopy | indeterminate
temporary_eligibility = eligible | ineligible | indeterminate
eligibility_context = argument | receiver | standalone | return
                      | assignment | initialization | aggregate
                      | match_binding | closure_capture
plan_outcome    = admitted | rejected(reason)
value_production = none | borrow | copy_value | copy_identity
                   | move_owned | transfer_shared | consume_temporary
transfer_origin = none | existing_source(root, path, view) | no_source_place
source          = no_state_change | keep_live
                  | invalidate_region(root, path, closure) | no_source_place
destination     = none | callee_parameter | receiver | assignment
                  | initialization | return | aggregate_member
                  | match_binding | closure_capture | statement_end_discard
drop            = none | source_retains | callee_assumes
                  | destination_assumes | statement_end_assumes
                  | shared_incremented | no_liability
obligation_before = none | outstanding | discharged
obligation_action = none | create_for_callee | transfer_to_callee
                    | discharge_to_return | discharge_to_storage
                    | discharge_to_statement_discard | preserve
obligation_after = none | outstanding | discharged
commit          = not_committed | prepared | committed
```

Surface spelling, callee consumption, and return transfer remain distinct
stages. Evidence must retain the exact written source path and source view; it
may not normalize `buf` and `^buf` into one source. It must distinguish
`CopyValue + InvalidateRegion` from ordinary
`CopyValue + KeepLive`, and `ConsumeTemporary + NoSourcePlace` from
`CopyValue + NoSourcePlace`. Standalone evidence uses
`selected_formal = none`, `destination = statement_end_discard`; rejection
uses `plan_outcome = rejected(reason)`, `source = no_state_change`, and
`commit = not_committed`. Fields whose facts were unavailable may remain
`none`/`indeterminate`, but an admitted committed plan may contain no unresolved
authority-bearing fact. Evidence may describe rejection but may not grant
semantic authority.

`formal_resolved_type`, complete formal morphology/view, and formal H/P
requirements are mandatory for an admitted argument or receiver plan; a cede
bit alone cannot prove `cede self` versus `cede ^self`. `ownership_root` uses
the stable v3 semantic coordinate defined in Section 3.1 and never emits a
process-local `SymbolID`. Compiler-synthetic temporary records keep
`surface_spelling = bare`, `cede_syntax_purpose = none`, and identify their
authority through `plan_origin = compiler_synthetic`.

Return evidence records `selected_formal = none`,
`destination = return`, source category, value production, transfer origin,
source disposition, Drop handoff, and obligation action. There is no
return-cede qualifier field in v3.

### 11.2 TKI and source-less replay

Provider TKI preserves declaration-side facts only:

- formal `cede` bits and the complete formal morphology, including `self`;
- formal H/P capability requirements;
- declared Copy/ownership/identity/generic constraints;
- the callee-side obligation contract;
- dependency/member/effect/execution-boundary contracts;
- result type/morphology without a return-cede bit; and
- type, trait, callable, and ABI facts already required for resolution.

Provider TKI must not serialize caller-local exact paths, ownership-root IDs,
`SourceView`, `SourceCategory`, caller spelling, PAL state, route eligibility,
plan outcome, or source disposition. Those facts are computed from the caller
AST and caller state after the imported declaration is selected. Source-backed
and source-hidden declarations must produce the same local plan from the same
formal contract and caller expression.

An imported declaration may not restore the RC9 implicit named-place rule due
to missing body or Copy facts. Named caller spelling remains independent of
Copy; any unresolved provider constraint or caller-local source, view,
eligibility, lowering, obligation, identity, or liability fact rejects.

### 11.3 Compatibility key

Activation is a source-semantic compatibility break and requires a new
compiler-interface key. The RFC does not select its literal value. If the TKI
wire representation changes, its format/schema version must also change. Old
cache entries must reject rather than replay RC9 caller-spelling semantics
under the new compiler.

The key also fences function-type identity after
`fn(A) -> cede T`/`dyn fn(A) -> cede T` are removed. Old TKI carrying a result
cede bit rejects; it is not silently normalized during current-protocol replay.
Source migration deletes only the result qualifier and leaves parameter cede
and consuming-callable contracts intact.

No physical ABI change is implied solely by the spelling rule.

## 12. Staged implementation plan and estimate

The estimate is for one engineer after RFC acceptance.

### Stage 0: obligation-aware unified planner — 10–16 person-days

- freeze ownership root, exact path, `SourceCategory`, `SourceView`,
  reachability closure, route eligibility, `OwnershipKind`, Copy, referent,
  destination, Drop liability, `PlanOutcome`, formal resolved type/morphology/
  H-P requirements, stable root identity, and obligation facts;
- build one transaction-local receiver-plus-arguments prepared plan before any
  source or obligation mutation;
- make owner invalidation close over dependent payload/member/index views and
  make every overlapping active derived borrow block the transfer;
- reject ordinary-formal/explicit-`cede` mismatches before destructive reads;
- reject payload/handle-view mismatches before destructive reads and prohibit
  implicit de-hatting, re-hatting, or owner discovery;
- bring standalone `ExprStmt(CedeExpr)` discard under the same prepared
  source/obligation/liability model;
- give return, assignment/init, aggregate, match, and closure-capture
  `CedeExpr` routes the same exact-view/liveness invariants with independent
  route eligibility;
- require a destination-matching Sema validated plan for every admitted
  `CedeExpr`, with fail-closed CodeGen and no artifact on missing/mismatch;
- publish audit/shadow parity without changing accepted source behavior;
- give receiver and arguments equal validated-plan/E0761 fail-closed coverage;
- prove candidate/overload/generic rejection discards the complete transaction;
  and
- select diagnostic identities, Evidence v3, and the TKI/cache boundary.

No caller-spelling behavior changes before Stage 0 demonstrates source-backed,
source-hidden, and CodeGen plan parity.

### Stage 1: parameters, returns, Copy, generic, and identity — 9–15 person-days

- require `cede` for every named actual selected by a `cede` formal,
  independent of Copy proof;
- retain `E04640` for explicit `cede` supplied to an ordinary formal;
- implement `CopyValue + InvalidateRegion` for explicit Copy sources;
- distinguish Copy NoSourcePlace rvalues from owned temporary consumption;
- make user `CedeExpr` source-only, replace synthetic temporary `CedeExpr` with
  an internal transfer plan, and reject explicit `cede NoSourcePlace` in every
  destination;
- land the return source planner in audit/shadow mode before removing old
  checks; then activate the complete Copy/NonCopy/temporary/unique/shared/
  borrow/raw/generic/obligation matrix;
- split overloaded `Type::IsCede` roles, reject result-side `cede`, remove the
  old `E0464` meaning, and preserve parameter/callable cede contracts;
- migrate all 75 tracked result-side occurrences across 44 `.tk` files in the
  same behavior revision, including five `lib/std/thread.tk` occurrences;
- make ownership/identity classification precede Copy classification;
- preserve exact hats in diagnostics and machine-applicable fixes;
- prohibit `cede` of dereferenced owning payloads and prohibit source-view
  reinterpretation in arguments, standalone, return, assignment/init,
  aggregate, match, and closure capture;
- qualify standalone Copy, owned/shared, obligation-bearing, borrowed-conflict,
  partial-place, and indeterminate `cede` statements;
- migrate direct/static/generic/extern/indirect/async argument routes; and
- classify the 80-fixture migration rather than bulk-editing warnings.

### Stage 2: receiver morphology and callable expressions — 10–16 person-days

- extend the declaration Parser so `self` reuses the complete ordinary
  parameter morphology and H/P permission representation;
- carry receiver morphology through method selection, traits/conformance,
  TKI, Evidence, async/source-hidden resolution, and CodeGen;
- accept `(cede value).method()` and `(cede ^owner).method()` with exact
  declaration/call morphology matching;
- require explicit `cede` for every named receiver selected by a `cede self`
  morphology, including Copy and identity handles;
- reject `cede &self` and `cede &parameter` uniformly until an explicit
  reference-binding identity selector exists;
- require the hatted form when receiver transfer invalidates a unique owner
  handle, and reject the de-hatted payload substitute;
- reject explicit receiver cede for ordinary `self` methods before mutation;
- add general postfix `InvokeExpr(CalleeExpr, Args)` or an equivalent
  expression-callee carrier, and accept `(cede callable)(args)` as the
  unambiguous consuming callable form;
- make AST clone/export/round-trip preserve semantic structure and regenerate
  required method/invocation grouping from precedence rather than `HasParens`;
- preserve proven source-less receiver/callable temporary rows;
- migrate `unwrap`, `unwrap_err`, `into_iter`, partial receiver, async,
  dynamic-trait, and source-hidden routes; and
- prove rejected receiver/argument combinations commit no PAL/PlaceState,
  obligation, or cleanup change.

### Stage 3: protocols, deletion, qualification, and dogfood — 9–14 person-days

- activate Evidence v3 and isolate v2 as explicit historical replay;
- restrict provider TKI to declaration-side contracts, compute caller root,
  path/view/category/eligibility locally, and update the interface key;
- remove return cede-ness from TKI/function-type identity, reject old TKI, and
  qualify the complete tracked-source migration;
- complete source, TKI, async, extern, callable, generic, and CodeGen fault
  matrices;
- migrate examples/tools/packages and update user/AI documentation;
- delete the old implicit named-place invalidation and obsolete batching paths;
- run local gates after deletion and repair every failure;
- freeze one final candidate SHA only after behavior and cleanup are complete;
- run the full four-target release qualification on that exact SHA; and
- perform independent human/agent dogfood against those exact artifacts.

### Total

```text
10–16 + 9–15 + 10–16 + 9–14 = 38–61 person-days
```

Expected single-engineer schedule: approximately 8–12 weeks. Removing source
compatibility work does not remove the obligation-aware generic model,
unified transaction, full receiver morphology, callable-expression AST,
return-source and function-type migration, TKI/Evidence work, or final
qualification cost. This remains a bounded semantic migration, not a compiler
rewrite.

## 13. Activation gates

One exact revision must satisfy all of the following before this RFC can become
the active Toka 1.0 contract:

1. ordinary, method, static, generic, extern, indirect `fn`/`dyn fn`,
   `@Callable`, dynamic-trait, async, `.start`, thread handoff, and source-hidden
   routes use the same matrix;
2. every named actual selected by a `cede` formal requires visible caller
   `cede`, independent of Copy proof;
3. every accepted user-written `CedeExpr`, including Copy, records the exact written
   transfer origin and invalidates its reviewed liveness region;
4. payload and handle spellings are views over one ownership root: after
   `cede ^buf`, `buf`, its members, and indexes reachable only through that
   owner are unavailable;
5. every active payload/member/index borrow derived from an owner root blocks
   transfer of that root before any mutation commits;
6. `cede buf` cannot be inferred or lowered as `cede ^buf`; argument, receiver,
   standalone, return, assignment/init, aggregate, match, closure capture,
   TKI, Evidence, and CodeGen routes preserve the exact-view axiom;
7. every bare admitted source-less flow is proven `NoSourcePlace`; Copy
   rvalues use `CopyValue + NoSourcePlace`, while owned temporaries transfer
   cleanup exactly once through a compiler transfer plan distinct from
   `CedeExpr`;
8. explicit user `cede` on `NoSourcePlace` rejects in every destination, and
   borrowed/raw/dependency-bearing expressions do not enter the owned
   temporary exemption;
9. an explicit `cede` actual supplied to an ordinary formal rejects with no
   state change;
10. standalone `cede place` invalidates Copy and non-Copy direct-value sources,
    and standalone `cede ^owner` invalidates the unique owner root; either
    discharges an outstanding obligation when present and drops/releases its
    result exactly once at statement completion; `cede NoSourcePlace` and a
    dereferenced owning payload reject;
11. `self` accepts the same morphology and H/P permission grammar as ordinary
    parameters, with identical method, trait, TKI, Evidence, and CodeGen
    interpretation;
12. `cede self` matches only a direct-value receiver and `cede ^self` matches
    only `(cede ^owner)`; no receiver-specific morphology inference exists;
13. `cede &self` and `cede &parameter` reject uniformly because `&expression`
    is borrow construction rather than an existing reference-binding selector;
14. general `InvokeExpr(CalleeExpr, Args)` or its reviewed equivalent preserves
    the distinction between `(cede callable)(args)` and
    `cede callable(args)` across Parser, clone, TKI, exporter, and CodeGen;
    the former may consume a named callable source, while the latter rejects as
    `cede NoSourcePlace`;
15. exporters regenerate required receiver/invocation parentheses from AST
    precedence and never depend on `HasParens` for semantic correctness;
16. each partial source is admitted independently for argument, receiver,
    standalone, return, assignment/init, aggregate, match, and closure-capture
    contexts;
17. function and callable result grammar rejects `-> cede T`; function-type
    identity and current TKI contain no return-cede qualifier, while parameter
    and consuming-callable cede remain distinct;
18. the complete return matrix distinguishes Copy keep/invalidate, named
    NonCopy rejection/move, bare temporary, rejected `cede` temporary,
    canonical intrinsic unique return, shared transfer, and borrowed/raw flow;
19. a returned admitted source may discharge its own outstanding parameter
    obligation, while an unrelated temporary cannot discharge it and an
    ordinary owning parameter cannot gain transfer authority from return;
20. generic lowering requirements resolve before acceptance and CodeGen, while
    caller spelling remains source-category/formal driven;
21. receiver and all arguments validate and commit atomically;
22. rejected plans use `PlanOutcome = Rejected(reason)` and
    `SourceDisposition = NoStateChange`, preserving PlaceState, PAL,
    obligation state, cleanup masks, and Drop liability;
23. value production, source disposition, destination, Drop disposition, and
    obligation transition remain independent plan dimensions;
24. Evidence v3 represents standalone/rejected/return plans without fake
    formals or transfer kinds, records actual/formal type, formal morphology
    and H/P requirements, and uses deterministic semantic root identity;
25. provider TKI contains only declaration-side contracts, including full
    parameter/receiver morphology and unqualified result morphology; every
    caller root, path/view/category, eligibility, PAL, and plan fact is computed
    locally;
26. every admitted `CedeExpr` destination carries a matching Sema validated
    root/path/view/purpose/destination plan; missing or inconsistent argument,
    receiver, return, assignment/init, aggregate, match, capture, or standalone
    plans fail closed in CodeGen with no artifact, proven by destination-specific
    fault injection;
27. explicit multi-argument alias/borrow conflicts retain their original
    independent diagnostic and atomicity purpose;
28. the complete pass/fail/warn, conformance, TKI, async, sanitizer, package,
    developer-experience, and release gates pass;
29. obsolete implicit invalidation/batching and return-cede paths are deleted
    before freezing
    the candidate SHA;
30. four published-target artifacts from that exact final SHA pass clean
    relocation and packaged replay; and
31. independent human/agent dogfood finds no P0/P1 caller-spelling,
    diagnostic, cleanup, root/view, or route-parity defect.

## 14. Non-goals

This RFC does not:

- add `ParenExpr` or replace the complete expression grammar; bounded Parser
  and AST changes for receiver morphology and `InvokeExpr` are in scope;
- add a separate `CedeStmt` AST node or require a user-facing `drop(value)`
  function; standalone discard remains `ExprStmt(CedeExpr)`;
- remove compiler/runtime destructor, release, cleanup-mask, or Drop-liability
  machinery;
- generalize partial moves or place calculus;
- add user-visible lifetime syntax;
- introduce `@must_use`, a linear-result type, or any caller obligation through
  function-result `cede`;
- weaken PAL, H/P capability, async boundary, raw/unsafe, or dependency rules;
- make every transfer in the language use the `cede` keyword;
- delete temporary transfer planning or CodeGen liability checks;
- change the callee obligation to consume a `cede` parameter;
- select a new ABI; or
- authorize implementation before review.

## 15. Stop conditions

Implementation must stop and return to RFC review if any of these occur:

- generic source/ownership/Copy/obligation facts cannot be represented and
  replayed without unresolved behavior reaching CodeGen;
- explicit receiver preparation cannot avoid pre-validation PAL/PlaceState
  mutation;
- a rejected multi-argument call can partially invalidate a source;
- an accepted explicit `cede` leaves a Copy source available;
- an ordinary formal accepts explicit `cede`, or a cede formal accepts a bare
  named actual;
- any user-written `cede NoSourcePlace` is admitted because of its destination,
  or a compiler temporary gains authority through a synthetic user-semantic
  `CedeExpr`;
- function-result `cede` survives in grammar, function-type identity, current
  TKI, or Evidence after activation, or parameter/callable cede is accidentally
  removed with the overloaded storage;
- old return enforcement is removed before the complete return source planner
  proves that bare named NonCopy values cannot become implicit moves;
- `return cede ^owner` is accepted instead of the single intrinsic spelling
  `return ^owner`;
- an unrelated return temporary discharges another parameter's outstanding
  obligation, or an ordinary captured parameter gains transfer authority;
- any route treats `cede buf` as transfer or destruction of `^buf`, erases a
  required hat, or silently manufactures owner identity from a payload view;
- owner invalidation leaves any solely derived payload/member/index view live,
  or commits while an overlapping derived borrow remains active;
- receiver morphology differs from ordinary parameter morphology, or trait/TKI
  replay loses a receiver hat or H/P permission;
- `cede &parameter` or `cede &self` is accepted without a separately reviewed
  unambiguous reference-binding identity selector;
- Parser, clone, exporter, or CodeGen conflates `(cede callable)(args)` with
  `cede callable(args)`, or relies on `HasParens` for semantic grouping;
- a standalone destination is encoded as value production, rejection is
  encoded as transfer, or a rejected plan carries a state-changing source
  disposition;
- provider TKI serializes or authorizes caller-local root/path/view/category,
  PAL, eligibility, or plan facts;
- eligibility proven for one `CedeExpr` route is reused by another route
  without its independent proof;
- any admitted `CedeExpr` destination reaches CodeGen without a matching
  Sema-validated root/path/view/purpose/destination plan, or missing-plan
  failure still emits an artifact;
- Evidence emits a process-local root identifier or omits the selected formal's
  resolved type, morphology/view, or H/P requirements;
- a `NoSourcePlace` expression is treated as an invalidatable source, or an
  owned temporary loses/double-owns cleanup;
- temporary cleanup responsibility cannot be transferred without reintroducing
  implicit named-place invalidation;
- any behavior/cleanup commit lands after the SHA used for final qualification;
  or
- the interface/cache boundary cannot reliably reject the old contract.

## 16. Review checklist

The reviewer must explicitly decide each item before implementation:

- [ ] accept the call-boundary constitutional rule;
- [ ] accept exact written `TransferOrigin` plus reviewed `InvalidateRegion`
      for both Copy and NonCopy values;
- [ ] accept payload/handle spellings as views over one ownership root:
      `cede ^buf` invalidates the owner and dependent reachability, while
      `cede buf` cannot transfer or discard it;
- [ ] accept that active derived payload/member/index borrows block owner-root
      invalidation;
- [ ] accept strict handshake: ordinary formal + explicit `cede` rejects, and
      `cede` formal + bare named actual rejects;
- [ ] accept user `cede` as source invalidation only: bare `NoSourcePlace`
      values use compiler transfer plans and explicit `cede` on them rejects in
      every destination;
- [ ] accept separate Copy-rvalue and whole-owned-temporary NoSourcePlace rows;
- [ ] accept `resource.dup()` only when it proves an independent eligible
      temporary;
- [ ] accept ownership/identity classification before Copy classification;
- [ ] accept generic caller spelling independent of Copy proof, with all
      lowering facts resolved before CodeGen;
- [ ] accept standalone `cede value` for direct values and `cede ^owner` for
      unique owner handles, with exactly-once terminal cleanup;
- [ ] accept that `self` completely reuses ordinary parameter morphology,
      permissions, admission/rejection, `cede`, trait, TKI, Evidence, and
      CodeGen rules;
- [ ] accept rejection of `cede &parameter` and `cede &self` until an
      unambiguous reference-binding selector exists;
- [ ] accept `(cede value).method()` for `cede self` and
      `(cede ^owner).method()` for `cede ^self`, with exact handshake;
- [ ] accept general `InvokeExpr(CalleeExpr, Args)` and the semantic distinction
      between consuming `(cede callable)(args)` and rejected
      `cede callable(args)`;
- [ ] accept precedence-driven exporter grouping rather than semantic reliance
      on `HasParens`;
- [ ] accept Stage-0 unified plan-first atomic commit and destination-complete
      CodeGen fail-closed protection;
- [ ] accept independent plan outcome, value production, transfer origin,
      source, destination, Drop, and obligation dimensions;
- [ ] accept obligation before/action/after as independent from payload and
      source disposition;
- [ ] accept Evidence v3 standalone/rejected representation, provider-only TKI
      declarations, caller-local source planning, historical-only v2 replay,
      and an interface-key bump on activation;
- [ ] accept deletion of `-> cede T`/`dyn fn(...) -> cede T`, the complete
      return source matrix, removal of the old E0464 meaning, and preservation
      of parameter/consuming-callable cede;
- [ ] accept stable semantic Evidence root coordinates rather than process-local
      symbol IDs, plus complete formal type/morphology/H-P facts;
- [ ] accept independent eligibility for argument, receiver, standalone,
      return, assignment/init, aggregate, match, and closure-capture routes;
- [ ] accept the fixture migration categories and reject bulk warning-to-fail
      conversion;
- [ ] accept retention of temporary plans, PAL, destination-complete
      fail-closed checks, and explicit atomic batching;
- [ ] accept deletion before the final candidate SHA and complete requalification
      after every cleanup change; and
- [ ] accept the 38–61 person-day / 8–12 week implementation budget.

Until every required item is reviewed, this RFC remains Revisions requested and
current compiler behavior remains unchanged.
