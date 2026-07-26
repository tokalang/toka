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

- `python3 tools/run_conformance.py`: 51 passed, 0 failed on 2026-07-27;
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
| Iron rule: a shared view never amplifies Payload authority | `PermissionFlow` derives a one-hop RHS fact. Fresh local Shared bindings retain a payload ceiling, and declaration boundaries enforce it for payload assignment, mutable calls, returns, fields, match/guard binders, and destructuring reference fields. `permission_002_shared_flow` verifies source-backed/source-less agreement for readonly and writable imported reference fields, match binders, and guard binders. | **Partial**: nullable/independent-transfer semantics remain open. |
| Static authority is declaration/signature-backed; syntax is intent only | `BindingPermission`, `AccessCapability`, and `AccessIntent` route ordinary assignment and calls through declaration facts. The conformance suite includes direct, call, receiver, and raw negative cases. | **Conforms (Layer 1)** |
| Existing assignment cannot redeclare H/P | Existing targets are checked against their declared access capability. A later assignment does not update the target declaration. | **Conforms (Layer 1)** |
| `cede` requires invalidation and marks a source moved | A whole `^` binding is PAL-invalidated and reports `E0438` after transfer; cede parameters have dedicated call checks. Local resource transfer still supports documented member/index paths, but `PermissionFlow` explicitly classifies those paths as Shared. | **Partial**: per-field move/drop state remains unmodeled. |
| Independent sources re-root a fresh binding under referent ceilings | A fresh unique binding obtains its own declaration permissions and a whole `^` source is invalidated (`cede_unique_creates_independent_owner` and `cede_unique_source_invalidated`). `cede` member/index/spread paths retain their direct payload ceiling rather than becoming independent. | **Partial**: no universal carried referent ceiling exists for frozen/nullable transfer. |
| Shared sources cannot gain payload authority | `~`/`&` classification uses the direct RHS capability. Negative tests cover a two-hop Shared chain, `cede ~`, mutable call arguments, return signatures, fields, match/guard reference patterns, and a readonly-reference destructuring field. Positive run tests show declared writable reference fields and imported `Option<&T#>` match/guard binders preserve—not invent—Payload permission. `permission_002_shared_flow` repeats readonly/writable destructuring, match, and guard outcomes source-less. Safe raw payload access remains gated by `unsafe`. | **Partial**: independent-transfer and nullable semantics remain open. |
| Nullable to non-null `cede` requires a same-path guard | Local initialization rejects a nullable `cede` source for a non-null indirection (`cede_nullable_source_requires_guard`, `E04599`). The existing `guard` narrowing of the same local binding permits the transfer (`cede_nullable_source_after_guard`). | **Partial**: only a direct local binding has guard evidence; member/index paths have no canonical-path proof and remain conservatively rejected. |
| Fields/freeze ceilings survive independent flow | A field's declared `$` ceiling remains attached to its shape after whole-unique transfer: `cede_unique_preserves_blocked_field` rejects the write with `E0443`, and `permission_004_referent_ceiling` repeats that outcome source-less from the exported field declaration. | **Partial**: this proves declared field ceilings; there is no separate general freeze/sealed-object ceiling or all-boundary propagation model. |
| Patterns and destructuring use direct flow derivation | Match and guard callers derive `PermissionFlow` from their target and pass the direct capability to `checkPattern`. Destructuring computes the initializer flow once, records each binder's own `BindingPermission`, and applies each handle field's declared P as its one-hop ceiling. Imported readonly/writable reference-field destructuring, match, and guard paths have source-backed/source-less replay. The match runtime test also covers a local type alias so payload type metadata survives alias lowering. | **Partial**: independent-transfer and nullable semantics remain open. |
| Partial moves do not establish independent authority | `cede` classifies member/index/spread paths as Shared, and a writable destination cannot amplify a readonly member (`cede_member_cannot_rebuild_payload_permission`). | **Partial**: local resource transfer remains supported, but there is no formal per-field move/drop state. |
| `.tki` replay preserves flow facts | `permission_002_shared_flow` checks interface emission, source-backed/source-less semantic-evidence equality, writable imported reference-field, match, and guard run cases, and readonly rejection for each path. `permission_003_independent_nullable_flow` adds a source-backed/source-less pair for guarded whole-unique transfer and unguarded nullable rejection. `permission_004_referent_ceiling` proves exported `$` field ceilings remain effective after whole transfer. | **Partial**: no general freeze/sealed-object ceiling exists to replay. |

## Concrete implementation gaps

1. `CedeExpr` now classifies member/index/spread paths as Shared, so they do
   not rebuild payload authority. Local initialization rejects nullable-to-
   non-null transfer unless the existing direct-binding `guard` narrowing has
   removed nullability. Its invalidation and move marking are still generic,
   with no canonical projected-path proof or per-field drop state.
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
   match, and guard flow propagation, direct guarded unique transfer,
   unguarded nullable rejection, and an exported `$` field ceiling. It does
   not yet exercise a general freeze/sealed-object ceiling because none is
   currently represented in the language model.

## Recommended implementation order

1. Implement **whole-binding unique `cede` only**. Require `^`/owned source,
   PAL invalidation, non-null proof, and source invalidation; derive the fresh
   binding from its declaration capped by the carried ceiling.
2. Route `~`, `&`, and safe `*` through one Shared derivation and add negative
   tests for local destructuring and receiver promotion.
3. Add canonical-path null guard evidence. Initially reject unsupported guarded
   forms rather than guessing.
4. Leave partial moves rejected until per-field state and drop semantics are
   designed explicitly.

## Release conclusion

`PERM-STATIC-01` may remain a verified static guarantee. `OWN-FLOW-01` and
`OWN-FLOW-02` are proposed rules with partial prerequisites only. The language
gap should remain **in progress** until the listed negative tests and source/
`.tki` evidence pass.
