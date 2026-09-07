# Stage 1: standalone `cede`

Status: Accepted — standalone `cede` behavior slice.

Acceptance date: 2026-09-07.
Accepted scope: this standalone implementation and its callable-value audit
follow-up only; not all of Stage 1 or the complete RFC.

Base: `ba18bc147febcec1628a50937faf7c6873c012fc` (accepted return/source behavior).

Only an expression statement whose root is `cede source` is activated here;
parentheses, an enclosing unsafe expression, and type ascription do not hide
that root. Other enclosing operations remain their own destinations.

The existing prepared non-call plan must admit `StatementEndDiscard` before
the expression is checked. A rejected plan emits E04660 and never evaluates
a nested source expression or invalidates a place. For an admitted plan,
normal Sema validation still runs; a failure restores the pre-statement
`AnalysisState`, and no validated cleanup carrier is published.

Copy changes value production, not the source's invalidation. NonCopy direct
values, unique handles (`cede ^owner`), and shared handles (`cede ~owner`)
transfer the existing cleanup liability to statement end. CodeGen uses its
existing move/drop path. Only a Sema-validated standalone carrier permits
source normalization through an unsafe wrapper for drop suppression; other
destinations' lowering remains unchanged.

`cede callback(args)` for an existing consuming callable stays a call, not a
standalone source-less `cede`. Its ordinary unused result cleanup is unchanged.
Uninstantiated generic bodies defer the gate until the instance has facts;
this does not activate any additional generic/morphic parameter route.

The gate is `tools/scripts/test_stage1_standalone_cede.py`. It checks normal/
shadow parity, rejected-plan NoStateChange, absence of object/IR artifacts,
post-rejection source usability, Copy invalidation, parameter-obligation
discharge, statement-end exact-once destruction, shared-owner retention,
transparent wrappers, and consuming-callable isolation.

## Implementation verification (2026-09-07)

- Independent Debug build, including tools: passed.
- Full CTest: **43/43**, including the new standalone gate and all existing
  call/non-call/generic-body/CodeGen and return/source gates (148.63 seconds).
- Full PASS suite: **451/451**, no exclusions.
- Full FAIL suite: **473/473**, no exclusions.
- `git diff --check`: passed.

The standalone regression set includes nested `cede` rejection without source
invalidation, validation-failure rollback, and normal/shadow diagnostic parity.
No old test oracle or fixture was changed. These are implementation results,
not release qualification. The freeze revision is the commit containing this
acceptance record.

## Callable-value discard audit follow-up

The standalone-only fact provider now preserves the actual callable receiver
mode: consuming `fn`/`dyn fn` values are `OwnedCallable + ProvenNonCopy`, while
ordinary values remain `CallableIdentity + ProvenCopy`. Both invalidate the
named binding on explicit discard. A `dyn fn` carrier also transfers its
environment-owner cleanup liability to statement end; a thin `fn` does not.
The pure planner, return/source provider and receiver syntax are unchanged.

`callable_values.tk` verifies ordinary shared-owner retention, consuming
discard without invoking the function body, an unsafe wrapper, and exact-once
capture destruction. Four ordinary/consuming `fn`/`dyn fn` negative cases
verify that a second discard is `SourceNotLive + NoStateChange` and emits no
object. The gate checks the value-production, binding-invalidation, liability
and obligation rows as well as normal/shadow parity. The existing consuming
invocation regression remains separate and unchanged.

The expanded standalone gate passed in 19.17 seconds before the final full
regression run. The original audit `callable_values.tk` also compiled and ran
successfully (exit 0). The one final run passed the complete incremental tool
build, CTest **43/43** (91.78 seconds), PASS **451/451** (225.05 seconds), and
FAIL **473/473**, without exclusions. Independent audit accepted the fix,
original reproduction, expanded standalone gate and complete incremental
build; it did not repeat the full PASS/FAIL suites.

The broad authority flag on a Stage 1 program remains outside
this follow-up: the standard library includes conservative rejected plans for
other non-call destinations. The established Stage 0 authority gate retains
its historical-mode qualification; no such rejected plan was relaxed here.

Excluded: assignment/initialization, aggregate, match binding, closure capture,
generic/morphic or identity parameter expansion, receiver spelling, general
`InvokeExpr`, Parser/TKI/ABI/interface-key changes, and protocol cleanup.

See [Stage 1 remaining scope](explicit_call_boundary_cede_stage1_remaining.md).
