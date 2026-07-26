# Audit: Current Implementation Against Two-Mode Permission Flow

**Audit target:** the working implementation after writable-reference
destructuring closure.

**Audit date:** 2026-07-26

**Result:** the static authority layer is implemented and evidenced. A first
direct-source Shared-flow slice is implemented for local initialization,
assignment/call use, returns, field initialization, match/guard pattern
binding, and readonly-reference destructuring; the full two-mode RFC remains
incomplete and must not be represented as a closed 1.0 guarantee.

## Evidence basis

- `python3 tools/run_conformance.py`: 46 passed, 0 failed on 2026-07-27;
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
| Iron rule: a shared view never amplifies Payload authority | `PermissionFlow` derives a one-hop RHS fact. Fresh local Shared bindings retain a payload ceiling, and declaration boundaries enforce it for payload assignment, mutable calls, returns, fields, match/guard binders, and destructuring reference fields. `permission_002_shared_flow` now verifies source-backed/source-less agreement for readonly and writable imported reference fields. | **Partial**: source-less pattern/guard plus nullable/independent-transfer semantics remain open. |
| Static authority is declaration/signature-backed; syntax is intent only | `BindingPermission`, `AccessCapability`, and `AccessIntent` route ordinary assignment and calls through declaration facts. The conformance suite includes direct, call, receiver, and raw negative cases. | **Conforms (Layer 1)** |
| Existing assignment cannot redeclare H/P | Existing targets are checked against their declared access capability. A later assignment does not update the target declaration. | **Conforms (Layer 1)** |
| `cede` requires invalidation and marks a source moved | `CedeExpr` calls PAL invalidation for a path and marks an underlying variable moved; cede parameters have dedicated call checks. | **Partial**: useful prerequisites exist, but the rule applies generically rather than only to classified whole independent sources. |
| Independent sources re-root a fresh binding under referent ceilings | Initializers record the new declaration permission and move unique variables. | **Partial**: there is no Independent classifier, no universal referent-ceiling fact carried through transfer, and no explicit whole-binding rule. |
| Shared sources cannot gain payload authority | `~`/`&` classification uses the direct RHS capability. Negative tests cover a two-hop Shared chain, `cede ~`, mutable call arguments, return signatures, fields, match/guard reference patterns, and a readonly-reference destructuring field. A positive run test shows a declared writable reference field preserves—not invents—Payload permission. `permission_002_shared_flow` repeats field/signature plus readonly/writable destructuring evidence source-less. Safe raw payload access remains gated by `unsafe`. | **Partial**: source-less pattern/guard coverage remains open. |
| Nullable to non-null `cede` requires a same-path guard | Generic type compatibility models only broad nullability covariance. | **Does not conform**: no cede-specific guard/dominance proof exists. |
| Fields/freeze ceilings survive independent flow | `BindingPermission` contains blocked flags and member checks compute some final flags. | **Does not conform**: no audited transfer fact establishes a persistent referent ceiling across all initializer/call/return paths. |
| Patterns and destructuring use direct flow derivation | Match and guard callers derive `PermissionFlow` from their target and pass the direct capability to `checkPattern`. Destructuring computes the initializer flow once, records each binder's own `BindingPermission`, and applies each handle field's declared P as its one-hop ceiling. Imported readonly/writable reference-field destructuring has source-backed/source-less replay. | **Partial**: source-less pattern/guard replay and writable-reference guard CodeGen still need dedicated coverage. |
| Partial moves are excluded until modeled | Member-resource move checks exist in local initialization. | **Partial**: cede itself accepts general expressions and only has narrow root marking for member paths. There is no formal per-field move state. |
| `.tki` replay preserves flow facts | `permission_002_shared_flow` checks interface emission, source-backed/source-less semantic-evidence equality, a writable imported reference-field run case, and readonly imported reference-field rejection. | **Partial**: no source-less pattern/guard matrix or general two-mode transfer replay exists. |

## Concrete implementation gaps

1. `CedeExpr` in `src/Sema/Sema_Expr.cpp` still performs generic invalidation
   and move marking. `PermissionFlow` now classifies its direct RHS for local
   initialization, but cede is not yet restricted to a formal whole-binding
   independent-transfer rule and it has no nullable proof.
2. Local initialization and destructuring in `src/Sema/Sema_Stmt.cpp`, plus
   match/guard binding in `src/Sema/Sema_Expr_Init.cpp`, store a one-hop
   payload ceiling without rewriting the declaration. Destructuring also
   preserves the binder's declared `BindingPermission`, so a writable
   reference field retains its declared Payload capability while a readonly
   field remains capped. `permission_002_shared_flow` replays both outcomes
   against a source-less imported shape interface.
3. `isTypeCompatible` in `src/Sema/Sema_Type.cpp` is morphology/type
   compatibility. It cannot serve as the authority-flow decision because it is
   intentionally bidirectional in several non-permission cases and has no
   path, move, guard, or PAL inputs.
4. Source-less `.tki` replay covers imported reference-field destructuring,
   but does not yet exercise match/guard flow propagation.

## Recommended implementation order

1. Add source-less replay for pattern and guard propagation.
2. Implement **whole-binding unique `cede` only**. Require `^`/owned source,
   PAL invalidation, non-null proof, and source invalidation; derive the fresh
   binding from its declaration capped by the carried ceiling.
3. Route `~`, `&`, and safe `*` through one Shared derivation and add negative
   tests for local destructuring and receiver promotion.
4. Add canonical-path null guard evidence. Initially reject unsupported guarded
   forms rather than guessing.
5. Leave partial moves rejected until per-field state and drop semantics are
   designed explicitly.

## Release conclusion

`PERM-STATIC-01` may remain a verified static guarantee. `OWN-FLOW-01` and
`OWN-FLOW-02` are proposed rules with partial prerequisites only. The language
gap should remain **in progress** until the listed negative tests and source/
`.tki` evidence pass.
