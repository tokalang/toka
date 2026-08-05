# Current-HEAD Audit: PlaceState, Permission Flow, and Bounded Partial `cede`

**Status:** Qualified current-HEAD evidence at
`8d680fea4a9301cec21efc310a73a15ce4eb8157`. This is a conformance audit and
a fail-closed repair record; it does not close the PlaceState Core.

## Audit basis

The prior P-1 baseline was
`b937224aa3a3dc29978967097b40682ca0f6ceae`. This audit qualified the next
candidate revision, which narrows two partial-`cede` forms that were accepted
beyond the frozen mask capability:

- a constant fixed-array index in an array with more than 64 elements; and
- a direct field of a record that contains a shared member, for which CodeGen
  deliberately does not install the field cleanup mask.

Both now fail in Sema with `E04632` before lowering. The accepted synchronous
matrix is therefore:

| Exact place | Admitted condition | Static state | Runtime cleanup |
|---|---|---|---|
| whole stable local | ordinary whole-place rules | `PlaceStateMask` (`Never`, `Live`, `Moved`) | drop/init flags |
| direct record field | local binding of a struct/tuple, at most 64 fields, no explicit drop, no shared member | `InitMask` bit | field drop-mask bit where cleanup is non-trivial |
| fixed-array constant index | local fixed array, in range, at most 64 elements | `InitMask` bit | array drop-mask bit where cleanup is non-trivial |
| any other projection | nested, dynamic, nonlocal, custom-drop, shared-member aggregate, or over-limit | rejected | not lowered |

The new diagnostic is intentionally a capability-boundary error, not a new
authority rule. H/P, direct-flow ceilings, PAL invalidation, and existing
source/destination overlap checks remain independent preconditions.

## Evidence at the candidate revision

| Runner | Result |
|---|---|
| targeted source diagnostics | 2/2 pass (`E04632`) |
| full negative diagnostic suite | 321 passed, 0 failed |
| source-less semantic replay | 32 passed, 0 failed |
| direct-field/fixed-array lifecycle runs | 6 passed, 0 failed |
| complete macOS-arm64 release gate | all 13 stages passed: 397 positive, 321 negative, 32 replay, 13 cache, 100 native/reference cycles, 300 QSLite operations, 81 sanitizer checks, and 12 package checks |

The replay case `permission_005_partial_cede_lifecycle` now carries both new
negative consumers, so source-backed checking and source-less TKI replay agree
that these unsupported forms are rejected. The completion repair at `8d680fea`
also resolves a direct field through the same visible-shape/member-name identity
path that CodeGen uses when the local binding originated in a `match` pattern.
The positive lifecycle runs cover direct-field cleanup/reinitialization/branch
join and fixed-array cleanup/return/loop paths.

## Reconciliation result

### Whole places: qualified implementation substrate

`SymbolInfo::PlaceStateMask` represents the three bounded whole-place facts,
and Sema snapshots, restores, and unions it with move state, `InitMask`, PAL,
and direct-flow restrictions on the admitted control-flow paths. The delayed
initialization and Outcome P1 slices use the exact `Never` and `Live`
preconditions; a whole `cede` records `Moved`. This is the implementation
substrate required by the whole-place synchronous slices.

### Partial projections: bounded lifecycle slice, not PlaceState Core closure

Projection liveness remains a legacy per-member `InitMask`; runtime cleanup is
a separate `DropMask`. A cleared projection bit means that cleanup and later
reads are suppressed, but it does not encode whether absence is
`NeverConstructed` or `Constructed + MovedOut`. Thus the implementation does
not yet carry the RFC's full construction-origin/availability product for each
projection.

The Sema and CodeGen eligibility checks now agree on the frozen rows above,
including direct fields of eligible by-value pattern bindings, but they are
still duplicated checks rather than one shared structured eligibility fact.
The audit treats that duplication as a remaining engineering closure item, not
as proof that future extensions automatically preserve the matrix.

### Deliberate exclusions

This audit does not qualify partial state across `.await`, terminal
cancellation, custom-drop aggregates, dynamic/container indices, nested
projections, or a bodyless `TKI + object` provider. Those remain separately
fail-closed until the async/place bridge and Semantic Manifest Level B are
qualified.

## Decision and next implementation step

The bounded synchronous partial-`cede` surface is now narrower and has fresh
source/source-less and full-release evidence, but **P0 PlaceState Core is not
closed**. The next implementation step is internal only: replace the parallel whole
`PlaceStateMask` and projection `InitMask` authority with one exact-place
fact/eligibility representation, then prove its join and cleanup lowering
without changing this frozen surface. A full release gate at the resulting
candidate revision is required before any broader status can be claimed.
