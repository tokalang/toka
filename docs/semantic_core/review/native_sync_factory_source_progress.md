# Native sync factory source plans — WIP, not Accepted

Base: `aaac3267`. This checkpoint connects the three private creation calls to
real Sema-produced, exact-edge CodeGen plans. It does **not** complete the native
owner/storage witness, the thread environment proof, or the binding slice.

2026-09-10 update (implementation authorized against `b94c471b`): the direct
owner writable-view provenance gap is fixed with the bounded comparison below.
This update is not acceptance of the complete witness chain.

## Implemented and tested

- Resolver-trusted source `std/sync` declaration selection; exact selected
  declaration, nominal owner template, instantiated element morphology, source
  argument edge, and enclosing definition are retained in an immutable plan.
- Only a successfully checked factory specialization can seal a plan. Deferred
  generic-body plans remain unvalidated and cannot enter binding provenance;
  finalization checks both exact specializations before replacing that private
  carrier. It does not re-run Sema or reconstruct ownership from post-state.
- CodeGen validates every attached/tagged factory plan before emitting the
  ordinary reviewed adapter call. Missing, incomplete and mismatching plans are
  E0701/no-artifact failures. Only existing 64-bit POSIX targets and malloc-
  compatible element alignment are admitted by this adapter layout gate.
- The existing production adapter bodies are unchanged. Their runtime return
  remains after native initialization and full element initialization. Failures
  still clean partial state before fatal, without returning an owner.
- ClosedPayload remains a separate replacement prerequisite. Its result does
  not create a native witness and does not authorize a thread capture.

Directed source tests: 6 positives (three factories, valid generic cache reuse,
same-name untrusted function, readonly read), 24 E0701/no-artifact fault checks,
one invalid generic-parent rejection, and 2 readonly-owner write-contract
rejections with strict normal/shadow parity and no artifacts. Existing adapters
retain all 76 runtime/failure cases; shared reception retains its 23-case/IR gate.

Historical `b94c471b` directed CTest result: **5/6**, 43.12 seconds, with the
source-flow target failing. It was kept required, without xfail or oracle change.

Current directed CTest result: **6/6**, 67.50 seconds (ClosedPayload, source
flow, sync storage subset, shared reception, adapters, factory source/fault
matrix). Incremental compiler/test builds and `git diff --check` pass. No full
PASS/FAIL suite or non-testing build was rerun in this update.

## Resolved direct-owner view gap

`toka_native_sync_factory_flow` is a real ModuleResolver/Sema test against the
actual SDK source. Its original move, distinct-owner and branch invalidation
assertions now pass, without executing corrupted storage. Additional assertions
prove that a move to a readonly binding preserves the very same source edge,
and a rejected consuming call restores the original relation without a leaked
Moved/Uninitialized diagnostic.

The factory result is `Mutex_M_i32`, while a checked mutable binding view is
`Mutex_M_i32#`. `matchesOwnerView` compares a temporary clone using the current
view's top-level IsWritable only. It requires a validated direct ShapeType,
the exact owner template, and the complete matching instantiated element type.
Nominal identity, nullable, blocked and all element morphology remain checked.
Tests change candidate view types only and assert the real factory, expression,
declaration and element type objects/identities remain unchanged. Raw/reference,
unique/shared outer views, unresolved declarations, same-name other nominals,
nullable/blocked differences and changed element permissions are rejected.

The previously rejected comparison received explicit full-diff implementation
authorization on 2026-09-10 and is now applied, factored into the read-only plan
query for direct testing. The original historical diff remains archived in
[native_sync_view_origin_unapplied.patch](native_sync_view_origin_unapplied.patch).
Its filename describes its earlier status, not the current implementation.
No permission/PAL/flow ceiling or CodeGen factory comparison was relaxed.
The source permission negatives request a mutable owner formal from a readonly
view and require E04571. They do not misclassify `handle#` field-local mutability
as inherited owner permission or change that existing rule.

An earlier proposal to simply remove the speculative-context guard was also
rejected and not applied. The current safer deferred/unvalidated carrier path
preserves the publication barrier and passed the source/fault gates above.

## Remaining chain (not hidden by factory success)

1. Extend the now-tested direct-owner flow to shared-copy continuity and the
   remaining alias/unknown mutation paths. This patch does not claim those
   additional paths merely because the direct-owner relation is preserved.
2. Match guard acquisitions/discharge and exact slot/replacement plans to the
   same owner. Factory creation alone does not prove terminal cleanup or guard
   lifetime continuity.
3. Connect the complete relation to both callable-environment and public-thread
   qualification. Neither has received a new nominal whitelist or permission.
4. Complete missing/mismatch and source-hidden rejection matrices for those
   operation boundaries, then switch public wrappers and run the end-to-end
   responsibility matrix. `sync_thread_pending.tk` remains open and mandatory.

No public wrapper/caller migration, capture/refcount/A-B protocol change,
TKI/interface/ABI change, full PASS/FAIL run, push or PR occurred in this work.
