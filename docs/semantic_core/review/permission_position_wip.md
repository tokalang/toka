# Permission position / view fidelity WIP

Starting revision: `b153de9b` on `impl/json-dynamic-index`, at
`/private/tmp/toka-b6-dynamic-20260912`. Existing WIP and the three untracked
out-parameter tests are retained; no old checkout is substituted.

Latest approved rule distinguishes named binding permissions from unnamed
handle/reference capabilities. Nested `&i32#` is representable and retained by
morphic substitution; it cannot grant outer binding/member/rebind permission.
Bare `T#` is not generally allowed. Return descriptions and the separate fn#
receiver spelling remain. Vec dependency qualification is explicitly separate.

Implemented WIP: a recursive TypeSyntax position checker, declaration-to-exact
cast request association, request validation in the raw construction plan and
CodeGen, plus initial library/fixture migrations. `paramRequiresPayloadWrite`
and existing binding/PAL/lifetime checks remain. No Accepted claim is made.

The user explicitly approved `permission_new_guard_unapplied.patch`; it is
now applied. It removed only this task's new overbroad else diagnostic.
Existing allocation casts work again. Subsequent independent probes found
scalar mutability and NoSourcePlace call results incorrectly treated as raw
write authority. Applied fixes only remove these false grants. No other
rejection gate was deleted. Opaque malloc results in core/string and sys/thread
now use the explicit Addr construction, retaining malloc/unwrap/free behavior.

Current validation logs: `/private/tmp/toka-permission-position.IcQTeC`.
The five-file `permission_closure_unapplied.patch` was subsequently explicitly
approved and applied. Normal capability lookup now follows the same permission
ceiling as preflight; direct morphic substitution preserves the full argument
only for a bare morphic field, never stripping an explicit outer reference.

Completed targeted verification before the final combined run:

- Parser/type core: 19 syntax cases, complete-reference substitution and
  same-outer-type generic argument distinction. Parser/type core plus source
  position tests pass 2/2 (6.60 s); the final extra type assertion passes too.
- Three existing E0496 snapshots retain their purpose and precise hint, 3/3.
- Nine construction runtime/parity cases pass, including original allocation
  casts, name-side construction in expression/block forms, nullable guards,
  fallible allocation and no fabricated Drop responsibility.
- All fifteen construction negatives reject identically in normal and shadow
  with 30 object/IR no-artifact checks. Unsafe alone cannot grant raw capability.
  Output checks use fresh artifact directories, not stale earlier files.
- All 36 wrong-plan checks reject with E0701 and no artifact, including missing
  exact declaration request. See `triage.json` and `triage-final.log`.
- Operation controls: 6 runtime and 9 rejection cases, including explicit/inferred
  generic calls, explicit rebind, and native byte-storage write/readback.
- Views: 2 runtime, 8 rejection/parity and 16 no-artifact checks. Outer/field
  writability does not upgrade a readonly reference; mutable/readonly instance
  orders, invariant arguments and rebind ceilings are covered.
- Native zero controls: implicit value-preserving attributes accepted; nonzero,
  variable, call and explicit conversion rejected in normal/shadow, with 8
  no-artifact checks. No source-text oracle is used to bless these expressions.
- Exact 12 net programs compile, match normal/shadow diagnostics and run, 12/12.
  The explicit list is maintained in test_permission_net_regression.py, not
  inferred from an async directory count.

Library migration retains native ABI and allocation/cleanup behavior. Readonly
native inputs no longer request spurious write capability. Writable byte output
uses an explicit concrete Vec<u8> storage view with null guard and owner dependency;
it creates no safe reference to unwritten elements. UDP host/port writeback uses
ordinary declared writable outputs. Native C entry points are unchanged.

The Vec IR gate now requires the resource specialization and verifies every
emitted take interval. The concrete byte helper also emits a Vec<u8> instance;
the former assumption that exactly one take exists in the entire module is no
longer valid. Check/retirement/take ordering and the no-call interval are retained.

Scope exclusions: no generic quote removal, fn# redesign, raw_take expansion,
capture/RC algorithm or native ABI change. Root-hat aliases still obey E0493;
grammar representation does not imply alias semantic acceptance. A direct
payload return from Slot<&i32> still needs separate return/preflight analysis;
the view matrix proves access permissions, not every future no-quote operation.
No full PASS/FAIL or old baseline rebuild was performed. No Accepted claim,
push, PR, or movement of existing frozen refs is made.

Final combined candidate verification: **14/14 selected CTests passed, 172.12 s**
(`permission-gates-final.log`), including all six permission suites, unsafe raw
construction, native public/composite/witness, network adapters, shared parameter
ABI, shared aggregate handoff and Vec pop. Complete incremental tool build and
`git diff --check` passed. This is a local rollback checkpoint, not full-suite
qualification or an independent Accepted verdict.

## Independent unquoted-generic observation (after permission checkpoint)

Permission checkpoint: `bc6d252c7f08427cca1d84fd3de76ae2dff24e35`.
No compiler behavior was changed for this experiment. Reproduce with
`tools/scripts/probe_unquoted_generic.py --build-dir BUILD --output-dir OUT`.
The script is deliberately not a CTest gate and does not label a rejected
program as an intended permanent negative.

Seven operations on Slot (binding, member extraction, fixed array indexing,
passing, return, borrow, whole-slot replacement), three actual types (i32,
unique Cell, shared Cell), and two spellings produce 42 check-only observations.
Final logs: `/private/tmp/toka-permission-position.IcQTeC/unquoted-ascribed-probe`.
Ordinary T accepts 7/21 (all i32); existing morphic spelling accepts 18/21.
These are observations, **not** a runtime correctness score or complete-type
qualification. Initial malformed probe spellings were corrected before these
results; earlier probe directories are not the final matrix.

- Unquoted managed instances hit the existing rigid-generic E0604 rule. This
  is a known implementation policy to migrate, not evidence that a full type
  cannot be represented without a quote.
- Quoted unique member extraction reaches `ProjectedHandleRequiresSubroot`.
  Its existing rejection must not be removed merely to eliminate a spelling.
- Quoted managed array cases stop at array initialization `IncompleteFacts`,
  before the index operation. They do not establish index behavior either way.
- Quoted shared extraction with explicit full-type ascription is accepted;
  inferred extraction is a distinct existing morphology issue, not silently
  counted as covered.

Next bounded step is separating whole-value generic interpretation from these
existing admission restrictions, with reference/readonly and concrete-binding
controls. No replacement placeholder is proposed. Removing `'` must not imply
opening partial moves, declaring array dependencies complete, or reopening the
permission checkpoint. This experiment does not yet claim the full seven-route
no-quote implementation or authorize deleting existing safety checks.
