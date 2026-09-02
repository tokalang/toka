# RFC: Explicit `cede` at Call Boundaries

**Status:** Proposed for review. This document authorizes no implementation,
source migration, interface-key change, CI run, merge, tag, or release. Work
may begin only after an explicit review changes this status to an accepted
implementation boundary.

**Target:** Toka 1.0 call-boundary ownership semantics, if accepted.

**Draft baseline:** `3d32808a9f34e1fdf9c4c36dac9facc5284a0ac2`.

**Prospective supersession:** on activation only, this RFC supersedes the
caller-spelling portion of
[`RC9 Signature-Driven Call Transfer ADR`](rc9_signature_driven_call_transfer_adr.md)
and revises [`OWN-CEDE-001`](rule_matrix.md#own-cede-001-cede-formals-are-signature-driven-transfer-obligations).
Those documents remain the historical and currently implemented contract until
all acceptance gates in this RFC pass.

## 1. Decision boundary

The proposed Toka 1.0 rule is:

> At a call boundary, every transfer that invalidates an existing source place
> must spell `cede` at the caller. A complete, independently owned temporary
> with `NoSourcePlace` may transfer without caller spelling. A proven `@Copy`
> call never invalidates its source.

The shorter design slogan is:

> **A call boundary must show `cede` when the call invalidates a source place.**

This is intentionally a call-boundary rule. It is not a claim that `cede` is
the only visible invalidation operation in the language. Existing forms such
as error propagation through `result!`, direct unique-handle transfer, explicit
return transfer, aggregate transfer, and consuming captures retain their own
visible operators and contracts.

The RFC restores caller-visible ownership loss without deleting the internal
planning required for temporary cleanup transfer, PAL validation, atomic
multi-argument rejection, or fail-closed CodeGen.

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
the desired Toka 1.0 surface. This RFC changes that policy prospectively while
preserving the proven transfer, cleanup, PAL, and CodeGen boundaries.

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
availability can be changed. The absence of `makeAccessPath()` is necessary
but not sufficient. The classifier must prove all of the following:

- the expression produces one complete value, not a projection or selected
  subobject;
- the value is owned by the full expression;
- the value has no borrowed referent or raw identity standing in for ownership;
- no lifetime or member dependency escapes with the value;
- cleanup/drop liability is known; and
- the transfer consumes the full temporary exactly once.

A borrowed temporary, raw identity, place alias, dependency-bearing expression,
or incompletely described generic value does not enter this exemption merely
because it lacks a stable display path.

### 3.3 `WholeOwnedTemporary`

A `WholeOwnedTemporary` is the admitted `NoSourcePlace` subset whose complete
payload and cleanup liability can be transferred to the selected `cede`
formal. It may use an internal synthetic `CedeExpr` or an equivalent validated
transfer plan. That representation is compiler authority, not implicit caller
place invalidation.

### 3.4 Copy proof

Copy classification has exactly three semantic states:

```text
ProvenCopy | ProvenNonCopy | Indeterminate
```

Absence of a visible drop hook is not a Copy proof. Generic and source-hidden
calls must use the same proof source as source-backed calls.

### 3.5 Proposed semantic rule identities

These identifiers are stable review handles. They enter `rule_matrix.md` only
if the RFC is accepted and qualified.

| Rule ID | Proposed status | Operation class | Decision summary |
| --- | --- | --- | --- |
| `OWN-CALL-EXPLICIT-001` | Core guarantee | `CedeObligation`, `OwnershipTransfer`, `Invalidation` | a call may invalidate a `NamedSourcePlace` only when the caller spells `cede` |
| `OWN-CALL-COPY-001` | Core guarantee | `OwnershipTransfer` | a `ProvenCopy` call keeps its source live; explicit `cede` Copy rejects |
| `OWN-CALL-TEMP-001` | Conservative rejection | `OwnershipTransfer` | implicit owning transfer is limited to proven `NoSourcePlace + WholeOwnedTemporary`; all uncertain cases reject |
| `OWN-CALL-GENERIC-001` | Conservative rejection | `CedeObligation`, `InterfaceReplay` | symbolic generic requirements must resolve to Copy/NonCopy/source-less facts before acceptance and CodeGen |
| `OWN-CALL-ATOMIC-001` | Core guarantee | `OwnershipTransfer`, `Invalidation` | receiver and all arguments prepare and validate together, then commit once or not at all |

The shared compiler inputs are the selected formal, caller spelling, exact
source/referent place, value category, Copy proof, ownership/drop facts,
capabilities, and dependencies. Interface replay must reproduce the same final
decision. Primary diagnostics, implementation areas, and test classes are
specified in Sections 7–13; no diagnostic number is allocated by this draft.

## 4. Proposed normative matrix

The selected formal still determines whether the callee borrows or accepts an
ownership-bearing value. Caller spelling determines whether an existing source
place may be invalidated.

| Selected formal and actual | Caller spelling | Transfer disposition | Source disposition | Proposed result |
| --- | --- | --- | --- | --- |
| non-`cede` formal + place/value | bare | `BorrowCapture` or ordinary value flow | `KeepLive` | allow under existing rules |
| non-`cede` formal + place/value | explicit `cede` | none | no state change | reject under existing `cede`-to-borrowed rules |
| `cede` formal + `ProvenNonCopy` `NamedSourcePlace` | bare | none | no state change | reject; caller must write `cede` |
| `cede` formal + `ProvenNonCopy` `NamedSourcePlace` | explicit `cede` | `MoveOwned` or `TransferShared` | `InvalidatePlace(exact path)` | allow after whole-call validation |
| `cede` formal + `ProvenCopy` place | bare | `CopyValue` | `KeepLive` | allow |
| `cede` formal + `ProvenCopy` place | explicit `cede` | none | `KeepLive` | reject: `cede` cannot truthfully invalidate Copy |
| `cede` formal + admitted `WholeOwnedTemporary` | bare | `ConsumeTemporary` | `NoSourcePlace` | allow |
| `cede` formal + admitted `WholeOwnedTemporary` | explicit `cede` | `ConsumeTemporary` | `NoSourcePlace` | allow for source compatibility; same plan as bare |
| `cede` formal + borrowed/raw/dependency-bearing actual | bare or explicit | existing `CopyIdentity`/boundary decision | existing safe disposition only | no temporary exemption; preserve existing capability, PAL, unsafe, and dependency checks |
| `cede` formal + unresolved generic classification | bare or explicit | symbolic requirement only | no committed change | reject unless the requirement resolves before acceptance/CodeGen |

### 4.1 Copy decision

This RFC proposes a hard 1.0 rule:

- a proven `@Copy` actual passed bare remains live;
- an explicit `cede copyValue` is an error; and
- no call route may use explicit spelling to destructively read a proven Copy
  source.

This prospectively reverses the RC9 row locked by
`copy_explicit_invalidates.tk`. Treating explicit `cede` as destructive for a
Copy value would give `cede` two incompatible meanings: visible ownership loss
for non-Copy places, but an optional forced invalidation for Copy. The proposed
1.0 contract chooses one meaning and rejects the misleading spelling.

The exact diagnostic code and whether a migration-only warning precedes the
hard error are review items; the semantic result is not.

### 4.2 Borrowed and raw identities

The `NoSourcePlace` exemption does not grant transfer authority to a borrowed
temporary or raw identity. These expressions retain their current independent
checks:

- referent/place identity;
- payload and handle capability ceilings;
- lifetime/member dependencies;
- unsafe provenance;
- execution-boundary restrictions; and
- PAL conflicts.

This RFC neither widens nor silently rejects every existing `CopyIdentity`
route. It changes only caller spelling for a plan that would invalidate an
existing source place. Any accepted identity-copy/`KeepLive` route remains a
non-invalidating call and therefore does not require `cede` under this rule.

## 5. Generic decision

Generic classification remains necessary, but an unresolved symbolic plan is
never source-state or CodeGen authority.

At generic definition checking, the compiler may retain these symbolic
requirements:

| Generic actual category | Caller spelling | Symbolic requirement |
| --- | --- | --- |
| named place | bare | `RequiresProvenCopy` |
| named place | explicit `cede` | `RequiresProvenNonCopy` and an admitted exact place |
| source-less expression | bare or explicit | `RequiresWholeOwnedTemporary` or a concrete non-owning value rule |
| borrowed/raw/dependency-bearing expression | either | preserve the existing capability/dependency predicate |

The requirement must resolve after substitution and before the call is
accepted:

- `RequiresProvenCopy` accepts only a concrete `ProvenCopy` result and keeps
  the source live;
- `RequiresProvenNonCopy` accepts only a concrete `ProvenNonCopy` result and
  commits the explicit exact-place invalidation;
- a contradicting proof rejects the instantiation; and
- an `Indeterminate` result after substitution or source-less replay rejects
  fail-closed.

This preserves useful symbolic generic analysis without allowing a symbolic
`InvalidatePlace` to mutate PAL/PlaceState or reach CodeGen. TKI must serialize
enough formal, Copy-bound, substitution, and source-category information to
solve the same requirement when the declaration body is not available.

The exact surface spelling of any new Copy/NonCopy constraint is outside this
RFC. Implementation must stop before activation if existing declarations and
TKI facts cannot express the required proof without adding unreviewed syntax.

## 6. Consuming receiver syntax

For a method whose selected receiver formal is `cede self`, a named non-Copy
receiver uses:

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
| named `ProvenNonCopy` receiver selected by `cede self` | `value.consume()` | reject; visible receiver `cede` required |
| named `ProvenNonCopy` receiver selected by `cede self` | `(cede value).consume()` | plan and atomically invalidate after validation |
| admitted `WholeOwnedTemporary` selected by `cede self` | `make_value().consume()` | `ConsumeTemporary`; no caller place to invalidate |
| named `ProvenCopy` receiver selected by `cede self` | `value.consume()` | copy and `KeepLive` |
| named `ProvenCopy` receiver selected by `cede self` | `(cede value).consume()` | reject redundant/false `cede` |
| borrowed/raw/dependency-bearing receiver | either | preserve existing receiver capability/dependency decision; no temporary exemption |

Consuming callable receivers, async receivers, partial direct-field receivers,
and source-hidden methods must converge on the same matrix before activation.

## 7. Whole-call planning and atomic commit

The receiver change cannot be implemented as a late syntax check around the
current state-mutating `CedeExpr` path. Today, checking a `CedeExpr` may update
PAL and PlaceState before final method selection and argument validation. The
new contract requires one receiver-plus-arguments transaction.

The proposed semantic pipeline is:

```text
resolve callable and selected formals
    -> prepare receiver and argument facts without global mutation
    -> derive spelling/copy/source/dependency facts
    -> plan every transfer and symbolic generic requirement
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
ValueCategory = NamedSourcePlace | NoSourcePlace | Indeterminate
SourcePlace?
ReferentPlace?
DependencyRoots[]
CopyProof = ProvenCopy | ProvenNonCopy | Indeterminate
OwnershipKind
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
    | SharedLiabilityIncremented | NoLiability
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
7. Nested candidate/overload/generic probes discard all prepared state when
   not selected.
8. Source-backed and source-less calls produce the same final plan.

Existing rollback guards may participate, but rollback after observable
mutation is not a substitute for a plan-first authority boundary when a pure
prepared fact is available.

## 8. What implementation may remove and must retain

### 8.1 Remove only after activation gates pass

- the branch that accepts a bare `NamedSourcePlace` for a `cede` formal and
  synthesizes `InvalidatePlace`;
- the matching default-allow W0409 policy for accepted bare named moves; and
- batching code whose sole purpose is committing multiple implicit named-place
  invalidations.

### 8.2 Retain

- `ConsumeTemporary` planning and Drop-liability handoff;
- an internal synthesized `CedeExpr` for temporaries, or an equivalent
  structured transfer plan;
- Copy and identity classification;
- symbolic generic requirements;
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

- A bare non-Copy named argument to an ordinary `cede` formal uses `E04570` or
  its explicitly reviewed successor.
- Method arguments retain `E04509` or converge on the ordinary code only after
  a diagnostic-compatibility review.
- A bare named receiver selected by `cede self` requires a dedicated diagnostic
  or an explicitly reviewed reuse; the RFC does not reserve a number.
- Explicit `cede` on a proven Copy actual requires a new hard diagnostic.
- An unresolved generic requirement requires a fail-closed diagnostic naming
  the missing Copy/NonCopy/source-category proof.

Callee-side `E0474`, return-side explicit `cede`, partial-move diagnostics, PAL
diagnostics, and `E0761` remain independent.

### 9.2 Fixes

A machine-applicable insertion of `cede` is allowed only when the compiler has
already proved:

- one exact source place;
- `ProvenNonCopy`;
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
so W0409 no longer describes an accepted core-language path. Temporary and Copy
calls must never be converted to that error merely because they target a
`cede` formal.

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
| temporary, proven Copy, borrowed identity, or already explicit path | remain positive or retain its existing independent rejection |
| runtime fixture whose purpose is transfer/drop behavior | add explicit `cede` only at named invalidating calls; keep runtime qualification |
| multi-argument alias/borrow/atomic fixture | add explicit `cede` to intended transfer arguments and continue proving all-or-nothing failure |
| mixed runtime plus use-after-move fixture | split or locally rewrite; do not turn the entire file into compile-fail |
| E0761 fault-injection fixture | retain missing-plan CodeGen fail-closed purpose |
| TKI/source-less fixture | retain source/TKI plan parity purpose |
| Evidence fixture | update schema expectations without discarding caller/callee/return stages |
| async/extern/callable/dynamic/static/generic route | retain route-specific runtime and atomicity coverage |

A repository-wide W0409 census must be regenerated immediately before
implementation begins. Counts outside this 80-fixture directory are discovery
data, not frozen migration scope.

## 11. Evidence, TKI, and cache boundary

### 11.1 Evidence

`toka.cede-obligation-evidence` v2 is bound to the RC9 caller-implicit contract
and must not be silently reinterpreted. Activation requires a new schema
version or a separately named protocol that can express:

```text
caller_spelling = bare | explicit
value_category  = named_source_place | no_source_place | indeterminate
copy_proof      = proven_copy | proven_noncopy | indeterminate
requirement     = none | requires_copy | requires_noncopy
transfer        = borrow | copy_value | copy_identity
                  | move_owned | transfer_shared | consume_temporary
source          = keep_live | invalidate_place(path) | no_source_place
commit          = rejected | planned | committed
```

Caller spelling, callee consumption, and return transfer remain distinct
stages. Evidence may describe a rejected symbolic requirement but may not grant
semantic authority.

### 11.2 TKI and source-less replay

TKI replay must preserve or recompute:

- formal `cede` bits and consuming receiver mode;
- Copy bounds/proofs and generic symbolic requirements;
- dependency/member contracts;
- exact source-category requirements needed at the call site; and
- the same caller-spelling decision as source-backed resolution.

An imported declaration may not restore the RC9 implicit named-place rule due
to missing body or Copy facts. Any unresolved requirement rejects.

### 11.3 Compatibility key

Activation is a source-semantic compatibility break and requires a new
compiler-interface key. The RFC does not select its literal value. If the TKI
wire representation changes, its format/schema version must also change. Old
cache entries must reject rather than replay RC9 caller-spelling semantics
under the new compiler.

No physical ABI change is implied solely by the spelling rule.

## 12. Staged implementation plan and estimate

The estimate is for one engineer after RFC acceptance.

### Specification freeze — 1–2 person-days

- accept or reject the proposed Copy rule;
- accept the generic symbolic-requirement/fail-closed rule;
- freeze receiver spelling and atomic commit boundary;
- select diagnostic identities and Evidence versioning; and
- record the interface/cache compatibility decision.

### Stage 1: ordinary parameters — 5–8 person-days

- enforce caller `cede` for named non-Copy places;
- preserve Copy `KeepLive` and `NoSourcePlace` temporary consumption;
- migrate direct/static/generic/extern/indirect/callable/async argument routes;
- retain all-argument PAL/alias validation and E0761; and
- classify the 80-fixture migration rather than bulk-editing warnings.

No public semantic activation occurs at the end of Stage 1. Receiver and
source-less parity remain required.

### Stage 2: `cede self` receivers — 6–10 person-days

- accept `(cede value).method()` through the existing Parser AST;
- reject bare named non-Copy consuming receivers;
- preserve whole temporary and Copy receiver rows;
- plan receiver and arguments in one atomic group;
- migrate `unwrap`, `unwrap_err`, `into_iter`, consuming callable, partial
  receiver, async, and source-hidden routes; and
- prove rejected receiver/argument combinations commit no PAL/PlaceState/drop
  change.

### Stage 3: protocols, qualification, and cleanup — 4–7 person-days

- version Evidence and update AI completion/diagnostic documentation;
- update TKI/source-less replay and the compiler-interface key;
- complete source, TKI, async, extern, callable, generic, and CodeGen fault
  matrices;
- run full local and four-target release qualification;
- migrate examples/tools/packages and perform independent dogfood; and
- only after all gates pass, delete the implicit `InvalidatePlace` branch and
  batching code that has no remaining purpose.

### Total

```text
1–2 + 5–8 + 6–10 + 4–7 = 16–27 person-days
```

Expected single-engineer schedule: approximately 3–5 weeks. This is a bounded
semantic migration, not a parser rewrite or compiler rewrite.

## 13. Activation gates

One exact revision must satisfy all of the following before this RFC can become
the active Toka 1.0 contract:

1. ordinary, method, static, generic, extern, indirect `fn`/`dyn fn`,
   `@Callable`, dynamic-trait, async, `.start`, thread handoff, and source-hidden
   routes use the same matrix;
2. every named non-Copy invalidating call requires visible caller `cede`;
3. every named proven Copy call stays live, and explicit `cede` Copy rejects;
4. every bare admitted temporary is proven `NoSourcePlace + WholeOwned` and
   transfers cleanup exactly once;
5. borrowed/raw/dependency-bearing expressions do not enter the temporary
   exemption;
6. generic symbolic requirements resolve before acceptance and CodeGen;
7. receiver and all arguments validate and commit atomically;
8. rejected calls preserve PlaceState, PAL, cleanup masks, and drop liability;
9. explicit multi-argument alias/borrow conflicts retain their original
   independent diagnostic and atomicity purpose;
10. valid `(cede value).method()` and source-less consuming receiver cases have
    source, TKI, runtime, and exactly-once-drop coverage;
11. Evidence uses a new version and source/TKI outputs agree;
12. E0761 fault injection still fails closed with no artifact;
13. the complete pass/fail/warn, conformance, TKI, async, sanitizer, package,
    developer-experience, and release gates pass;
14. four published-target candidate artifacts pass clean relocation and
    packaged replay; and
15. independent human/agent dogfood finds no P0/P1 caller-spelling,
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

- generic Copy/NonCopy requirements cannot be represented and replayed without
  unresolved behavior reaching CodeGen;
- explicit receiver preparation cannot avoid pre-validation PAL/PlaceState
  mutation;
- a rejected multi-argument call can partially invalidate a source;
- Copy source liveness differs between source and TKI paths;
- temporary cleanup responsibility cannot be transferred without reintroducing
  implicit named-place invalidation; or
- the interface/cache boundary cannot reliably reject the old contract.

## 16. Review checklist

The reviewer must explicitly decide each item before implementation:

- [ ] accept the call-boundary constitutional rule;
- [ ] accept `(cede value).method()` as the consuming named-receiver form;
- [ ] accept `NoSourcePlace + WholeOwnedTemporary` as the only implicit owning
      transfer exemption;
- [ ] accept bare proven Copy as `KeepLive`;
- [ ] accept explicit `cede` on proven Copy as a hard error;
- [ ] accept symbolic generic requirements with mandatory pre-CodeGen
      resolution and fail-closed `Indeterminate`;
- [ ] accept explicit `cede` on an owned temporary as source-compatible and
      semantically equivalent to bare temporary consumption;
- [ ] accept receiver-plus-arguments plan-first atomic commit;
- [ ] accept Evidence versioning and an interface-key bump on activation;
- [ ] accept the fixture migration categories and reject bulk warning-to-fail
      conversion;
- [ ] accept retention of temporary plans, PAL, E0761, and explicit atomic
      batching; and
- [ ] accept the 16–27 person-day / 3–5 week implementation budget.

Until every required item is reviewed, this RFC remains Proposed and current
compiler behavior remains unchanged.
