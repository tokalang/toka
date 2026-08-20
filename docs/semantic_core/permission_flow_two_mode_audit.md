# Audit: Recorded Implementation Evidence Against Two-Mode Permission Flow

**Audit target:** revision `7ccb649e7678494ac0f0fc3a8ae6b240a8dc6600`
after direct-source PAL closure for nested patterns, destructuring, and
reference iteration.

**Audit date:** 2026-07-27

**Authority note:** This is dated implementation evidence, not the current
normative status source. The bounded capability matrix is owned by
`permission_flow_two_mode_rfc.md`; current-revision qualification is owned by
`semantic_contract_evolution_roadmap_rfc.md`. References below to a Proposed
RFC or an in-progress language gap describe the audit date and are retained as
history.

**Recorded result:** at that revision, the static authority layer was
implemented and evidenced. A first
direct-source Shared-flow slice is implemented for local initialization,
assignment/call use, returns, field initialization, nested match/guard pattern
binding, readonly-reference destructuring, and fixed-array/protocol reference
iteration. Direct nullable payload guard and fresh-rebind lowering is also
closed across value, reference, unique, shared, and unsafe raw paths. The full
two-mode RFC remains incomplete and must not be represented as a closed 1.0
guarantee.

## Evidence basis

- `python3 tools/run_conformance.py`: 180 passed, 0 failed on 2026-07-27;
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

These source-less runners record declaration replay and execution at the audit
revision. They do not establish Level-B bodyless provider-fulfilment trust for
a separately supplied object.

## Recorded direct-source PAL evidence

- Nested `match` and `guard` reference patterns now retain their exact member
  projection in PAL. A borrow of `pair.left` rejects `cede pair.left` while
  still allowing disjoint `cede pair.right`; the source-backed and source-less
  `nested_pattern_reference_*` cases cover both outcomes.
- Ordinary destructuring has the same exact-field registration. The
  `destructure_reference_*` cases distinguish a conflicting ceded field from
  a disjoint one.
- A reference `for` loop over a fixed array retains the collection borrow for
  its lifetime and CodeGen binds each iteration variable to the real element,
  not a temporary payload slot. `for_reference_fixed_array_*` and
  `g08_iterator_pal_protocol.tk` cover fixed-array and protocol iterator
  lowering respectively.
- Enum `match` and `guard` payload reference binders retain their enclosing
  enum target as a conservative PAL path. They therefore block invalidating
  `cede` of that target while the binder is live; source-backed and
  source-less `enum*_payload_reference_cede_conflict` cases cover both entry
  points.
- These are direct-source facts only. They do not add provenance traversal or
  alter the still-open independent-versus-Shared `cede` semantics.

## Findings

| RFC rule | Recorded evidence | Assessment |
|---|---|---|
| Audit status | A `Partial` assessment below identifies a proposed RFC generalization or an explicitly excluded 1.x form. It does not reopen the verified frozen 1.0 surface recorded in `docs/1_0_gap_ledger.md`. | **Scope note** |
| Iron rule: a shared view never amplifies Payload authority | `PermissionFlow` derives a one-hop RHS fact. Fresh local Shared bindings and existing variable/field handle rebindings retain a payload ceiling, and declaration boundaries enforce it for payload assignment, mutable calls, returns, fields, match/guard binders, and destructuring reference fields. `cede_shared_existing_lhs_cannot_amplify_payload` proves an explicit `~target = cede ~source` is a handle rebind, not an implicit payload assignment, and its later payload write is rejected. `cede_shared_field_rebind_cannot_amplify_payload` proves the same field path is lowered as an envelope rebind; its branch variant proves the ceiling survives a control-flow join, while the reset test proves a later independent rebind removes only that exact ceiling. `reference_field_rebind_cannot_amplify_payload` covers the formerly early-returning `&#field = &source` path. Toka 1.0 has no independent indexed-element handle-rebind surface; `index_handle_rebind_is_not_a_1_0_surface` makes that boundary explicit. `permission_002_shared_flow` repeats shared variable and field cases source-less alongside readonly and writable imported reference fields, match binders, and guard binders. | **Verified for frozen 1.0 surface**: an object-level freeze/sealed ceiling has no 1.0 syntax carrier and is a 1.x proposal, not an open 1.0 rule. |
| Static authority is declaration/signature-backed; syntax is intent only | `BindingPermission`, `AccessCapability`, and `AccessIntent` route ordinary assignment and calls through declaration facts. Static methods (including lazy generic instantiation), generic functions, extern declarations, `@Callable` invocation, and instance methods share the same consuming-parameter guard and explicit-transfer obligation as ordinary calls. Explicit `cede` captures create a fresh binding while preserving the source declaration's capability and direct-flow ceiling; the capture-list sigils themselves grant nothing. The conformance suite includes direct, static, generic-static, generic, callable, extern, receiver, raw, and explicit-capture cases. | **Conforms (Layer 1)** |
| Existing assignment cannot redeclare H/P | Existing targets are checked against their declared access capability. A later assignment does not update the target declaration. `cede_existing_handle_only_lhs_cannot_gain_payload_write` proves that an existing `^#` destination can receive a ceded unique value but remains payload-readonly; `cede_shared_existing_lhs_cannot_amplify_payload` and `cede_shared_field_rebind_cannot_amplify_payload` prove writable shared variable and field targets instead inherit their direct shared source's lower payload ceiling. Indexed elements have payload and partial-`cede` operations, but no independent handle-rebind operation in the 1.0 grammar. | **Conforms (Layer 1)** |
| `cede` requires invalidation and marks a source moved | A whole `^` binding is PAL-invalidated and reports `E0438` after transfer; cede parameters have dedicated call checks. `cede_unique_parameter_borrow_conflict` and `direct_unique_move_borrow_conflict` prove both whole-unique spellings reject with `E0440` while their exact source has an active borrow, and `permission_003_independent_flow` repeats both results from a source-less `.tki`. `cede_direct_field_borrow_conflict`, `cede_fixed_array_index_borrow_conflict`, and `permission_005_partial_cede_lifecycle` establish the same exact-path rejection for both supported partial-cede projections. `cede_fixed_array_disjoint_borrow` proves that distinct constant fixed-array elements remain independently transferable. A rejected transfer leaves its path initialized. For a direct named field of a local compiler-managed record or a constant index of local fixed array with at most 64 elements, `InitMask` marks only that projection uninitialized and a CodeGen drop mask prevents double drop. Reassignment restores that exact cleanup obligation. Dynamic resource array indexes are rejected with `E04600`; container-internal indexes retain their own invariants. Coroutine cancellation unwinds the same mask before completing the task. Member/index/spread paths remain Shared-flow for authority. | **Partial**: custom-drop, dynamic/container index, spread, enum, and broader source-less lifecycle evidence remain open. |
| Independent sources re-root a fresh binding under referent ceilings | A fresh unique binding obtains its own declaration permissions and either visible whole-transfer spelling invalidates the source: `cede ^source` (`cede_unique_creates_independent_owner`, `cede_unique_readonly_source_creates_writable_owner`, and `cede_unique_source_invalidated`) or direct hatted move `^source` (`direct_unique_move_creates_independent_owner` and `direct_unique_move_invalidates_source`). The readonly-source case demonstrates that a fresh whole-unique owner derives P from its own declaration, not from the old owner's local P marker. A `cede ^p#` parameter and a local unique `cede self#` receiver are also fresh unique roots: their sync/async conformance cases execute the caller-to-callee ABI, mutate through the fresh root, and produce the required async `TaskHandle`. For the by-value consuming receiver ABI, `codegen_cede_unique_receiver_heap_release_01` verifies the caller releases the old receiver heap slot after the callee/factory captures the payload; `codegen_cede_temporary_receiver_heap_release_01` proves the equivalent fresh-`new` release; and cold/started async tests prove frame capture precedes source-slot release. The same lifecycle proof now covers a bounded local direct field: sync/async field-receiver tests verify exact-once destruction and source-slot release; custom-drop and nonlocal/indexed receiver projections are rejected. Both retain a declared `$` field ceiling; `permission_003_independent_flow` replays the local `cede ^source` case plus sync/async parameter paths source-less, and `permission_004_referent_ceiling` replays the direct-move case source-less. `cede` member/index/spread paths retain their direct payload ceiling rather than becoming independent. | **Verified for frozen 1.0 surface**: no general freeze/sealed-object carrier is part of that surface. |
| Shared sources cannot gain payload authority | `~`/`&` classification uses the direct RHS capability. Member capability is the intersection of its parent path and its field declaration; member/index flow is classified from that direct projection rather than from its aggregate/container. An explicit unary handle target (`^target`, `~target`, or `&target`) and a morphic field target (`holder.~field` or `holder.&field`) are classified as rebinds before smart-pointer payload decay, so a shared or reference rebind updates only that exact direct-flow path rather than writing through its target. Indexed elements have no independently rebindable handle surface in 1.0; their supported payload and partial-`cede` operations remain direct projection flows. Thus a writable record cannot pass a readonly handle field to a payload-writable ordinary, generic, `@Callable`, or `cede` parameter, mutable receiver, closure capture, return signature, aggregate field, or an existing shared variable/field target. A by-value `cede self` receiver is a separate ownership boundary and rejects shared, borrowed, and raw views (`E04602`); ordinary `self#` methods remain available where the declared Payload capability permits them. Negative tests cover a two-hop Shared chain, `cede ~`, existing variable and field rebinding (including a branch join), a reference-field rebind, ordinary/generic/callable/cede mutable arguments, explicit closure capture, returns and field initialization with/without `cede`, readonly field/index fresh bindings, match/guard bindings, a readonly-reference destructuring field, non-owned consuming receivers, and task-start of an async `cede` parameter. The independent-reset run test shows a later fresh field rebind restores only its declared Payload authority. Positive run tests show declared writable reference fields, ordinary writable shared receivers, and sync/async writable shared `cede` parameters preserve—not invent—Payload permission. `permission_002_shared_flow` repeats variable/field rebind, reset, and readonly/writable destructuring, match, guard, return, ordinary/generic/callable/cede readonly-field call, explicit closure capture, non-owned consuming receiver rejection, and field/index fresh-binding outcomes source-less. Safe raw payload access remains gated by `unsafe`. | **Partial**: independent-transfer and nullable semantics remain open. |
| Safe nullable payload and owning-handle surface | `T?`, `none`, `nul ^T`, `nul ~T`, and the nullable assertion operator are permanently rejected by E0484-E0487. Their Sema/PAL/CodeGen lowering is removed. `IdentityMayBeZero` and `Type::IsNullable` now describe only raw `nul *T`; raw pointer/FFI null remains outside PAL. | **Closed by removal**: conformance locks every removed spelling and the surviving raw may-zero surface. |
| Fields/freeze ceilings survive independent flow | A field's declared `$` ceiling remains attached to its shape after whole-unique transfer: `cede_unique_preserves_blocked_field` rejects the write with `E0443`, and `permission_004_referent_ceiling` repeats that outcome source-less from the exported field declaration. | **Partial**: this proves declared field ceilings; there is no separate general freeze/sealed-object ceiling or all-boundary propagation model. |
| Patterns and destructuring use direct flow derivation | Match and guard callers derive `PermissionFlow` from their target and pass the direct capability to `checkPattern`. Destructuring computes the initializer flow once, records each binder's own `BindingPermission`, and applies each handle field's declared P as its one-hop ceiling. Imported readonly/writable reference-field destructuring, match, and guard paths have source-backed/source-less replay. The match runtime test also covers a local type alias so payload type metadata survives alias lowering. | **Partial**: independent-transfer and nullable semantics remain open. |
| Partial moves do not establish independent authority | `cede` classifies member/index/spread paths as Shared, and a writable destination cannot amplify a readonly member (`cede_member_cannot_rebuild_payload_permission`). Direct named fields of eligible local records and constant indexes of local fixed arrays additionally have exact static liveness and runtime cleanup (`cede_direct_field_cleanup`, `cede_direct_field_reinitialize`, `cede_fixed_array_index_lifecycle`, and canceled-async cleanup coverage). | **Partial**: custom-drop, dynamic/container index, spread, enum, and broader source-less lifecycle evidence remain open. |
| `.tki` replay preserves flow facts | `permission_002_shared_flow` checks interface emission, source-backed/source-less semantic-evidence equality, writable imported reference-field, match, guard, async shared-`cede`, and consuming mutable-receiver cases, plus readonly rejection for each path. `permission_003_independent_flow` covers imported sync/async `cede ^p#` parameter roots, sync/async consuming unique mutable receivers (including task start/join), readonly-source permission rebuilding, and borrow-conflict rejection. `permission_004_referent_ceiling` proves exported `$` field ceilings remain effective after both `cede` and direct hatted unique moves. `permission_005_partial_cede_lifecycle` proves imported direct-field and fixed-array constant-index liveness across its bounded cases, including a consuming direct-field receiver, its post-transfer read rejection, and nonlocal receiver-projection rejection. | **Partial**: no general freeze/sealed-object ceiling exists to replay. |

## Concrete implementation gaps

1. Existing-LHS transfer has no recorded current-revision proof that Sema and
   CodeGen reject canonical source/destination equality or prefix overlap
   before retiring the old destination. In particular, the historical suite
   does not qualify `^x = cede ^x`, direct hatted self-move, same-field, or
   same-index replacement, nor a source-invalidating Shared-handle self-rebind
   where that syntax is admitted. P-1 must treat those as negative source/TKI
   gates; this audit must not be read as authorizing self-transfer or defining
   it as a no-op.
2. `CedeExpr` now classifies member/index/spread paths as Shared, so they do
   not rebuild payload authority. Local initialization rejects nullable-to-
   non-null transfer unless the existing direct-binding `guard` narrowing has
   removed nullability. Direct named fields of eligible local records and
   constant indexes of eligible local fixed arrays now have a bounded
   projected-path proof and per-projection drop state; other projections
   remain outside that lifecycle guarantee.
3. Local initialization and destructuring in `src/Sema/Sema_Stmt.cpp`, plus
   match/guard binding in `src/Sema/Sema_Expr_Init.cpp`, store a one-hop
   payload ceiling without rewriting the declaration. Destructuring also
   preserves the binder's declared `BindingPermission`, so a writable
   reference field retains its declared Payload capability while a readonly
   field remains capped. `permission_002_shared_flow` replays both outcomes
   against a source-less imported shape interface.
4. `isTypeCompatible` in `src/Sema/Sema_Type.cpp` is morphology/type
   compatibility. It cannot serve as the authority-flow decision because it is
   intentionally bidirectional in several non-permission cases and has no
   path, move, guard, or PAL inputs.
5. Source-less `.tki` replay covers imported reference-field destructuring,
   match, and guard flow propagation, direct guarded unique transfer,
   unguarded nullable rejection, and an exported `$` field ceiling. It does
   not yet exercise a general freeze/sealed-object ceiling because none is
   currently represented in the language model.

6. Direct-source PAL coverage now includes nested struct and enum
   match/guard patterns, ordinary destructuring, and fixed-array/protocol
   reference iteration. Enum payloads use their enclosing enum target as a
   conservative direct source because they lack a separately nameable source
   projection; no provenance traversal is introduced by this closure.

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

## Historical release conclusion

At the audit date, `PERM-STATIC-01` could remain a verified static guarantee,
while `OWN-FLOW-01` and `OWN-FLOW-02` were proposed rules with partial
prerequisites. That historical conclusion does not override the later bounded
matrix or certify a newer compiler revision.
