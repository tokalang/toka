# Managed-element alignment: bounded partial implementation

Base: `a38773eae5d626fdc070fa896562a1900037a383`.
Branch: `impl/binding-b6-managed-elements`.
Status: **not Accepted; the subsequently authorized domain/shared scopes are
implemented**. Current candidate and complete matrix:
`shared_aggregate_closeout.md`. The checkpoint history below preserves prior
failures and authorization boundaries; its pending items are superseded by
that candidate, not silently counted as earlier passes.

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

## Authorized raw iterator declaration migration

The original handles.tk errors included E0492 in Entry's raw accessors
and HashMapIterator @Iterator, which instantiated `*^Token` / `*~Token`.
The authorized fix gives **only those raw APIs** the existing `raw_extendable`
domain, as Vec::unsafe_get already does. HashMap itself, ordinary operations,
the iterable view and @BorrowIterator must not receive that restriction.

After the user's explicit public-API authorization, it has been applied. The
only syntax correction is removing commas between where constraints, which
the existing parser separates by lines. No parser change. The archived
`managed_iterator_domains.proposed.patch` is the original proposal, not the
current source diff. No raw/managed relaxation was used.

Running handles.tk then exposed an extra shared retain in
`Option::Some(cede 'old_v)`. `qualifyAggregateTransfer` now preserves MoveOwned
when the explicit cede source is already marked Moved. This classification
change reuses the established transfer and does not add another invalidation,
change Copy/Dup or alter reference-count operations. The original handles.tk
now builds and exits 0 (102 expected drops, shared survivor intact).

The initial broader call-based inference proposal was also rejected and is
not present. The narrower descriptor synchronization described above was
applied only after tracing the existing type synthesis and final planner.

## Validation scope

`toka_binding_b6_managed` checks exact actual/formal handle identity, NonCopy
and cleanup disposition; unique transfer IR uses the handle slot; runtime
checks exact-once and a surviving shared owner. Negative controls cover bare
unique copying, use-after-move, active borrow and writable permission elevation,
with normal/shadow parity and no object/IR artifact.

The original handles.tk is unchanged and is now a successful runtime test.
The separate JSON design-only note is `binding_b6_json_boundaries.md`; it
distinguishes valid concrete JsonNode construction from invalid generic
memset-based initialization. No JSON parser, raw_take, Copy, clone, thread,
runtime, interface key or ABI changes are made here.

Final Debug tokac build succeeded. Targeted CTest **7/7**, **119.46 seconds**:
B6 managed, Stage-1 binding transfer, B5 clone, B5 scalar, standalone cede,
Stage-0 CodeGen authority and B4 enum Copy. The B6 negative controls also prove
bare-copy/borrow rejection leaves the original source live; no E0438/E0410
cascade is accepted for those controls. No full PASS/FAIL suite was run.

At the c17ece9d checkpoint handles.tk still failed with E0492; the newly
authorized migration closes that failure. Prior freeze refs and RC13 remain
unchanged. Local checkpoints only, with no push, PR, Actions or new Accepted
marker.

## Current domain tests and remaining failures

`toka_binding_b6_iterator_domains` passes four runtime/parity cases: original
handles; managed borrowed iterators with unique/shared in both key/value cursor
positions; ordinary/raw Entry accessors using live pointer chains; the original
2000-element HashMap resize/ordinary-next program. It also checks eight managed
raw-domain refusals (K/V, unique/shared, next/accessor), object/IR absence, and
four matching next_ref controls. Invalid raw next requests report E0621 with
the correct K/V bound; illegal raw accessor types retain E0492.

This does **not** close all requested raw-type next cases or all shared-copy
controls. Three failed positives are retained, not converted into negative
qualification cases:

- `raw_next_pending.tk`: merely checking a HashMapIterator<*i32,i32> function
  using next() also instantiates the unbounded EntryRef accessor body and fails
  with E0492 for &*i32. EntryRef/@BorrowIterator were not modified. A possible
  follow-up is **borrow_extendable** for their own borrow API domain (never
  raw_extendable), keeping managed borrowing and HashMap/Iterable unaffected;
  this public-domain adjustment needs its own scope decision.
- `shared_aggregate.tk`: a bare morphic shared variable is acquired by normal
  RValue generation and again by the legacy aggregate RetainShared handler.
  It exits 4 instead of 0. The proposed CodeGen deduplication was rejected by
  automatic review as beyond the current explicit scope; it is not applied.
  `b6_shared_retain.proposed.patch` records that unapplied increment, not an
  accepted or fully validated solution.
- `shared_transfer.tk`: unwrapped aggregate cede works, but its unsafe-wrapped
  counterpart still receives an extra retain (exit 5). The SymbolInfo Moved
  check is insufficient for this route; the authoritative transfer and the
  actual evaluation/retain path must be aligned before claiming full closure.

No new clone, recursive-container witness, JsonFactory, raw_take admission or
thread work has been implemented. These failing controls are not counted as
successful or skipped cases in the partial domain gate.

Final verification of this increment: Debug tokac build succeeded; targeted
CTest **7/7**, **141.00 seconds** (iterator domains, B6 managed, B5 clone,
enum payload cleanup, shared-parameter ABI, binding transfer, standalone).
`diff --check` passes. The runtime failures above remain outside this passing
partial gate and still prevent whole-matrix acceptance. No full PASS/FAIL run.

Next scope decisions, not implementations:

1. Give EntryRef's accessors and HashMapIterator @BorrowIterator their own
   K/V **borrow_extendable** domains. Do not add raw_extendable to either and
   do not constrain HashMap or @Iterable. This must make raw next() usable
   without instantiating invalid &raw accessors, while keeping managed next_ref.
2. Align shared aggregate disposition with the validated transfer and exactly
   one acquire for copying. A bare copied shared RValue and a morphic parameter
   may use different existing evaluation paths; an explicit/wrapped cede must
   transfer without retaining. This requires the complete Sema/CodeGen wiring,
   not treating the unapplied deduplication patch alone as an accepted fix.
   Preserve all source/PAL/permission checks and reference-count algorithms.
