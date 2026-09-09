# Native sync factory source plans — WIP, not Accepted

Base: `aaac3267`. This checkpoint connects the three private creation calls to
real Sema-produced, exact-edge CodeGen plans. It does **not** complete the native
owner/storage witness, the thread environment proof, or the binding slice.

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

Directed source tests: 5 positives (three factories, valid generic cache reuse,
same-name untrusted function), 24 E0701/no-artifact fault checks, and one invalid
generic-parent rejection. Existing adapters still pass all 76 runtime/failure
cases; shared reception retains its 23-case/IR gate.

Final directed CTest run: **5/6**, 43.12 seconds. The only failing target is the
mandatory source-flow test below. Incremental compiler/test builds and
`git diff --check` pass. No full suite or non-testing build was rerun.

## Open mandatory source-flow test and rejected patch

`toka_native_sync_factory_flow` is a real ModuleResolver/Sema test against the
actual SDK source. It intentionally remains a **failing required test**, not an
xfail or an updated oracle. It checks exact move identity, distinct factory
owners and branch-local storage mutation without executing corrupted storage.

Current failure: the factory result is `Mutex_M_i32`, while the checked mutable
binding view is `Mutex_M_i32#`. The strict full-type equality in
`collectNativeSyncFactoryOrigin` loses the relation immediately. Therefore the
subsequent move/branch assertions are not yet demonstrated; they are not claimed
as passing merely because the state-map code exists.

The proposed top-level view comparison was rejected by automatic review and is
**not applied**. The exact diff is saved in
[native_sync_view_origin_unapplied.patch](native_sync_view_origin_unapplied.patch).
`git apply --check` succeeds; that check did not modify source. Review flagged
the possibility of permitting writable/owner mismatches. The proposal clones a
comparison type (`ShapeType::withAttributes`), not the AST's actual type, but it
still changes provenance admission and requires confirmation of that boundary.
No alternate edit/tool was used to bypass the rejection.

An earlier proposal to simply remove the speculative-context guard was also
rejected and not applied. The current safer deferred/unvalidated carrier path
preserves the publication barrier and passed the source/fault gates above.

## Remaining chain (not hidden by factory success)

1. Close and test writable-view provenance, move/shared-copy continuity,
   conservative joins, unknown mutation invalidation and rejection rollback.
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
