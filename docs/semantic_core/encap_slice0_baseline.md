# `@encap` Epoch Slice 0 Baseline

**Status:** In progress. This document records current behaviour before the
proposed `@encap` epoch changes any semantic decision. It is not a statement
that the proposed rules are implemented or enabled.

**Authority:** [the hybrid-policy RFC](encap_hybrid_policy_rfc.md) defines the
target rules and Slice 0 exit gate. This file is a reproducible inventory of
the legacy inputs that the gate must account for.

## 1. Scope and non-goals

Slice 0 may add resolver-owned shadow metadata, redline fixtures, and audit
reports. It must not alter source acceptance, field visibility, Copy/Dup
selection, drop lowering, TKI format, cache validity, or code generation.

In particular, a shadow coordinate is evidence only. An `@encap` access check
continues to use the current implementation until a later implementation slice
explicitly activates the new policy.

## 2. Current baseline

| Concern | Current implementation fact | Slice 0 consequence |
| --- | --- | --- |
| Module identity | `ModuleResolver` and Sema key modules through canonical source/resolved paths. | A resolver-owned logical coordinate has to be introduced beside, not in place of, the existing path keys. |
| `pub(crate)` | `Sema_Expr_Member.cpp` currently accepts a crate entry without comparing an access-site and defining crate. | Same-crate and cross-crate shadow outcomes must be measured before the rule is enabled. |
| `pub(path)` | `Sema_Expr_Member.cpp` uses `PathUtils::modulePathMatchesTarget` with a physical module filename. | Physical path results are legacy outcomes; a logical segment-prefix result must be computed separately. |
| TKI identity | TKI metadata carries `source_path`; a source-less module may use it as parser/source identity. | Source, generated-TKI, and cached-TKI need one recorded shadow coordinate and a disagreement report. |
| Structural lifecycle | Sema synthesizes `ImplDecl(..., "encap")` with `IsStructuralDrop`; TKI replays `@tki structural_drop`. | Current structural witnesses are intentional legacy behaviour, not evidence for the new `DropPlan` model. |
| Capsule grammar | The parser accepts callable methods and wildcard/exclusion grants inside `impl T@encap`. | Existing library use must be inventoried before the later grammar break. |
| Resource provenance | Package metadata may declare `native.ffi_resources`, but the compiler has no resolver-bound `ResourceContract(TypeDefId)` fact. | Raw allocation, field spelling, and hook text must not be treated as ownership evidence in Slice 0. |
| Copy and duplication | Legacy `clone`, deleted clone declarations, and structural lifetime facts remain active. | Copy-SCC and Copy/Dup coherence redlines are observation/prototype work only. |

The source locations above are implementation waypoints, not permanent
authority. The RFC, redline results, and resolver audit output are the
authoritative Slice 0 evidence.

## 3. Existing evidence to preserve

Slice 0 starts from, and must not regress, the following existing coverage:

- source-less nominal shape identity:
  `tests/semantics/tki_replay/cases/ergonomics_003_shape_identity`;
- generic nominal identity and cache invalidation:
  `tests/semantics/tki_replay/cases/generic_shape_001_module_identity` and
  `tests/semantics/tki_cache/cases/generic_shape_module_identity`;
- current field-visibility examples:
  `tests/import_test/encap_visibility_lib.tk`;
- structural and custom cleanup / partial-`cede` cases:
  `tests/conformance/ownership/` and
  `tests/semantics/tki_replay/cases/permission_005_partial_cede_lifecycle`;
- fixed-array structural cleanup:
  `tests/conformance/ownership/structural_drop_fixed_array_fields.tk` and
  `tests/conformance/ownership/fixed_array_handle_cleanup.tk`; and
- current source/TKI replay and cache validation runners:
  `tools/scripts/test_semantic_replay.sh`,
  `tools/scripts/test_tki_cache_validation.sh`, and
  `tools/scripts/test_semantic_cache_invalidation.sh`.

These cases describe the supported legacy language. A later epoch test may
expect a different result only under a separate, explicit test profile.

## 4. Shadow-coordinate contract

The first implementation vertical slice will record, for every resolved
module, one of:

```text
Known(ModuleCoordinate(CrateId, LogicalModulePath))
Unknown(reason)
```

`Known` requires a resolver graph origin. For a locked package, the opaque
`CrateId` must be derived from the lock/package node identity, not its alias or
installation path. For a toolchain module it comes from the configured
toolchain domain. For a workspace root it comes from a resolver-provided
workspace node. A loose invocation that lacks one of these origins remains
`Unknown`; it is never silently assigned a path-derived crate identity.

The logical module path is derived from the import graph, including normalized
relative imports and package-root submodules. It is independent of the source
file spelling selected for source, generated interface, cache, symlink, or
relocation. Package aliases targeting the same resolved package node therefore
share a coordinate.

Slice 0 must make `Known` / `Unknown` visible in an audit-only machine-readable
report. It must also report the legacy access decision and the proposed shadow
decision; neither result changes the compilation outcome.

## 5. Evidence increments

The work proceeds in these independently reviewable increments:

1. **Baseline inventory** (this document): preserve the legacy test set and
   enumerate all known path-derived decisions.
2. **Resolver shadow identity:** propagate package/workspace/toolchain origin
   and logical import paths through source, generated-TKI, cached-TKI, package,
   overlay, and multi-root resolution without using them for authorization.
3. **Identity redlines:** relocation, symlink, alias, same-crate, cross-crate,
   source-less, cached, missing, and forged metadata matrices; unknown identity
   is reported as fail-closed for the proposed policy.
4. **Lifecycle/resource redlines:** custom-hook tail order and prohibited
   partial move, typed partial-move actions, raw provenance, and forged/missing
   FFI contract metadata.
5. **Copy/Dup redlines:** concrete/generic by-value graph, SCC/layout failure,
   `Unknown` failure, intrinsic/user provider overlap, and deterministic
   blocker-path order.

## 6. Slice 0 stop condition

Slice 0 stops for a go/no-go review, not for activation. The review receives:

- a deterministic resolver identity report for every supported resolver input;
- a classified list of legacy-versus-shadow authorization disagreements;
- source/generated-TKI/cached-TKI parity evidence;
- reviewed lifecycle/resource/Copy-Dup redline outputs; and
- a migration inventory for standard library, official packages, and build
  tooling.

Only after that review may a later slice change parser, Sema, CodeGen, TKI, or
library semantics.
