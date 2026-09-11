# B6: borrow domains and shared aggregate responsibility

Base: `c03adb571cc62c0966e0f8821d9c4d54d3cc6992`.
Status: **Accepted**, limited to borrow-interface domains and shared aggregate
transfer responsibility.
Accepted revision: `c81ecdd6bdb1b8f91a05d16e5f6e2a661885db0a`.
Local implementation freeze: `freeze/borrow-domains-shared-c81ecdd6`.
This does not accept JSON, recursive-container evidence, JsonFactory, the whole
binding work, the complete cede RFC, or RC13 release qualification.

The user's independent acceptance review reports ten selected related CTest
targets **10/10**, **179.78 seconds**, plus wrapper-copy, multi-field/nested
aggregate and partial-field-move controls. The source receipt did not replace
destination permission validation. The original extended result remains
**10/11**, not a fully green suite; the baseline condvar failure is neither a
new blocker for these scopes nor a passing test.

These two implementation scopes are closed. Subsequent work must be separately
scoped; no further shared/iterator changes are included in this acceptance
record. This documentation update does not rerun tests or alter implementation.

## 1. Interface domains

Only EntryRef's accessor impl and HashMapIterator @BorrowIterator gain K/V
`borrow_extendable`. HashMap and @Iterable remain unconstrained. The raw Entry
accessors and @Iterator keep their existing `raw_extendable` domains.

All three original failed positives now pass:
`raw_next_pending.tk`, `shared_aggregate.tk`, `shared_transfer.tk`.
The raw pointer-chain driver additionally executes next() for raw K and raw V,
using live C-owned input storage. Managed next_ref controls cover unique/shared
in both cursor positions; the original handles.tk remains an unchanged
successful container/exact-once test.

## 2. Source check -> disposition -> evaluation -> destination cleanup

- Sema normalizes only transparent unsafe/ascription wrappers before checking
  cede's exact source, PAL conflicts and invalidation. It records a successful
  source check on the CedeExpr. This receipt is reset on each check and is not
  cloned, serialized, or used as complete destination/transaction authority.
- Shared aggregate disposition consumes that source check: a validated explicit
  cede selects MoveOwned without looking at a later Moved bit. Shared copying
  selects RetainShared. Existing destination, permission, dependency and
  transaction validation still apply before artifact generation.
- Enum, struct and anonymous-record insertion use one aggregate evaluator.
  For a Sema-qualified RetainShared, an RAII scope identifies only its exact
  variable/selector. The original expression and conversion evaluation still
  run; only the leaf's acquire is deferred to the aggregate boundary, where it
  happens exactly once. Ordinary RValues elsewhere are unchanged. Scope state
  is restored on all exits and cannot suppress unrelated expressions.
- A shared cede already emits its value and retires the source. Aggregate
  reception does not retain or repeat that source cleanup. Non-shared legacy
  aggregate handling is retained.
- Masked record cleanup previously skipped fields whose type text started
  with `~`. It now dispatches an actually resolved shared field through the
  existing typed cleanup, guarded by the live mask. Raw/reference exclusions,
  unique handling, custom-drop policy and reference-count algorithms are not
  changed. This closes the anonymous-record leak found by the required matrix.

No blanket removal of RValue acquire, no post-state ownership guessing, no
new Copy/Dup grant, and no ABI or interface-key change. The old proposed
CodeGen deduplication patch remains historical; it was not used as a standalone
fix. Recursive-container evidence, JsonFactory, raw_take and thread work remain
unmodified and outside this candidate.

## Verification matrix

`toka_shared_aggregate_handoff` runs **36** generated cases:

| Axis | Values |
| --- | --- |
| destination | enum / struct / anonymous record |
| source | ordinary local / morphic local / ordinary parameter / morphic parameter |
| operation | copy / direct cede / composed unsafe + ascription wrapped cede |

Each case requires normal/shadow diagnostic parity, actual runtime success,
surviving owner/source checks, exact-once destruction and LLVM retain counts.
The selected aggregate route has one retain for a copy and zero for transfer;
the local-case retained survivor contributes one separate, explicitly counted
retain outside the aggregate. Four negatives verify active-borrow, non-cede
parameter, use-after-move and writable-permission rejection. Rejected bindings
preserve their prior source state; object and IR outputs must be absent.

`toka_binding_b6_iterator_domains` has **7 runtime/parity positives, 12 domain
rejections, and 4 valid borrowed-domain controls**. None of the three former
failed positives are skipped or counted as negative tests. The raw and borrowed
rejections require their actual E0621 domain (or E0492 illegal type), not an
unrelated parser failure. A separate minimal record_lifecycle runtime probe
also succeeds after the live-mask cleanup correction.

The compiler/tools build is Debug, not a fresh Release qualification. No full
PASS/FAIL suite is claimed. The complete call-shadow target still encounters
the existing `g09_sync_condvar.tk:39` / `EnvironmentLifetimeUnproven` blocker,
already recorded in binding_b1_candidate.md. It is not counted as a pass and
is not moved back into the frozen thread scope.

Only local commits with `[skip ci]`; no push, PR, Actions or frozen-ref moves.

Final candidate verification: `toka-tools` and `toka_explicit_cede_plan` builds
passed; the minimal record runtime exits 0; targeted CTest **10/11**, **194.73
seconds**. Both new targets passed (`toka_shared_aggregate_handoff`, 121.86s;
`toka_binding_b6_iterator_domains`, 60.37s). Binding transfer, shared-parameter
ABI, enum cleanup, CodeGen authority, non-call shadow, B6 managed, dyn-fn
lifecycle and return-source also passed. The sole failure is the unchanged
call-shadow condvar blocker described above. `git diff --check` passes.
