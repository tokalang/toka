# G implementation (not Accepted)

Design/authorization checkpoint: `ddb19870`. E is not started. Work remains on
the existing integration branch; no push, PR, tag or release is authorized.

Implemented so far:

- Exact generic-binder recognition records an abstract whole-value formal
  before specialization; the fact survives formal cloning. It is not derived
  from the resolved pointer kind or merely being inside a generic function.
- Direct generic cede/return and inferred local transfer retain full types.
  Checked expression/local facts are re-elaborated, not cloned as runtime roots.
- Direct whole-T borrowing preserves i32/unique/reference storage and the
  additional reference layer. A checked `return &parameter` storage-level fact
  is mapped through the explicit dependency route; it neither invents a route
  nor treats a local descriptor as its referent. Reference-value forwarding is
  separately tested and remains distinct from new descriptor borrowing.
- Whole-slot requests map to the existing H requirement for handle instances,
  separately from the transported T's internal P ceiling. Assignment does not
  implicitly decay such an operand into its payload.
- Lowering uses the caller's real captured slot and full resolved cleanup type
  for replacement, without granting scope ownership of a borrowed parameter.
- Opaque T does not acquire concrete fields or a concrete root selector merely
  because specialization reveals them. Concrete source bindings stay concrete.
- Compiler interface `0.9.9-20` rejects pre-G TKI/cache. Native handoff v1's key,
  layout and algorithm remain unchanged; the compatibility test explicitly
  verifies stale compiler-interface rejection and native link rejection.

The incremental `toka_whole_value_generics` test covers direct/local transfer,
scalar/unique/shared replacement and cleanup, internal writable-shared ceiling,
source-hidden relay, forbidden borrowed moves, payload/owner confusion, P/H
confusion and opaque field/root selection. There are six runtime/parity cases
(including the source-hidden program), eight rejection/parity cases with 16
object/IR no-artifact checks, and three exact caller-storage IR controls.
This is not the full G matrix.

Verification: `g-core-checkpoint.log` records the incremental G suite; the latest
combined selection before the final borrow additions was **6/7 CTests**
(`g-targeted2.log`, 88.98 s). The remaining legacy shared-parameter test is
still red and is not reclassified as a passing negative. The independent
interface/cache/native-link compatibility script passed (`g-interface.log`).

Still to implement before G qualification:

1. Complete associated/alias and remaining expression propagation after the
   now-implemented structure/member/fixed-index and call-result paths. The
   old rigid shape check has been removed; explicit bounds remain required.
2. Extend the direct borrow controls to generic members/indices, reference-valued
   replacement, aliases, branch/loop state, constraints and unknown facts.
3. Finish library/interface-domain and test migration. The source quote forms
   now reject with E01268, character literals remain valid, and interface
   version is 0.9.9-21. Library/tools spelling migration removed 1,393 markers
   in 38 files without touching strings/comments. No source dual mode is used.
4. Full targeted G matrix, then one integrated comparison. No full PASS/FAIL
   run has been performed during this implementation checkpoint.

Migration observations: the legacy shared-parameter matrix reads `.value`
directly from opaque T. It needs a valid declared observation contract, not an
exception to G3. Callback/trait trial migrations did not qualify (a Token trait
does not automatically apply to ~Token); those trial edits were removed rather
than relaxing trait or execution-boundary checks. The original matrix remains
as a required migration target. The aggregate-copy test's identical surviving-source content
check now runs in the concrete caller; retain-count and cleanup assertions are
unchanged. General shared ABI tests are not counted green until migrated.

Logs are in `/private/tmp/toka-permission-position.IcQTeC` (`g-*`). The RFC's
uncommitted §9 migration-criteria supplement was added concurrently and is left
separate from the implementation changes.

## 2026-09-14 continuation

The interrupted run timed out in two groups; it is not counted as passing.
Standalone reruns passed the earlier G group and the complete shared-parameter
matrix (24/24). The shared test now obtains observations via a typed callable;
its original 13 runtime cases and exact carrier/cleanup checks remain. A
field-mask cleanup defect skipped unique members and is corrected using the
same typed owner dispatch as the non-mask path. Contextual cede payload decay
is removed: an enclosing result type cannot change the selected source view.

After quote removal, `g-noquote-expanded.log` reports 12 runtime cases,
12 rejection/parity cases, 24 no-artifact checks and the existing three storage
IR controls. This includes enum transfer, literal preservation and source-hidden
structure/nominal-shadowing checks. It is not the full G matrix or a full suite.

Execution review initially rejected a proposed three-file schema migration.
The exact patch was subsequently explicitly approved against `b5398f26` and
has now been applied. Its original Codex patch-format artifact is
`g_noquote_schema_unapplied.patch` in this directory (locally present, gitignored).
The execution approval did not reopen the RFC or accept the whole G candidate.
The concerns were the legacy morphic-exemption marking and native declaration
schema admission. The patch also keeps cede/reference substitution fail-closed
and retains actual morphology-bound checking for every type parameter.

Patch SHA-256: `9b39a4f7c0692f1572e9207d6626151bf91a931c749282dbf7433cd51ce0a041`.
Base HEAD is `372d83266cbf6ab8b82f2564244132d7b3ac0f71`, **plus current G WIP**;
it must not be applied to that clean commit alone. Target file SHA-256 values:

- Sema.cpp: `d0e012e3fd8ea39128ee935bacc1268e0a2d814b430ac8f26b3ba65ac79aaf44`
- Sema_Template.cpp: `ae793371952724caca600008f7c4baa5cde26d9052f0882c6a4dadda6de13d1f`
- Sema_NativeSync.cpp: `b36d8e7e0a7d006c83c31530b9afc15702b263be8fe2b31c43cf934aa32e098d`

The post-migration shared run is **21 passed / 3 failed**, not green: two sync
fixtures reach the old declaration schema, and the morphology-domain fixture
reaches the obsolete "requires morphic parameter" check. No oracle was changed
to accept these failures. E, push/PR/release and the final full comparison have
not been started.

### Approved schema migration checkpoint

The three-file migration is applied unchanged. The native factory recursion
fixture now matches the unquoted signature exactly once before injection;
its recursive rejection and no-artifact assertion are retained. G also adds
a reference-consuming specialization rejection, checked in normal/shadow and
object/IR modes.

Validation after rebuilding tokac: the G suite passed (12 runtime cases,
13 rejection/parity cases, 26 no-artifact checks and three storage IR checks),
and the native factory plan suite passed including its fault injections.
The initial C++ factory-flow test still used an old linked frontend and failed;
after rebuilding the C++ targets, permission syntax, closed payload and
factory flow all passed (3/3, 2.69 s). The initial failed run is not counted green.

The shared matrix is now **23/24, no skips**. Both the declaration-schema
blocker and morphology-domain blocker are closed. The remaining
`sync_managed_storage_pending.tk` rejects the generic managed-slot reference
at sync.tk:141/167 with E04573 (plus a downstream prepared-recipe failure).
This remains a G integration failure, not an accepted negative; investigate
the whole-slot versus inner payload view without relaxing permission checks.
No full suite was run. This is a local WIP checkpoint, not G acceptance.
