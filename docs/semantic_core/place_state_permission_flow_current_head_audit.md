# Current-HEAD Audit: PlaceState, Permission Flow, and Bounded Partial `cede`

**Status:** Historical release evidence remains at
`8d680fea4a9301cec21efc310a73a15ce4eb8157`; the subsequent frozen P0.4
exact-place carrier and bounded current-HEAD conformance qualification are
complete. This remains a fail-closed repair record, not a claim about the
async/place bridge or a future package release.

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
| whole stable local | ordinary whole-place rules | `ExactPlaceFacts::whole` | drop/init flags |
| direct record field | local binding of a struct/tuple, at most 64 fields, no explicit drop, no shared member | admitted projection fact in `ExactPlaceFacts` | field drop-mask bit where cleanup is non-trivial |
| fixed-array constant index | local fixed array, in range, at most 64 elements | admitted projection fact in `ExactPlaceFacts` | array drop-mask bit where cleanup is non-trivial |
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

### Whole places: qualified exact-place carrier

`SymbolInfo::ExactPlace` represents the three bounded whole-place facts, and
Sema snapshots, restores, and joins it with PAL and direct-flow restrictions
on the admitted control-flow paths. The delayed initialization and Outcome P1
slices use the exact `Never` and `Live` preconditions; a whole `cede` records
`Moved`. Compatibility `InitMask` and `Moved` fields remain metadata at legacy
boundaries rather than deciding an admitted exact-place transition.

### Partial projections: bounded lifecycle slice in the carrier

Admitted projection liveness is carried by `ExactPlaceFacts` and one
Sema-elaborated `PartialMovePlan`; runtime cleanup remains the separate
lowering `DropMask`. The frozen direct-field/fixed-array matrix therefore has
one static eligibility/fact source across Sema joins and CodeGen handoff.
`InitMask` remains a derived compatibility view, not an authority source.
This does not extend the matrix to nested, dynamic, custom-drop, shared-member,
or over-limit projections.

### Deliberate exclusions

This audit does not qualify partial state across `.await`, terminal
cancellation, custom-drop aggregates, dynamic/container indices, nested
projections, or a bodyless `TKI + object` provider. Those remain separately
fail-closed until the async/place bridge and Semantic Manifest Level B are
qualified.

## Decision and next implementation step

The bounded synchronous partial-`cede` surface and frozen P0 PlaceState Core
are complete with source/source-less evidence and the current 226/0
conformance closure. The whole-place synchronous `init` P1 and narrow Outcome
P1 are already implemented. The next decision gate is whether to make the
Canonical Declaration Witness declaration comparison importer-visible; it must
first close its identity and tamper-matrix audit without granting bodyless
provider authority. The async/place bridge, broader projection admission, and
package release qualification remain separate tracks.
