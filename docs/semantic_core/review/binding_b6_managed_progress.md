# Managed-element alignment: bounded partial implementation

Base: `a38773eae5d626fdc070fa896562a1900037a383`.
Branch: `impl/binding-b6-managed-elements`.
Status: **not Accepted; handles.tk not yet closed**.

## Reproduced facts and applied changes

1. A minimal ordinary factory returning `^Cell`, bound as `auto 'value`,
   produced actual_type=^Cell but formal_type=Cell. In Sema_Stmt, `morph` already
   retained ^, but the physical-type builder still used a DirectValue binding
   permission. The fix synchronizes only the inferred morphic unique/shared
   descriptor with that already-selected type. The final binding planner,
   source/PAL/permission/dependency checks and diagnostic rollback are intact.
   No call/type equality is used to declare a transfer admitted.
2. Once admitted, `cede 'value` on a unique local exposed an incorrect second
   load: the backend peeled the payload address and then loaded a pointer from
   the resource bytes. The fix uses existing emitHandleAddr for an exact-typed
   morphic variable, retaining existing source drop suppression. Transparent
   unsafe/ascription wrappers preserve the same exact variable only when its
   full resolved handle type matches. Shared retain/release and ABI are not
   changed. Runtime controls cover both wrapped and unwrapped transfers.
3. HashMap try_borrow and borrowed iterator now spell `&'(slot)`, matching the
   existing Vec morphic-borrow path, not payload `&slot`. This preserves the
   full managed value view without permitting mixed raw/managed pointers.

## Blocked raw iterator declaration migration

The remaining original handles.tk errors include E0492 in Entry's raw accessors
and HashMapIterator @Iterator, which still instantiate `*^Token` / `*~Token`.
The proposed fix gives **only those raw APIs** the existing `raw_extendable`
domain, as Vec::unsafe_get already does. HashMap itself, ordinary operations,
the iterable view and @BorrowIterator must not receive that restriction.

The automatic reviewer rejected that library-domain patch as requiring a more
explicit public-API authorization. It has **not** been applied or tested as an
implementation. `managed_iterator_domains.proposed.patch` contains the exact
unapplied difference; a read-only `git apply --check --unidiff-zero` succeeds.
No alternate tool, conditional bypass or raw/managed relaxation was used.

The initial broader call-based inference proposal was also rejected and is
not present. The narrower descriptor synchronization described above was
applied only after tracing the existing type synthesis and final planner.

## Validation scope

`toka_binding_b6_managed` checks exact actual/formal handle identity, NonCopy
and cleanup disposition; unique transfer IR uses the handle slot; runtime
checks exact-once and a surviving shared owner. Negative controls cover bare
unique copying, use-after-move, active borrow and writable permission elevation,
with normal/shadow parity and no object/IR artifact.

This is not a successful handles.tk test. The failing original remains intact.
The separate JSON design-only note is `binding_b6_json_boundaries.md`; it
distinguishes valid concrete JsonNode construction from invalid generic
memset-based initialization. No JSON parser, raw_take, Copy, clone, thread,
runtime, interface key or ABI changes are made here.

Final Debug tokac build succeeded. Targeted CTest **7/7**, **119.46 seconds**:
B6 managed, Stage-1 binding transfer, B5 clone, B5 scalar, standalone cede,
Stage-0 CodeGen authority and B4 enum Copy. The B6 negative controls also prove
bare-copy/borrow rejection leaves the original source live; no E0438/E0410
cascade is accepted for those controls. No full PASS/FAIL suite was run.

The original handles.tk still fails and does not produce an object. Its former
TypeIncompatible diagnoses are gone; E0492 remains in the unapplied raw API
domain migration. Prior freeze refs and RC13 remain unchanged. Local checkpoint
only, with no push, PR, Actions or new Accepted marker.
