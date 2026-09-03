# RFC: Explicit `cede` at Call Boundaries

**Status:** Revisions requested. Second-round design draft. This document
authorizes no implementation, source migration, interface-key change, CI run,
merge, tag, or release. Work may begin only after an explicit review changes
this status to an accepted implementation boundary.

**Target:** Toka 1.0 call-boundary ownership semantics, if accepted.

**Draft baseline:** `3d32808a9f34e1fdf9c4c36dac9facc5284a0ac2`.

**Prospective supersession:** on activation only, this RFC supersedes the
caller-spelling portion of
[`RC9 Signature-Driven Call Transfer ADR`](rc9_signature_driven_call_transfer_adr.md)
and revises [`OWN-CEDE-001`](rule_matrix.md#own-cede-001-cede-formals-are-signature-driven-transfer-obligations).
Those documents remain the historical and currently implemented contract until
all acceptance gates in this RFC pass.

## 1. Decision boundary

The revised proposed Toka 1.0 rules are:

> `cede place` is a destructive read of an existing source place and always
> makes that source place unavailable. Copy changes how the outgoing value is
> produced; it does not cancel invalidation.

> A selected `cede` formal requires every existing source-place actual to spell
> `cede`. An eligible expression with `NoSourcePlace` is passed bare; its value
> is consumed or copied without a caller place to invalidate.

The shorter design slogan is:

> **Named source: write `cede`, source becomes unavailable. No source place:
> pass bare.**

This is intentionally a call-boundary rule. It is not a claim that `cede` is
the only visible invalidation operation in the language. Existing forms such
as error propagation through `result!`, direct unique-handle transfer, explicit
return transfer, aggregate transfer, and consuming captures retain their own
visible operators and contracts.

Call boundaries are strict contract handshakes. A non-`cede` formal rejects an
explicit `cede` actual with `E04640` and commits no state change. A caller that
intends to end a value after one final ordinary call writes the two operations
explicitly, for example `inspect(value)` followed by `drop(cede value)`.

The RFC restores caller-visible ownership loss without deleting the internal
planning required for temporary cleanup transfer, Copy lowering, obligation
tracking, PAL validation, atomic multi-argument rejection, or fail-closed
CodeGen.

### 1.1 User-facing model

Writing code:

> **A `cede` formal receives a named source as `cede source`; a temporary,
> literal, or other proven `NoSourcePlace` expression is passed bare.**

Reading code:

> **If the current call contains `cede source`, that exact source becomes
> unavailable. If an accepted call passes `source` bare, that call does not
> invalidate the source.**

The second statement is about PlaceState availability. It does not promise
that a mutable parameter leaves the payload unchanged, or that independent PAL
and lifetime restrictions disappear after the call. Other visible language
operations such as `result!`, direct unique transfer, return transfer, and
explicit drop retain their own invalidation rules.

Examples:

```toka
consume(cede resource)       // hand over the named source; resource dies here
consume(resource.dup())      // hand over an independent temporary; resource lives
consume(Resource::new())     // source-less owned temporary
consume(42)                  // source-less Copy value
inspect(resource)            // ordinary handshake; this call keeps the place live
inspect(cede resource)       // E04640; mismatch commits no invalidation
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

### 3.2 `NoSourcePlace`

`NoSourcePlace` is a value-producing expression with no caller place whose
availability can be changed. It says nothing by itself about ownership, Copy,
referent identity, dependencies, or cleanup. The absence of
`makeAccessPath()` is necessary but not sufficient evidence for this category.

The semantic model must keep these facts separate:

```text
SourceCategory
    NamedSourcePlace | NoSourcePlace | Indeterminate

OwnershipKind
    PlainValue | OwnedValue | SharedOwner
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
payload and cleanup liability can be transferred to the selected `cede`
formal. It may use an internal synthetic `CedeExpr` or an equivalent validated
transfer plan. That representation is compiler authority, not implicit caller
place invalidation.

Passing `resource.dup()` bare is valid only when `dup()` produces a complete,
independently owned, dependency-free `NoSourcePlace` result. The original
`resource` stays live; the new temporary is consumed. Conceptually:

```toka
auto <compiler-temporary> = resource.dup()
consume(cede <compiler-temporary>)
```

The source language omits the impossible-to-name `cede` because the compiler
temporary cannot be used after the call.

### 3.4 Copy proof

Copy classification has exactly three semantic states:

```text
ProvenCopy | ProvenNonCopy | Indeterminate
```

Absence of a visible drop hook is not a Copy proof. Generic and source-hidden
calls must use the same proof source as source-backed calls.

Copy proof changes only payload production:

```text
cede copy_place     = CopyValue + InvalidatePlace(copy_place)
cede noncopy_place  = MoveOwned + InvalidatePlace(noncopy_place)
```

Both source places become unavailable. A bare Copy read outside a required
`cede` position remains an ordinary `CopyValue + KeepLive` operation.

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

### 3.6 Proposed semantic rule identities

These identifiers are stable review handles. They enter `rule_matrix.md` only
if the RFC is accepted and qualified.

| Rule ID | Proposed status | Operation class | Decision summary |
| --- | --- | --- | --- |
| `OWN-CALL-EXPLICIT-001` | Core guarantee | `CedeObligation`, `OwnershipTransfer`, `Invalidation` | `cede place` always invalidates that exact source place; a selected `cede` formal requires it for every source-place actual |
| `OWN-CALL-COPY-001` | Core guarantee | `OwnershipTransfer`, `Invalidation` | Copy selects `CopyValue`; it never overrides invalidation requested by `cede` |
| `OWN-CALL-TEMP-001` | Conservative rejection | `OwnershipTransfer` | a bare actual for a selected `cede` formal is limited to a proven safe `NoSourcePlace` value; owning temporaries transfer cleanup exactly once |
| `OWN-CALL-GENERIC-001` | Conservative rejection | `CedeObligation`, `InterfaceReplay` | generic spelling depends on source category and selected formal, not Copy; every unresolved lowering fact rejects before commit/CodeGen |
| `OWN-CALL-ATOMIC-001` | Core guarantee | `OwnershipTransfer`, `Invalidation` | receiver and all arguments prepare and validate together, then commit once or not at all |

The shared compiler inputs are the selected formal, caller spelling, exact
source/referent place, value category, Copy proof, ownership/drop facts,
obligation before/after, capabilities, and dependencies. Interface replay must
reproduce the same final decision. Primary diagnostics, implementation areas,
and test classes are specified in Sections 7–13; no diagnostic number is
allocated by this draft.

## 4. Proposed normative matrix

The selected formal determines whether the callee receives a cede obligation.
The presence of a source place determines caller spelling. An explicit
`CedeExpr` accepted by the selected call contract always invalidates its
source. An ordinary formal rejects that expression before invalidation.

| Selected formal and actual | Caller spelling | Transfer disposition | Source disposition | Proposed result |
| --- | --- | --- | --- | --- |
| non-`cede` formal + place/value | bare | `BorrowCapture` or ordinary value flow | `KeepLive` | allow under existing rules |
| non-`cede` formal + any actual | explicit `cede` | none | no state change | reject with `E04640`; callee did not accept ownership/obligation |
| `cede` formal + any transferable `NamedSourcePlace` | bare | none | no state change | reject; caller must write `cede`, independent of Copy proof |
| `cede` formal + `ProvenNonCopy` owning/shared `NamedSourcePlace` | explicit `cede` | `MoveOwned` or `TransferShared` | `InvalidatePlace(exact path)` | allow; callee obligation becomes `Outstanding` |
| `cede` formal + `ProvenCopy` value `NamedSourcePlace` | explicit `cede` | `CopyValue` | `InvalidatePlace(exact path)` | allow; callee obligation becomes `Outstanding` |
| `cede` formal + admitted non-Copy `WholeOwnedTemporary` | bare | `ConsumeTemporary` | `NoSourcePlace` | allow; callee assumes cleanup and obligation |
| `cede` formal + `ProvenCopy + NoSourcePlace` value | bare | `CopyValue` | `NoSourcePlace` | allow; callee obligation becomes `Outstanding` |
| any formal + `NoSourcePlace` expression | explicit `cede` | none | `NoSourcePlace` | reject: `cede` requires an existing source place |
| owning `cede` formal + borrowed/raw identity | bare or explicit | none | no state change | reject; use the corresponding borrow/identity contract |
| any route + unresolved source/ownership/Copy/liability fact | bare or explicit | none | no committed change | reject fail-closed before CodeGen |

### 4.1 Contract handshake

At a call boundary, caller and callee must agree. A named actual spells `cede`
only when the selected formal is `cede`. The source result is then invariant:

```text
cede NamedSourcePlace => InvalidatePlace(exact path)
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
drop(cede resource)
```

This prevents a call such as `log_task(cede task)` from silently discarding a
task merely because the caller mistook a borrow API for an ownership-taking
one. Changing an API from `cede T` to ordinary `T` is intentionally
source-breaking at its callers because the ownership handshake changed.

### 4.2 Copy decision

This RFC proposes a hard 1.0 rule:

- a bare ordinary read of a proven `@Copy` source is `CopyValue + KeepLive`;
- `cede copyValue` is `CopyValue + InvalidatePlace(copyValue)`;
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

## 5. Generic decision

Generic caller spelling depends on source category and the selected formal,
not on Copy proof:

```toka
fn outer<T>(cede value: T) {
    inner<T>(cede value)
}
```

This spelling is valid for both Copy and NonCopy instantiations. After
substitution:

- Copy selects `CopyValue + InvalidatePlace(value)`;
- NonCopy ownership selects `MoveOwned + InvalidatePlace(value)`;
- the outer obligation is discharged/transferred and the inner parameter
  begins `Outstanding`; and
- borrowed/raw/indeterminate instantiations reject.

Generic classification therefore remains necessary for payload, identity,
drop, dependency, and CodeGen selection, but it no longer chooses whether a
named caller writes `cede`. A symbolic generic plan may record unresolved
lowering requirements during definition checking; no unresolved
`TransferDisposition`, `SourceDisposition`, obligation transition, or Drop
liability may commit or reach CodeGen. Substitution and source-less replay must
resolve every fact or reject fail-closed.

## 6. Consuming receiver syntax

For a method whose selected receiver formal is `cede self`, every admitted
named receiver uses:

```toka
(cede value).consume()
```

No Parser change is required. The current Parser produces:

```text
MethodCallExpr(
    Object = CedeExpr(VariableExpr(value))
)
```

There is no `ParenExpr`. Grouping parentheses are removed, and the retained
expression records `HasParens`. The method suffix then wraps that expression as
the `MethodCallExpr::Object`. Transfer authority comes from the `CedeExpr`, not
from `HasParens`; the parentheses provide the required source grouping.

The receiver matrix mirrors ordinary arguments:

| Receiver | Spelling | Proposed result |
| --- | --- | --- |
| any admitted named receiver selected by `cede self` | `value.consume()` | reject; visible receiver `cede` required regardless of Copy proof |
| named non-Copy receiver selected by `cede self` | `(cede value).consume()` | `MoveOwned/TransferShared + InvalidatePlace`; transfer/create obligation |
| named Copy receiver selected by `cede self` | `(cede value).consume()` | `CopyValue + InvalidatePlace`; transfer/create obligation |
| admitted `WholeOwnedTemporary` selected by `cede self` | `make_value().consume()` | `ConsumeTemporary`; no caller place to invalidate |
| `ProvenCopy + NoSourcePlace` receiver selected by `cede self` | `make_copy().consume()` | `CopyValue + NoSourcePlace`; create obligation |
| any `NoSourcePlace` receiver | `(cede make_value()).consume()` | reject: no source place exists to invalidate |
| borrowed/raw/dependency-bearing receiver | either | preserve/reject under identity, capability, dependency, and PAL rules; no temporary exemption |

An ordinary `self` method rejects `(cede value).method()` because it did not
accept a consuming receiver contract. A caller that wants one final ordinary
method call followed by destruction writes `value.method()` and then an
explicit owning/drop sink.

Consuming callable receivers use the same grouping rule:

```toka
(cede callable)(arguments)
```

`cede callable(arguments)` continues to mean a `CedeExpr` applied to the call
result. Signature inference must never reinterpret it as consuming the callable
receiver. Bare source-less callable temporaries remain eligible only when their
ownership, environment, dependencies, and cleanup plan are proven.

Async receivers, partial direct-field receivers, owning callables, and
source-hidden methods must converge on the same matrix before activation.

## 7. Whole-call planning and atomic commit

The receiver change cannot be implemented as a late syntax check around the
current state-mutating `CedeExpr` path. Today, checking a `CedeExpr` may update
PAL and PlaceState before final method selection and argument validation. The
new contract requires one receiver-plus-arguments transaction, including
contract-mismatch rejection before any `cede` mutation.

The proposed semantic pipeline is:

```text
resolve callable and selected formals
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

At minimum, every receiver and argument contributes:

```text
ResolvedType
SourceCategory = NamedSourcePlace | NoSourcePlace | Indeterminate
SourcePlace?
ReferentPlace?
DependencyRoots[]
CopyProof = ProvenCopy | ProvenNonCopy | Indeterminate
OwnershipKind
WholeOwnedTemporaryEligibility
CedeObligationBefore = None | Outstanding | Discharged
DropLiability
AccessCapabilities
CallerSpelling = Bare | ExplicitCede
SelectedFormalContract
```

### 7.2 Required plan dimensions

The implementation may retain current AST elaboration or adopt a structured
side table, but the logical plan must preserve these independent dimensions:

```text
TransferDisposition
    BorrowCapture | CopyValue | CopyIdentity
    | MoveOwned | TransferShared | ConsumeTemporary

SourceDisposition
    KeepLive | InvalidatePlace(exact path) | NoSourcePlace

DropDisposition
    SourceRetainsLiability | CalleeAssumesLiability
    | DestinationAssumesLiability
    | SharedLiabilityIncremented | NoLiability

ObligationDisposition
    None | CreateForCallee | TransferToCallee
    | DischargeToReturn | DischargeToStorage
    | DischargeToDropSink | Preserve | Reject

CedeObligationAfter
    None | Outstanding | Discharged
```

Spelling is an input to plan admission, not a transfer disposition.

### 7.3 Atomicity requirements

1. Explicit `cede` receiver and arguments are prepared without committing
   global PAL/PlaceState changes.
2. All receiver and argument types are checked before any invalidation commits.
3. All pairwise alias/PAL relations are validated as one call.
4. A failed later argument leaves earlier explicit and planned sources live.
5. Equal, ancestor, descendant, active-borrow, and unprovable source conflicts
   reject without cleanup-mask or drop-liability mutation.
6. A successful call commits each exact source at most once.
7. Obligation before/after is committed with the same transaction as source
   and cleanup state; no rejected call may discharge an obligation.
8. A non-`cede` formal receiving an explicit `cede` rejects before source,
   obligation, or cleanup mutation.
9. Nested candidate/overload/generic probes discard all prepared state when
   not selected.
10. Source-backed and source-less calls produce the same final plan.

Existing rollback guards may participate, but rollback after observable
mutation is not a substitute for a plan-first authority boundary when a pure
prepared fact is available.

## 8. What implementation may remove and must retain

### 8.1 Remove before freezing the final qualification SHA

- the branch that accepts a bare `NamedSourcePlace` for a `cede` formal and
  synthesizes `InvalidatePlace`;
- the matching default-allow W0409 policy for accepted bare named moves; and
- batching code whose sole purpose is committing multiple implicit named-place
  invalidations.

Removal is part of the behavior implementation, not post-qualification
cleanup. After removal, the branch must freeze one candidate SHA and run every
activation/release gate on that exact SHA. Any later cleanup commit invalidates
the evidence and requires the full gate again.

### 8.2 Retain

- `ConsumeTemporary` planning and Drop-liability handoff;
- an internal synthesized `CedeExpr` for temporaries, or an equivalent
  structured transfer plan;
- Copy and identity classification;
- obligation before/after classification and generic lowering requirements;
- PAL and pairwise multi-argument validation;
- explicit multi-argument atomic commit;
- partial-place lifecycle eligibility and cleanup-mask correspondence;
- async, extern, indirect `fn`/`dyn fn`, `@Callable`, dynamic-trait, static,
  generic, TKI, and execution-boundary routing;
- CodeGen `E0761` fail-closed protection; and
- fault injection proving that a missing validated liability plan emits no
  executable artifact.

The implementation must not delete all implicit AST representation merely
because implicit named-place invalidation is removed.

## 9. Diagnostics and migration

### 9.1 Primary diagnostics

- A bare named argument to an ordinary `cede` formal uses `E04570` or its
  explicitly reviewed successor, independent of Copy proof.
- Method arguments retain `E04509` or converge on the ordinary code only after
  a diagnostic-compatibility review.
- A bare named receiver selected by `cede self` requires a dedicated diagnostic
  or an explicitly reviewed reuse; the RFC does not reserve a number.
- Explicit `cede` on `NoSourcePlace` requires a diagnostic explaining that
  there is no source place to invalidate.
- An unresolved plan requires a fail-closed diagnostic naming the missing
  source/ownership/Copy/identity/liability fact.

Callee-side `E0474`, return-side explicit `cede`, partial-move diagnostics, PAL
diagnostics, and `E0761` remain independent.

### 9.2 Fixes

A machine-applicable insertion of `cede` is allowed only when the compiler has
already proved:

- one exact source place;
- the selected formal is `cede`;
- the syntax can be rewritten without precedence ambiguity; and
- no multi-argument or receiver grouping judgment is hidden by the fix.

Receiver fixes must produce `(cede value).method()`, not `cede value.method()`.
Alias, borrow, partial-place, generic-indeterminate, and multi-argument failures
must not offer a speculative automatic rewrite.

### 9.3 W0409 transition

`W0409` is the migration inventory for currently accepted implicit
`InvalidatePlace` plans. During implementation it may remain available as an
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

Fixture migration must preserve each test's independent purpose:

| Fixture class | Required migration |
| --- | --- |
| pure bare named-move acceptance probe | become a focused caller-side rejection expecting `E04570`/reviewed equivalent |
| eligible source-less temporary or literal | remain positive and bare; retain CopyValue versus ConsumeTemporary coverage |
| named proven Copy actual selected by `cede` formal | add explicit `cede`, remain positive, and require later use-after-cede rejection |
| explicit Copy invalidation such as `copy_explicit_invalidates.tk` | remain positive and preserve source invalidation |
| borrowed/raw identity or already explicit independent path | retain its separate identity/borrow decision and original test purpose |
| runtime fixture whose purpose is transfer/drop behavior | add explicit `cede` only at named invalidating calls; keep runtime qualification |
| multi-argument alias/borrow/atomic fixture | add explicit `cede` to intended transfer arguments and continue proving all-or-nothing failure |
| mixed runtime plus use-after-move fixture | split or locally rewrite; do not turn the entire file into compile-fail |
| E0761 fault-injection fixture | retain missing-plan CodeGen fail-closed purpose |
| TKI/source-less fixture | retain source/TKI plan parity purpose |
| Evidence fixture | update schema expectations without discarding caller/callee/return stages |
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
caller_spelling = bare | explicit
spelling_reason = ordinary | formal_required | source_less_exempt
selected_formal = ordinary | cede
source_category = named_source_place | no_source_place | indeterminate
ownership_kind  = plain_value | owned_value | shared_owner
                  | borrowed_view | raw_identity
                  | callable_identity | owned_callable | indeterminate
copy_proof      = proven_copy | proven_noncopy | indeterminate
temporary_eligibility = eligible | ineligible | indeterminate
transfer        = borrow | copy_value | copy_identity
                  | move_owned | transfer_shared | consume_temporary
source          = keep_live | invalidate_place(path) | no_source_place
obligation_before = none | outstanding | discharged
obligation_action = none | create_for_callee | transfer_to_callee
                    | discharge_to_return | discharge_to_storage
                    | discharge_to_drop_sink | preserve | reject
obligation_after = none | outstanding | discharged
commit          = rejected | planned | committed
```

Caller spelling, callee consumption, and return transfer remain distinct
stages. Evidence must distinguish `CopyValue + InvalidatePlace` from ordinary
`CopyValue + KeepLive`, and `ConsumeTemporary + NoSourcePlace` from
`CopyValue + NoSourcePlace`. It may describe a rejected unresolved plan but may
not grant semantic authority.

### 11.2 TKI and source-less replay

TKI replay must preserve or recompute:

- formal `cede` bits and consuming receiver mode;
- source category, ownership/identity class, Copy bounds/proofs, and generic
  lowering requirements;
- obligation before/action/after for formals and forwarded parameters;
- dependency/member contracts;
- exact source-category requirements needed at the call site; and
- the same caller-spelling decision as source-backed resolution.

An imported declaration may not restore the RC9 implicit named-place rule due
to missing body or Copy facts. Named caller spelling remains independent of
Copy; any unresolved lowering, obligation, identity, or liability fact rejects.

### 11.3 Compatibility key

Activation is a source-semantic compatibility break and requires a new
compiler-interface key. The RFC does not select its literal value. If the TKI
wire representation changes, its format/schema version must also change. Old
cache entries must reject rather than replay RC9 caller-spelling semantics
under the new compiler.

No physical ABI change is implied solely by the spelling rule.

## 12. Staged implementation plan and estimate

The estimate is for one engineer after RFC acceptance.

### Stage 0: obligation-aware whole-call planner — 8–14 person-days

- freeze `SourceCategory`, `OwnershipKind`, Copy, referent, temporary
  eligibility, Drop liability, and obligation before/after facts;
- build one transaction-local receiver-plus-arguments prepared plan before any
  source or obligation mutation;
- reject ordinary-formal/explicit-`cede` mismatches before destructive reads;
- publish audit/shadow parity without changing accepted source behavior;
- give receiver and arguments equal validated-plan/E0761 fail-closed coverage;
- prove candidate/overload/generic rejection discards the complete transaction;
  and
- select diagnostic identities, Evidence v3, and the TKI/cache boundary.

No caller-spelling behavior changes before Stage 0 demonstrates source-backed,
source-hidden, and CodeGen plan parity.

### Stage 1: parameters, Copy, generic, and identity — 6–10 person-days

- require `cede` for every named actual selected by a `cede` formal,
  independent of Copy proof;
- retain `E04640` for explicit `cede` supplied to an ordinary formal;
- implement `CopyValue + InvalidatePlace` for explicit Copy sources;
- distinguish Copy NoSourcePlace rvalues from owned temporary consumption;
- make ownership/identity classification precede Copy classification;
- migrate direct/static/generic/extern/indirect/async argument routes; and
- classify the 80-fixture migration rather than bulk-editing warnings.

### Stage 2: method and callable receivers — 5–8 person-days

- accept `(cede value).method()` through the existing Parser AST;
- require it for every named receiver selected by `cede self`, including Copy;
- reject explicit receiver cede for ordinary `self` methods before mutation;
- accept `(cede callable)(args)` as the unambiguous consuming callable form;
- preserve proven source-less receiver/callable temporary rows;
- migrate `unwrap`, `unwrap_err`, `into_iter`, partial receiver, async,
  dynamic-trait, and source-hidden routes; and
- prove rejected receiver/argument combinations commit no PAL/PlaceState,
  obligation, or cleanup change.

### Stage 3: protocols, deletion, qualification, and dogfood — 8–13 person-days

- activate Evidence v3 and isolate v2 as explicit historical replay;
- update TKI/source-less replay and the compiler-interface key;
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
8–14 + 6–10 + 5–8 + 8–13 = 27–45 person-days
```

Expected single-engineer schedule: approximately 5–9 weeks. Removing source
compatibility work does not remove the obligation-aware generic model,
whole-call transaction, unified receiver plan, TKI/Evidence work, or final
qualification cost. This remains a bounded semantic migration, not a parser or
compiler rewrite.

## 13. Activation gates

One exact revision must satisfy all of the following before this RFC can become
the active Toka 1.0 contract:

1. ordinary, method, static, generic, extern, indirect `fn`/`dyn fn`,
   `@Callable`, dynamic-trait, async, `.start`, thread handoff, and source-hidden
   routes use the same matrix;
2. every named actual selected by a `cede` formal requires visible caller
   `cede`, independent of Copy proof;
3. every accepted `cede NamedSourcePlace`, including Copy, transitions that
   exact source to unavailable;
4. every bare admitted source-less actual is proven `NoSourcePlace`; Copy
   rvalues use `CopyValue + NoSourcePlace`, while owned temporaries transfer
   cleanup exactly once;
5. explicit `cede` on `NoSourcePlace` rejects, and borrowed/raw/dependency-
   bearing expressions do not enter the owned temporary exemption;
6. an explicit `cede` actual supplied to an ordinary formal rejects with no
   state change;
7. generic lowering requirements resolve before acceptance and CodeGen, while
   caller spelling remains source-category/formal driven;
8. receiver and all arguments validate and commit atomically;
9. rejected calls preserve PlaceState, PAL, obligation state, cleanup masks,
   and drop liability;
10. explicit multi-argument alias/borrow conflicts retain their original
   independent diagnostic and atomicity purpose;
11. valid `(cede value).method()`, `(cede callable)(args)`, and source-less
    consuming receiver cases have
    source, TKI, runtime, and exactly-once-drop coverage;
12. Evidence v3 records obligation before/action/after and source/TKI outputs
    agree; v2 is isolated as historical replay;
13. argument and receiver E0761 fault injection still fails closed with no
    artifact;
14. the complete pass/fail/warn, conformance, TKI, async, sanitizer, package,
    developer-experience, and release gates pass;
15. obsolete implicit invalidation/batching paths are deleted before freezing
    the candidate SHA;
16. four published-target artifacts from that exact final SHA pass clean
    relocation and packaged replay; and
17. independent human/agent dogfood finds no P0/P1 caller-spelling,
    diagnostic, cleanup, or route-parity defect.

## 14. Non-goals

This RFC does not:

- add `ParenExpr` or require a Parser redesign;
- generalize partial moves or place calculus;
- add user-visible lifetime syntax;
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
- [ ] accept `cede NamedSourcePlace => InvalidatePlace` for both Copy and
      NonCopy values;
- [ ] accept strict handshake: ordinary formal + explicit `cede` rejects, and
      `cede` formal + bare named actual rejects;
- [ ] accept bare `NoSourcePlace` values and reject explicit `cede` on them;
- [ ] accept separate Copy-rvalue and whole-owned-temporary NoSourcePlace rows;
- [ ] accept `resource.dup()` only when it proves an independent eligible
      temporary;
- [ ] accept ownership/identity classification before Copy classification;
- [ ] accept generic caller spelling independent of Copy proof, with all
      lowering facts resolved before CodeGen;
- [ ] accept `(cede value).method()` and `(cede callable)(args)` as consuming
      named-receiver forms;
- [ ] accept Stage-0 receiver-plus-arguments plan-first atomic commit and
      receiver-level E0761;
- [ ] accept obligation before/action/after as independent from payload and
      source disposition;
- [ ] accept Evidence v3, historical-only v2 replay, and an interface-key bump
      on activation;
- [ ] accept the fixture migration categories and reject bulk warning-to-fail
      conversion;
- [ ] accept retention of temporary plans, PAL, E0761, and explicit atomic
      batching;
- [ ] accept deletion before the final candidate SHA and complete requalification
      after every cleanup change; and
- [ ] accept the 27–45 person-day / 5–9 week implementation budget.

Until every required item is reviewed, this RFC remains Revisions requested and
current compiler behavior remains unchanged.
