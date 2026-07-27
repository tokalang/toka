# Audit: Current Implementation Against Two-Mode Permission Flow

**Audit target:** the working implementation after writable-reference
destructuring closure.

**Audit date:** 2026-07-27

**Result:** the static authority layer is implemented and evidenced. A first
direct-source Shared-flow slice is implemented for local initialization,
assignment/call use, returns, field initialization, match/guard pattern
binding, and readonly-reference destructuring. Direct nullable payload guard
and fresh-rebind lowering is also closed across value, reference, unique,
shared, and unsafe raw paths. The full two-mode RFC remains incomplete and
must not be represented as a closed 1.0 guarantee.

## Evidence basis

- `python3 tools/run_conformance.py`: 157 passed, 0 failed on 2026-07-27;
- all 25 populated source-less replay cases: 25 passed, 0 failed in bounded
  batches on 2026-07-27;
- `permission_005_partial_cede_lifecycle`: isolated source-backed/source-less
  replay passed on 2026-07-27, including the ceded-field `E0410` rejection;
- static permission logic: `include/toka/Sema.h`,
  `src/Sema/Sema_Expr.cpp`, `src/Sema/Sema_Expr_Binary.cpp`, and call checking;
- transfer logic: `src/Sema/Sema_Expr.cpp` (`CedeExpr`),
  `src/Sema/Sema_Stmt.cpp` (initialization), and
  `src/Sema/Sema_Expr_Call.cpp` (cede parameters);
- consuming-receiver lowering: `src/CodeGen/CodeGen_Decl.cpp` releases the
  original heap slot only after a by-value `cede self` call/factory has copied
  a local unique receiver payload into callee-owned storage;
- type compatibility: `src/Sema/Sema_Type.cpp`;
- pattern binding: `src/Sema/Sema_Expr_Init.cpp`, `src/Sema/Sema_Stmt.cpp`,
  and `src/Sema/Sema_Expr.cpp`.

## Findings

| RFC rule | Current evidence | Assessment |
|---|---|---|
| Iron rule: a shared view never amplifies Payload authority | `PermissionFlow` derives a one-hop RHS fact. Fresh local Shared bindings retain a payload ceiling, and declaration boundaries enforce it for payload assignment, mutable calls, returns, fields, match/guard binders, and destructuring reference fields. `permission_002_shared_flow` verifies source-backed/source-less agreement for readonly and writable imported reference fields, match binders, and guard binders. | **Partial**: general independent-transfer referent ceilings remain open. |
| Static authority is declaration/signature-backed; syntax is intent only | `BindingPermission`, `AccessCapability`, and `AccessIntent` route ordinary assignment and calls through declaration facts. Static methods (including lazy generic instantiation), generic functions, extern declarations, `@Callable` invocation, and instance methods share the same consuming-parameter guard and explicit-transfer obligation as ordinary calls. Explicit `cede` captures create a fresh binding while preserving the source declaration's capability and direct-flow ceiling; the capture-list sigils themselves grant nothing. The conformance suite includes direct, static, generic-static, generic, callable, extern, receiver, raw, and explicit-capture cases. | **Conforms (Layer 1)** |
| Existing assignment cannot redeclare H/P | Existing targets are checked against their declared access capability. A later assignment does not update the target declaration. | **Conforms (Layer 1)** |
| `cede` requires invalidation and marks a source moved | A whole `^` binding is PAL-invalidated and reports `E0438` after transfer; cede parameters have dedicated call checks. `cede_unique_parameter_borrow_conflict` and `direct_unique_move_borrow_conflict` prove both whole-unique spellings reject with `E0440` while their exact source has an active borrow, and `permission_003_independent_nullable_flow` repeats both results from a source-less `.tki`. `cede_direct_field_borrow_conflict`, `cede_fixed_array_index_borrow_conflict`, and `permission_005_partial_cede_lifecycle` establish the same exact-path rejection for both supported partial-cede projections. A rejected transfer leaves its path initialized. For a direct named field of a local compiler-managed record or a constant index of local fixed array with at most 64 elements, `InitMask` marks only that projection unset and a CodeGen drop mask prevents double drop. Reassignment restores that exact cleanup obligation. Dynamic resource array indexes are rejected with `E04600`; container-internal indexes retain their own invariants. Coroutine cancellation unwinds the same mask before completing the task. Member/index/spread paths remain Shared-flow for authority. | **Partial**: custom-drop, dynamic/container index, spread, enum, and broader source-less lifecycle evidence remain open. |
| Independent sources re-root a fresh binding under referent ceilings | A fresh unique binding obtains its own declaration permissions and either visible whole-transfer spelling invalidates the source: `cede ^source` (`cede_unique_creates_independent_owner` and `cede_unique_source_invalidated`) or direct hatted move `^source` (`direct_unique_move_creates_independent_owner` and `direct_unique_move_invalidates_source`). A `cede ^p#` parameter and a local unique `cede self#` receiver are also fresh unique roots: their sync/async conformance cases execute the caller-to-callee ABI, mutate through the fresh root, and produce the required async `TaskHandle`. For the by-value consuming receiver ABI, `codegen_cede_unique_receiver_heap_release_01` verifies the caller releases the old receiver heap slot after the callee/factory captures the payload; `codegen_cede_temporary_receiver_heap_release_01` proves the equivalent fresh-`new` release; and cold/started async tests prove frame capture precedes source-slot release. The same lifecycle proof now covers a bounded local direct field: sync/async field-receiver tests verify exact-once destruction and source-slot release; custom-drop and nonlocal/indexed receiver projections are rejected. Both retain a declared `$` field ceiling; `permission_003_independent_nullable_flow` replays the sync and async parameter paths source-less and `permission_004_referent_ceiling` replays the direct-move case source-less. `cede` member/index/spread paths retain their direct payload ceiling rather than becoming independent. | **Partial**: no universal carried referent ceiling exists for frozen/nullable transfer. |
| Shared sources cannot gain payload authority | `~`/`&` classification uses the direct RHS capability. Member capability is the intersection of its parent path and its field declaration; member/index flow is classified from that direct projection rather than from its aggregate/container. Thus a writable record cannot pass a readonly handle field to a payload-writable ordinary, generic, `@Callable`, or `cede` parameter, mutable receiver, closure capture, return signature, or aggregate field, or rebuild its P through a fresh field/index binding. A by-value `cede self` receiver is a separate ownership boundary and rejects shared, borrowed, and raw views (`E04602`); ordinary `self#` methods remain available where the declared Payload capability permits them. Negative tests cover a two-hop Shared chain, `cede ~`, ordinary/generic/callable/cede mutable arguments, explicit closure capture, returns and field initialization with/without `cede`, readonly field/index fresh bindings, match/guard bindings, a readonly-reference destructuring field, non-owned consuming receivers, and task-start of an async `cede` parameter. Positive run tests show declared writable reference fields, ordinary writable shared receivers, and sync/async writable shared `cede` parameters preserve—not invent—Payload permission. `permission_002_shared_flow` repeats readonly/writable destructuring, match, guard, return, ordinary/generic/callable/cede readonly-field call, explicit closure capture, non-owned consuming receiver rejection, and field/index fresh-binding outcomes source-less. Safe raw payload access remains gated by `unsafe`. | **Partial**: independent-transfer and nullable semantics remain open. |
| Nullable payload guard and nullable-to-non-null `cede` | The generic `{T, present}` guard now branches on `present != 0`, which is exercised by direct `T?` and `&T?` views. Unique and shared handles use their own handle-first paths; unsafe raw handles use the same direct payload rule after the handle check. A fresh `new` or `unsafe alloc` rebind into `T?` establishes `present=true`. Local initialization, `cede` parameter calls, returns, static methods (including lazy generic instantiation), `@Callable` invocation, consuming method receivers, and consuming method arguments reject nullable handles and nullable payloads without a same-path guard (`E04599`); guarded whole-unique transfer is runtime-qualified for local, direct-call, return, static-call, callable, and consuming-receiver paths. A branch-local `if path is null { ... } else { ... }` narrows the exact member or fixed-array index path only in its non-null else branch; a direct local `guard path { ... } else { ... }` now establishes the same proof in its then branch. The null branch is explicitly rejected for non-null `cede`. `cede_nullable_path_guard_resource_cleanup` and `cede_nullable_guard_path_resource_cleanup` prove both guarded projection forms preserve exact resource cleanup. `auto` preserves the nullable payload representation of direct sources, including ceded member and index paths, rather than silently converting `T?` to `T`; fixed-array list and repeat initialization preserve the same element representation. `cede_nullable_payload_after_guard` additionally proves an `@encap` resource is moved from `^Token?` without wrapper shadowing and dropped exactly once. | **Partial**: projection narrowing has only local member and constant fixed-array index coverage; different paths, dynamic indexes, and general projection guards remain unavailable. |
| Fields/freeze ceilings survive independent flow | A field's declared `$` ceiling remains attached to its shape after whole-unique transfer: `cede_unique_preserves_blocked_field` rejects the write with `E0443`, and `permission_004_referent_ceiling` repeats that outcome source-less from the exported field declaration. | **Partial**: this proves declared field ceilings; there is no separate general freeze/sealed-object ceiling or all-boundary propagation model. |
| Patterns and destructuring use direct flow derivation | Match and guard callers derive `PermissionFlow` from their target and pass the direct capability to `checkPattern`. Destructuring computes the initializer flow once, records each binder's own `BindingPermission`, and applies each handle field's declared P as its one-hop ceiling. Imported readonly/writable reference-field destructuring, match, and guard paths have source-backed/source-less replay. The match runtime test also covers a local type alias so payload type metadata survives alias lowering. | **Partial**: independent-transfer and nullable semantics remain open. |
| Partial moves do not establish independent authority | `cede` classifies member/index/spread paths as Shared, and a writable destination cannot amplify a readonly member (`cede_member_cannot_rebuild_payload_permission`). Direct named fields of eligible local records and constant indexes of local fixed arrays additionally have exact static liveness and runtime cleanup (`cede_direct_field_cleanup`, `cede_direct_field_reinitialize`, `cede_fixed_array_index_lifecycle`, and canceled-async cleanup coverage). | **Partial**: custom-drop, dynamic/container index, spread, enum, and broader source-less lifecycle evidence remain open. |
| `.tki` replay preserves flow facts | `permission_002_shared_flow` checks interface emission, source-backed/source-less semantic-evidence equality, writable imported reference-field, match, guard, async shared-`cede`, and consuming mutable-receiver cases, plus readonly rejection for each path. `permission_003_independent_nullable_flow` now covers guarded whole-unique transfer, imported sync/async `cede ^p#` parameter roots, sync/async consuming unique mutable receivers (including task start/join), and unguarded rejection for both nullable handles and nullable payloads, including imported `cede` parameter calls, returns, static methods, and nullable member/index transfer into inferred nullable destinations. `permission_004_referent_ceiling` proves exported `$` field ceilings remain effective after both `cede` and direct hatted unique moves. `permission_005_partial_cede_lifecycle` proves imported direct-field and fixed-array constant-index liveness across its bounded cases, including a consuming direct-field receiver, its post-transfer read rejection, and nonlocal receiver-projection rejection. | **Partial**: no general freeze/sealed-object ceiling exists to replay. |

## Concrete implementation gaps

1. `CedeExpr` now classifies member/index/spread paths as Shared, so they do
   not rebuild payload authority. Local initialization rejects nullable-to-
   non-null transfer unless the existing direct-binding `guard` narrowing has
   removed nullability. Direct named fields of eligible local records and
   constant indexes of eligible local fixed arrays now have a bounded
   projected-path proof and per-projection drop state; other projections
   remain outside that lifecycle guarantee.
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
4. Preserve existing library-invariant partial transfers without treating them
   as a general ownership guarantee. The bounded direct-field model in
   `partial_cede_lifecycle_rfc.md` is now implemented; add its branch, replay,
   and async evidence before widening that surface.

## Release conclusion

`PERM-STATIC-01` may remain a verified static guarantee. `OWN-FLOW-01` and
`OWN-FLOW-02` are proposed rules with partial prerequisites only. The language
gap should remain **in progress** until the listed negative tests and source/
`.tki` evidence pass.
