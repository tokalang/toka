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

### Whole indexed-slot borrow continuation

Checkpoint `c0e9d1c2` saves the exact approved schema patch before continuing.
A standalone `View<T>(&value#:T)` constructor reproduced the remaining E04573
without native sync, threads or factory witnesses. The existing selected-slot
capability path recognized only the removed quote operator. It now also
recognizes a checked `IsAbstractWholeValue` index; its storage type, inner type
equality, source capability and flow-ceiling checks are unchanged.

The independent writable slot constructor now compiles to object/IR; its
readonly-storage counterpart still rejects with E04573 and no artifact.
The shared matrix is **24/24** after this fix. Permission views passed,
including outer/field mutation ceilings and cross-instance isolation.

The managed-slot readonly-guard fixture previously requested a writable
reference from a readonly borrow and failed before reaching its assignment.
It now obtains the legitimate readonly reference, then attempts replacement;
the original E04572/E04573 rejection expectation remains. Extra assertions
exclude undeclared-slot and initializer-capability failures. All managed-slot
runtime, rejection/rollback and fault checks pass (34.80 s).

Four morphology negative sources were mechanically migrated from quote syntax;
their domain/unknown-bound diagnostic expectations are unchanged and now run
inside the G gate. The latest G gate passed (29.83 s): 12 runtime cases, one
compile-only slot-constructor control, 18 rejection/parity cases with 36
object/IR no-artifact checks, and three caller-storage IR controls. The
compile-only constructor is not counted as a runtime cleanup test; managed
storage execution is covered by the existing shared/managed-slot programs.

G remains WIP with the propagation, constraints and full-comparison items
above outstanding. The concurrent RFC changes remain outside implementation
commits. No push, PR, E work or full-suite run occurred.

### Library contracts and remaining expression routes (WIP)

Continuation from `02db4283`, not G acceptance. The concurrent RFC supplement
remains outside implementation commits.

| Entry family | Previous assumption | Migration and evidence |
| --- | --- | --- |
| Twelve std/math floating wrappers | T happens to have float methods | explicit Float + Copy; runtime f32/f64 and source-hidden bound rejection |
| std/math numeric utilities | T happens to be numeric/Copy | concrete scalar entry points, shared private Copy algorithms; i32 default (f64 for lerp), other widths use scalar suffixes |
| Nine atomic hooks and nine wrappers | rigid T plus intrinsic qualification | explicit Copy + soul_only, forwarded write request; existing trusted machine-type and ordering checks remain; Copy shapes still reject |
| Option.unwrap_or | consuming bare T works for every T | consumes a complete Option fallback; discarded fallback/resource/borrowed-value runtime controls |
| Vec.unsafe_get | old quoted element selector | select the declared raw handle before indexing; no raw_take or initialized-storage expansion |
| PlaceIterator source | quote node required | same registered provider, parameter root, access path and exact item type; plain checked place replaces quote |

Numeric overload trials exposed legacy imported-overload selection ambiguity.
Distinct numeric suffixes avoid adding overload-resolution work to G. The
original math runtime assertions pass after this API and bound migration.
`bit_cast` was inspected but not changed: its existing intrinsic path bypasses
template bounds, so source-only labels would not enforce a new contract. It
was not protected by rigid-T instantiation and is not counted as qualified.

The protected-token scanner migrated 64 test files / 374 markers before
purpose-specific adjustments. Removed-syntax negatives keep their quoted
source and expect E01268. The former requires-morphic negative moves to
`g08_unquoted_morphology_constraint.tk` as a positive. Some remaining fixtures,
including the old unique projection-return negative, still need purpose
alignment; mechanical migration does not count them as passing.

Expression fixes:

- Exact impl-associated RHS declarations expand formal source contracts:
  Item = T stays whole T; a concrete RHS is not made opaque. Normal type and
  trait validation remains mandatory.
- Inferred whole raw/reference locals synchronize physical binding metadata
  through the existing unique/shared path, without bypassing the planner.
- Checked GenericViewDepth records outer declared layers before T. CodeGen
  validates the final type and selects only these layers; clones re-elaborate.
- Explicit &value:T is not a redundant concrete borrow. Readonly local
  reborrows retain T, and non-rebinding reference arguments pass the checked
  reference value instead of stripping &&r to its pointee.
- Explicit descriptor selection in &&local preserves the local storage root;
  the new escaping-descriptor control rejects with E0455.
- A consuming match of a plain resource is not an implicit copy; the
  non-consuming resource match still rejects with E0554.

Validation: G/call-shadow/managed-binding/shared-aggregate passed 4/4
(157.95 s). After the borrow changes, G/permission-views/shared-parameter/
managed-slot passed 4/4 (106.68 s). The final G gate, including source-hidden
associated values and descriptor-preserving borrows, passed (50.65 s).
Earlier failed iterations are not counted as passing.

Still WIP: general alias/method-result propagation, complete qualified-write
and branch/loop coverage, remaining fixture/purpose migrations, then the single
full build and integrated comparison. No full PASS/FAIL run, E work or push.

### Method/alias continuation and execution-review boundary

`4cfd0a17` preserves the preceding library/source-view work. This continuation
adds declaration-owned result contracts for generic receiver methods and a
read-only alias expansion using exact source-module/lexical import identities.
Aliases are refreshed after imports become available and before specialization;
no body is re-executed and no specialization's actual types are used to infer
the original declaration role. Strong aliases and unresolved/unsupported alias
forms are not silently made opaque. Same-module alias chains and renamed
source-hidden imported aliases now pass the expanded G gate (71.45 s).

The previous four-test selection was 3/4: only the newly added imported-alias
case failed; permission views, shared parameters and managed slots passed.
The targeted alias rerun above closes that failure, not a full-suite rerun.

`qualified_slot_write.tk` is a required pending positive, not a passing negative
or an excluded supported domain. Its generic target view/capability and cleanup
address preparation are in WIP, but it still rejects with E04571 and E0475.
Two attempted changes were rejected by execution review and were NOT applied:

- `g_qualified_reference_intent_unapplied.patch`: maps an explicit selected
  handle-rebind request to the outer borrow's slot-write request. It does not
  replace the independent source capability checks. Review flagged possible
  write-permission elevation.
- `g_qualified_reference_loan_unapplied.patch`: checks exact path, origin and
  transient loan state to avoid treating the argument's own already-acquired
  reference loan as a second incompatible borrow. It does not remove the loan
  or pairwise argument checking. Review flagged possible incompatible-loan
  admission. This implementation still needs explicit human confirmation and
  dynamic tests; its safety is not asserted from the description alone.

Both complete original differences are saved locally in this directory
(gitignored review artifacts). SHA-256:

- intent: `94cb815b492843a648d167d2dfbb1d382fc2094288400f11b43ecc3ea3999753`
- loan: `f0af3ec957df4b9b41e4490b3bc1fe265980a0d403ffa1b98a2fc2ddc6f725a8`

Patch target hashes, prior to the next checkpoint:

- Sema_Expr.cpp: `ce53b399b92afef72cc8e62a365a9f3c9509c16dc0714f61da0786edee9ddcd8`
- Sema_Expr_Call.cpp: `0d000433c4039671e85942e03b1d238237baed19de133965bb7a6f3d2412eeea`
- PAL_Checker.h: `20385b42cdb0a99eb6af0790a23ff9a37f0db9d7169a724875919fed887b5a58`
- PAL_Checker.cpp: `d2ade28894010de800d44748941c18ac5312ad750536fb980fff54c39703cf4d`

Before qualified writes can count as complete, require scalar/unique/shared
replacement and old-value cleanup, readonly source/request rejection, prior
active borrow and duplicate-argument rejection, source/target rollback, plus
normal/shadow parity and no artifact on rejection. Do not bypass the execution
rejection or call the existing partial G gate full qualification. E, full-suite
comparison and publishing remain unstarted.
