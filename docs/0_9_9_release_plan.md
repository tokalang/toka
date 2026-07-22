# Toka 0.9.9 release-closure plan

Status: `InProgress`

The 0.9.9 line qualifies the language, compiler, SDK, semantic service, and
machine-facing tooling intended for Toka 1.0 without changing the public
version to 1.0. The first candidate is `0.9.9-01`. Completing this plan freezes
the implementation evidence; it does not itself authorize a `v1.0.0` tag.

No task in this plan may add syntax or expand the frozen language surface.

| ID | Status | Deliverable | Exit evidence |
| --- | --- | --- | --- |
| `R9-1` | `Complete` | Establish the 0.9.9 candidate line | Compiler, interface, SDK tools, diagnostics, package defaults, and documentation report `0.9.9-01` |
| `R9-2` | `Complete` | Close generic-template module identity | `semantic_core/generic_shape_identity_closure.md`: same-name layouts, generic methods, nominal mismatch, source-less TKI, and cache regeneration are isolated |
| `R9-3` | `Complete` | Unify the release gate | The schema-version-2 gate has 13 fail-closed stages including explicit research-fixture quarantine lists, SDK installation, semantic index, LSP, AI contracts, scale/soak, sanitizer, sustained applications, and package smoke; all six tooling sub-gates and deterministic count aggregation pass |
| `R9-4` | `Pending` | Produce current-candidate evidence | A clean local native report passes on the frozen candidate revision; four supported runners execute the identical gate |
| `R9-5` | `Pending` | Reconcile and freeze the ledger | FZ-5, native-build, tooling, release notes, tags, and reports identify historical versus current evidence without contradiction |

## Exit conditions

- `FZ-3-R02` has direct source/TKI/cache evidence and is no longer pending.
- The release gate is fail-closed and has no silent test exclusions.
- Any research fixture exclusion is named in a checked-in list with rationale.
- `tokac`, `toka`, `tokafmt`, and `tokalsp` agree on `0.9.9-01`.
- The candidate package contains and exercises all four tools.
- The candidate revision has a clean native report and is ready for the
  Linux x64/arm64 and macOS x64/arm64 matrix.
- No `1.0.0` version or tag is created by this phase.

Four-platform reports require their native CI runners. Until those artifacts
exist, the repository may describe the candidate as prepared or locally
qualified, but not as a completed final 1.0 release.
