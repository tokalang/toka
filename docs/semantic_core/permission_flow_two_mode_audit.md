# Audit: Current Implementation Against Two-Mode Permission Flow

**Audit target:** the working implementation after `24651565`
(`docs(sema): specify two-mode permission flow`)

**Audit date:** 2026-07-26

**Result:** the static authority layer is implemented and evidenced. A first
direct-source Shared-flow slice is implemented for local initialization,
assignment/call use, returns, and field initialization; the full two-mode RFC
remains incomplete and must not be represented as a closed 1.0 guarantee.

## Evidence basis

- `python3 tools/run_conformance.py`: 41 passed, 0 failed on 2026-07-26;
- isolated source-less replay for `permission_001_capability` and
  `permission_002_shared_flow`: 2 passed, 0 failed on 2026-07-26;
- static permission logic: `include/toka/Sema.h`,
  `src/Sema/Sema_Expr.cpp`, `src/Sema/Sema_Expr_Binary.cpp`, and call checking;
- transfer logic: `src/Sema/Sema_Expr.cpp` (`CedeExpr`),
  `src/Sema/Sema_Stmt.cpp` (initialization), and
  `src/Sema/Sema_Expr_Call.cpp` (cede parameters);
- type compatibility: `src/Sema/Sema_Type.cpp`;
- pattern binding: `src/Sema/Sema_Expr_Init.cpp`, `src/Sema/Sema_Stmt.cpp`,
  and `src/Sema/Sema_Expr.cpp`.

## Findings

| RFC rule | Current evidence | Assessment |
|---|---|---|
| Iron rule: a shared view never amplifies Payload authority | `PermissionFlow` derives a one-hop RHS fact. Fresh local Shared bindings retain a payload ceiling, and declaration boundaries enforce it for payload assignment, mutable calls, returns, and fields. | **Partial**: patterns and nullable/independent-transfer semantics remain open. |
| Static authority is declaration/signature-backed; syntax is intent only | `BindingPermission`, `AccessCapability`, and `AccessIntent` route ordinary assignment and calls through declaration facts. The conformance suite includes direct, call, receiver, and raw negative cases. | **Conforms (Layer 1)** |
| Existing assignment cannot redeclare H/P | Existing targets are checked against their declared access capability. A later assignment does not update the target declaration. | **Conforms (Layer 1)** |
| `cede` requires invalidation and marks a source moved | `CedeExpr` calls PAL invalidation for a path and marks an underlying variable moved; cede parameters have dedicated call checks. | **Partial**: useful prerequisites exist, but the rule applies generically rather than only to classified whole independent sources. |
| Independent sources re-root a fresh binding under referent ceilings | Initializers record the new declaration permission and move unique variables. | **Partial**: there is no Independent classifier, no universal referent-ceiling fact carried through transfer, and no explicit whole-binding rule. |
| Shared sources cannot gain payload authority | `~`/`&` classification uses the direct RHS capability. Negative tests cover a two-hop Shared chain, `cede ~`, mutable call arguments, return signatures, and fields; `permission_002_shared_flow` repeats field/signature evidence source-less. Safe raw payload access remains gated by `unsafe`. | **Partial**: pattern propagation remains open. |
| Nullable to non-null `cede` requires a same-path guard | Generic type compatibility models only broad nullability covariance. | **Does not conform**: no cede-specific guard/dominance proof exists. |
| Fields/freeze ceilings survive independent flow | `BindingPermission` contains blocked flags and member checks compute some final flags. | **Does not conform**: no audited transfer fact establishes a persistent referent ceiling across all initializer/call/return paths. |
| Patterns use identical flow derivation | `checkPattern` accepts `SourceIsMutable`, but match and guard binding currently pass `false`; pattern binders derive their own attributes. | **Does not conform**: pattern flow is outside the classifier and may not re-root or preserve capabilities consistently. |
| Partial moves are excluded until modeled | Member-resource move checks exist in local initialization. | **Partial**: cede itself accepts general expressions and only has narrow root marking for member paths. There is no formal per-field move state. |
| `.tki` replay preserves flow facts | The static declaration/signature facts have replay coverage. | **Open**: no two-mode flow fact or dedicated replay matrix exists. |

## Concrete implementation gaps

1. `CedeExpr` in `src/Sema/Sema_Expr.cpp` still performs generic invalidation
   and move marking. `PermissionFlow` now classifies its direct RHS for local
   initialization, but cede is not yet restricted to a formal whole-binding
   independent-transfer rule and it has no nullable proof.
2. Local initialization in `src/Sema/Sema_Stmt.cpp` now stores a one-hop
   Shared payload ceiling without rewriting the LHS declaration. Return and
   field boundaries validate the same direct capability, but pattern binders
   do not yet store or validate that fact.
3. `isTypeCompatible` in `src/Sema/Sema_Type.cpp` is morphology/type
   compatibility. It cannot serve as the authority-flow decision because it is
   intentionally bidirectional in several non-permission cases and has no
   path, move, guard, or PAL inputs.
4. Pattern creation in `src/Sema/Sema_Expr_Init.cpp` creates declaration facts
   from the pattern marker without a source capability input. Match and guard
   callers currently supply `false` for `SourceIsMutable`.

## Recommended implementation order

1. Extend the current internal `PermissionFlow` classification/result to
   pattern binders. It should retain mode (`Independent`, `Shared`,
   `UnsafeRaw`), direct payload ceiling, null-state, and source path only.
2. Implement **whole-binding unique `cede` only**. Require `^`/owned source,
   PAL invalidation, non-null proof, and source invalidation; derive the fresh
   binding from its declaration capped by the carried ceiling.
3. Route `~`, `&`, and safe `*` through one Shared derivation and add negative
   tests for local, call, return, receiver, field, and pattern promotion.
4. Add canonical-path null guard evidence. Initially reject unsupported guarded
   forms rather than guessing.
5. Bring pattern binding and `.tki` replay under the same flow representation.
   Leave partial moves rejected until per-field state and drop semantics are
   designed explicitly.

## Release conclusion

`PERM-STATIC-01` may remain a verified static guarantee. `OWN-FLOW-01` and
`OWN-FLOW-02` are proposed rules with partial prerequisites only. The language
gap should remain **in progress** until the listed negative tests and source/
`.tki` evidence pass.
