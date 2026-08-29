# RC9 Signature-Driven Call Transfer ADR

**Decision status:** Accepted for RC9.

**Implementation status:** Partially implemented behind
`--experimental-signature-driven-cede`. Qualified behavior-changing slices now
cover source-backed, same-lexical ordinary direct calls and static calls with
one resolved formal, plus the equivalent single-argument method route. Whole
locals and whole temporaries are admitted. The same bounded behavior now covers
source-backed user `@Callable` values, including qualified unique places, and
local indirect `fn`/`dyn fn` values with one aggregate cede parameter. Indirect
unique closure parameters remain outside this slice because closure ascription
does not yet preserve that parameter morphology. Method `cede ^T` uses the same
moved-handle value ABI as ordinary/static calls; both its explicit legacy form
and implicit experimental form have native runtime qualification. The RC8
caller-explicit policy remains the default until every activation gate in this
ADR passes at one qualified revision.

Resolved generic functions/methods and source-hidden `.tki` declarations now
use the same final-call elaboration. Qualified async calls and `.start`, async
multi-argument handoff, unique extern parameters, and
`thread_spawn_with_state` owned state have native runtime qualification.
In experimental mode, capturing callable values passed to plain `thread_spawn`
fail closed with `E04570` instead of using the legacy Copy exemption.

Ordinary same-lexical direct calls also have a bounded all-bare
multi-argument slice. Sema first checks every actual and all pairwise PAL
relations, then elaborates all qualified non-Copy transfers together. A type,
borrow, alias, or place-admission failure leaves every planned source live.
Calls containing explicit argument-level `cede`, mutable/rebindable formals,
defaults, `init`, generic, async, outcome, or dependency contracts remain
outside this multi-argument slice.

**Baseline:** `v1.0.0-rc.8` /
`997713f4828b43a5b82aa3363d99a37e9e6f2417`.

**Decision date:** 2026-08-27.

## Decision

After overload, generic, trait, callable, and interface resolution selects a
formal parameter, that resolved formal determines whether the call boundary is
borrowing or ownership-taking. A caller is not required to repeat an
ownership-taking formal's `cede` spelling at the argument site.

An explicit argument-level `cede` remains legal when the selected formal is
ownership-taking. It is a source-use request, not a second declaration of the
callee contract. For a non-Copy transferable place it confirms the same exact
place invalidation selected by the formal. For a proven `@Copy` place it
requests an otherwise optional destructive read and invalidates that place.

Compiler policy may expose implicit invalidating calls through a lint. The
language default is `allow`; a project may promote the lint to warning or
error. The lint applies only to an implicit plan whose source disposition is
`InvalidatePlace`. It does not report `CopyValue`, `CopyIdentity`, or
`ConsumeTemporary`.

This is an accepted RC9 semantic change, not a reinterpretation of the RC8
contract. On activation it prospectively supersedes only the caller-spelling
part of:

- [`1_0_freeze_decision_list.md`](../1_0_freeze_decision_list.md)'s rule that
  both caller and callee must spell the `cede` contract; and
- [`OWN-CEDE-001`](rule_matrix.md#own-cede-001-cede-parameters-are-explicit-transfer-obligations),
  whose RC8 source form is `f(cede value)`.

Those documents remain the historical normative record for RC8. Their caller
rule must be marked `Superseded by this RC9 ADR` only when the activation gates
pass. The historical EXP-LIN-01 result remains confirmed for RC8: the RC8
implementation and its frozen unconditional caller-spelling rule diverged.

## Normative transfer matrix

`@Copy` below means a compiler-proven Copy fact. Absence of an explicit drop
hook is not a Copy proof. A transferable place is an exact place admitted by
the current whole- or partial-transfer rules; this ADR does not widen that
set.

| Selected formal and actual | Transfer disposition | Source disposition |
| --- | --- | --- |
| non-`cede` aggregate formal + bare place | `BorrowCapture` | `KeepLive` |
| non-`cede` formal + argument-level `cede` | Reject with `E04640` | No state change |
| `cede` formal + proven non-Copy transferable exact place, bare or explicit | `MoveOwned` | `InvalidatePlace(exact path)` |
| `cede` formal + shared-handle transferable place, bare or explicit | `TransferShared` | `InvalidatePlace(exact path)` |
| `cede` formal + proven `@Copy` place, bare | `CopyValue` | `KeepLive` |
| `cede` formal + proven `@Copy` place, explicit | `CopyValue` | `InvalidatePlace(exact path)` |
| `cede` formal + borrowed view place, bare | `CopyIdentity` | `KeepLive`; validate dependency |
| `cede` formal + borrowed view place, explicit | `CopyIdentity` | `InvalidatePlace(view path)`; validate dependency |
| `cede` formal + raw identity place, bare | `CopyIdentity` | `KeepLive`; retain raw-unsafe fact |
| `cede` formal + raw identity place, explicit | `CopyIdentity` | `InvalidatePlace(raw binding)`; retain raw-unsafe fact |
| `cede` formal + admitted whole temporary, bare or explicit | `ConsumeTemporary` | No source place |
| `cede` formal + indeterminate or insufficiently described actual | Reject fail-closed | No state change |

An explicit `cede` on a temporary is accepted and has the same observable
semantic plan and drop liability as its bare form. It is not a language error.
An implementation or style tool may later offer a non-default cosmetic lint,
but that lint is outside this decision.

## Place eligibility is unchanged

This decision changes caller spelling, not which places can move. The exact
place must first satisfy the existing transfer rules.

- An eligible whole local may transfer.
- The admitted partial-transfer subset remains bounded to the current local
  direct-field and fixed-array constant-index model.
- Dynamic or container indexes, general or nested projections, spreads, enum
  payload projections, place aliases, nonlocal places, and custom-drop
  partial aggregates remain rejected where the current model cannot prove an
  atomic source-state and cleanup-mask transition.
- A rejected transfer commits no `PlaceState`, PAL, cleanup-mask, or drop-
  liability change.

The capability matrix and atomic partial-move requirements remain owned by
[`partial_cede_lifecycle_rfc.md`](partial_cede_lifecycle_rfc.md).

## Boundary coverage

The selected-formal rule applies uniformly after resolution to:

- ordinary and static functions;
- ordinary and static methods;
- generic functions and methods after instantiation;
- indirect function values and `@Callable` argument parameters;
- extern declarations;
- async calls;
- `.start` handoff; and
- `thread_spawn` and equivalent execution-boundary calls.

Execution-boundary dependency rules remain an independent axis. Borrowed,
raw, or dependency-bearing state does not become transferable because a
formal is `cede` or because the caller wrote `cede`. On activation, `.start`
owned handoff uses the matrix above and no longer separately requires an
argument-level `cede`; its selected formal must still be `cede`, and its
dependency and `@Send` gates remain unchanged.

This ADR does not change:

- consuming callable receiver syntax: a `cede fn(...)` value or
  `call(cede self, ...)` receiver is still invoked with `cede callable()`;
- the callee's `E0474` obligation to consume, forward, store, return, or
  otherwise complete a `cede` parameter;
- explicit destructive reads in local initialization, assignment, return,
  aggregate fields, or closure captures;
- explicit `cede` return contracts; or
- the EXP-LIN-02 rule rejecting implicit duplication of an owning local value.

## Two-phase generic planning

An uninstantiated generic definition uses a symbolic transfer plan. It may not
assume that a type parameter is Copy merely because no concrete drop hook is
visible. A generic body that forwards a potentially ownership-bearing `cede`
parameter must retain an explicit transfer operation or an equivalent
conditional semantic fact.

After instantiation, the caller plan is recomputed from the selected concrete
formal, the actual value category, the formal `@Copy` proof, exact-place
eligibility, and dependency facts. Source-less interfaces must carry enough
information to reproduce the same result. Explicit `cede` is not a substitute
for missing ownership metadata: an actually indeterminate plan rejects.

## Sema planning and atomic commit

Removing the missing-caller-`cede` diagnostic alone is unsound. RC8 source
invalidation and CodeGen cleanup suppression still depend on `CedeExpr` in
multiple paths. The behavior change may activate only after all call routes
share this sequence:

1. Resolve the callable and select every formal parameter.
2. Classify every actual's value category, proven Copy fact, dependency fact,
   and exact-place eligibility.
3. Produce a logical plan for every argument without changing compiler state.
4. Validate all argument plans together, including PAL conflicts, overlapping
   paths, capability ceilings, dependency escape, and execution-boundary
   requirements.
5. If any plan rejects, commit no source invalidation or drop change.
6. Otherwise atomically commit all `PlaceState`, PAL, source disposition, and
   drop-liability transitions.
7. CodeGen executes the committed plan and fails closed if a liability-bearing
   edge has no plan.

The current proposed implementation architecture for satisfying this sequence
is
[`RC9 M1b-D.3 Ordinary Direct-Call Vertical Slice`](rc9_m1b_d3_direct_call_vertical_slice.md).
It retains the atomicity and immutable-plan requirements while rejecting the
standalone synthetic semantic engine. M1a shadow vectors remain audit
prototypes, not commit authority.

The plan records logical behavior, not LLVM ABI or a required physical
lowering. Equivalent implementations may use different slots, masks, loads,
or calling conventions while preserving the same observable source state and
drop liability.

## Evidence and tooling

[`toka.cede-obligation-evidence` v1](../cede_obligation_evidence_v1.md) is
frozen with an explicit-spelling caller contract. Its meaning must not change
silently. RC9 therefore provides the separately versioned
[`cede obligation evidence v2`](../cede_obligation_evidence_v2.md) through
`--cede-obligations=v2`. Each caller record represents these independent facts:

```text
spelling = implicit | explicit
transfer = BorrowCapture | CopyValue | CopyIdentity
           | TransferShared | MoveOwned | ConsumeTemporary
source   = KeepLive | InvalidatePlace(path) | NoSourcePlace
```

It also retains the selected formal's contract location and the callee-
consumption and return-transfer stages. V1 remains available through
`--cede-obligations=json`; consumers must continue to reject unknown versions
rather than guess.

The implicit-call-move compiler lint is available through
`--warn-implicit-call-move`. Its default remains `allow`, and it reports only
implicit invalidating place moves, not Copy or temporary consumption. LSP
inlay hints remain optional follow-on tooling and are not a safety proof.

Default activation remains blocked by four concrete gaps: dynamic-trait method
lowering does not yet share the typed argument ABI, indirect unique closure
ascription does not preserve parameter morphology, implicit partial-projection
transfer is not qualified, and all-bare atomic batching has not yet been
extended from ordinary direct calls to every alternate route.

## Activation gates

This decision becomes the active 1.x call contract only when one revision
satisfies all of the following:

- ordinary, static, method, generic, callable, extern, async, `.start`, and
  thread handoff paths consume one Sema-owned plan;
- rejected multi-argument calls prove zero partial state mutation;
- implicit whole-place moves and every admitted exact partial place have
  source and source-less use-after-move, borrow-conflict, and exactly-once drop
  evidence;
- bare and explicit forms have identical observable plans for non-Copy owning
  places and whole temporaries;
- proven Copy bare/explicit source-disposition differences are test-locked;
- dependency-bearing and raw execution-boundary cases remain rejected;
- source and `.tki` builds produce the same plan;
- CodeGen rejects a liability-bearing call without a plan;
- cede obligation evidence v2 and the implicit-call-move lint are available;
- all existing callee-consumption, cede-return, callable-receiver, partial-
  move, async cancellation, and cleanup suites pass; and
- the full RC9 cold and hosted qualification gates pass.

Until then, `E04570`/`E04509` and the existing explicit-caller policy remain
active by default. The experimental direct/method/static slices may replace
those diagnostics only inside their closed admission domains. They elaborate a
bare proven-non-Copy transfer to the existing `CedeExpr` invalidation and
CodeGen drop-suppression path; a bare proven-Copy place stays live, and all
excluded routes retain their legacy diagnostic. Pre-activation Evidence v1
modes reject combination with this experimental behavior rather than
misreport caller spelling.

The indirect `fn`/`dyn fn` implementation previously accepted a bare
non-exempt argument to a cede-qualified function type without invalidating its
source. That was neither the frozen RC8 rule nor the accepted RC9 rule. It now
fails with `E04570` in default mode and performs the qualified implicit transfer
in experimental mode.

## Compatibility

Argument-level `cede` accepted by this matrix remains source-compatible. A
project that requires visible ownership handoff may enforce the implicit-call-
move lint without changing the core language.

The `cede` bit is part of function, method, callable, and interface contracts.
Adding or removing it from a public formal is a source-breaking API change.
Overload resolution must not distinguish otherwise identical candidates only
by their `cede` bit.

Parameter ownership flow is invariant: `fn(cede T)` and `fn(T)` are not
implicitly substitutable in either direction, including function values,
`dyn fn`, trait methods, and source-hidden interfaces.

This ADR freezes the decision direction for RC9. It does not freeze a physical
ABI, mandate a TKI version bump, or claim implementation qualification before
the activation gates pass.
