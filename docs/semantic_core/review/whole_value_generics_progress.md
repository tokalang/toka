# G implementation (not Accepted)

Design/authorization checkpoint: `ddb19870`. E is not started. Work remains on
the existing integration branch; no push, PR, tag or release is authorized.

## Return-chain closeout continuation from `4549aa97` (WIP)

- A selected static factory now projects its declared return contract through
  the exact source nominal substitution, using the existing member-contract
  mapper. Resolved physical type alone is not used to invent abstract T.
  `static_factory_chain` covers scalar/unique/shared, zero-argument factories,
  explicit/inferred arguments and exact-once cleanup; its source-hidden variant
  renames the import and introduces a caller namesake. Ring's unmodified public
  lifecycle/deque gate passed (24.53 s).
- TKI expression printing preserves source `::` separately from Sema's static
  enum-value classification. This repairs the empty-variant factory round trip
  without changing ordinary `.field` syntax or the runtime ABI.
- Pattern variable recognition only searches actual enum/union variants, not
  struct fields with the same name. Option/Result unwrap of `Payload(val:i32)`
  and the original sret/self regression programs compile and run.
- Reflection get/set carries the checked owner's complete type and exact field
  index from the existing unroll into the generated member expression. The
  member checker verifies nominal identity, full type (ignoring only outer
  access writability) and field identity; normal visibility, PAL and permission
  checks still run. Wrong-owner and readonly writes reject. Ordinary opaque-T
  field access remains rejected. The two original JSON serde programs run;
  this does not qualify the old approximate reflection offset/size metadata.
- Bare T substitution now preserves the replacement's attributes instead of
  resetting them to the placeholder's default false bits. This closes the
  independently observed loss of outer raw nullability. Canonicalization tests
  verify complete identity and no mutation of the shared actual type; the
  three layered-nullability instances run and nullable-to-nonnull still rejects.
- HashMap no longer manufactures writable raw pointers merely to update an
  initialized metadata byte. Its private take bridge receives `marker#: u8`
  and callers pass the exact metadata slot. Read-only key/value probes keep
  read-only raw views. Retirement order, raw_take storage and remainder cleanup
  are unchanged. Scalar and clone/replacement gates passed; iterator-domain
  runtime controls passed after concrete shared bindings regained their hats,
  and its bound diagnostic now matches the unquoted K/V name.
- The unverified experimental writable Vec accessor was removed, not widened.
  Arena has explicit raw-domain and writable-construction declarations under
  examination, but its original write test still rejects known-readonly source
  facts. No such rejection was removed or counted as passing.
- Crypto input pointers no longer request write permission; the six MD5/SHA/
  HMAC/HKDF/PBKDF2 programs run. LLVM handle constructors no longer attach H
  to a cast type; the backend-instructions runtime control passes.
- The native-sync appended tkfrag was migrated with the protected-token scanner
  (21 markers), not by changing protocol expectations. Raw storage-helper
  returns explicitly describe their writable allocation view. Adapter/raw-take
  CTests passed 2/2 (44.71 s), without expanding raw_take's element domain.

The latest combined type/G/permission/shared/unsafe selection passed **6/6,
217.88 s**. These are targeted results, not a replacement for the recorded
363/456, 441/479, 86/99 full baseline below. A new complete run must retain its
own logs and exact implementation revision. Other-worker RFC edits stay out of
implementation commits; E/push/PR/release remain off.

## Current integration result — G is not ready for acceptance

The one complete run is retained at `/private/tmp/toka-G-final.0snzrT`.
Its implementation state was subsequently saved as `86288f97`; the runner
started at `dfbebc57` with the exact tracked diff and added-file hashes saved
alongside its metadata. Later corrections below are NOT folded into these
full-suite totals.

| Suite | Measured result |
| --- | --- |
| Complete tools build | Passed, 9.60 s |
| PASS compile/run | 363/456; 93 failures, 249.11 s |
| FAIL expectations | 441/479; 38 failures, 151.31 s |
| Complete CTest | 86/99; 13 failures, 881.23 s |

`comparison.json` compares the retained db4385dc-era full logs, not an exact
G-start baseline. PASS: 17 recovered names, 25 newly failing names. FAIL: one
recovered, four newly failing. CTest: eleven newly failing names and the two
previous failures. Test inventory and permission/library migrations changed
between these snapshots; these observed deltas are not automatically all G
regressions or G recoveries. `added-pass-triage.json` records first diagnostics
for the 25 names; its check-only successes do not override runtime failures.

### Corrections after that run (targeted evidence only)

- Vec.unsafe_get accidentally cast the complete indexed element to a pointer.
  It now calculates an address from the existing raw base plus index*sizeof(T)
  without reading/moving an element. No writable capability or initialized
  extent is added. The original stride test runs with difference 4; the real
  TLS server/client test, which crashed in both full PASS and the exact net
  gate, now completes handshake and bidirectional transfer (exit 0).
- The old morphic payload-borrow test now receives the complete &^, &~, and &&
  result types. Its original content assertions pass. The obsolete rigid-T
  negative is migrated to `g08_unquoted_full_type_argument`, which constructs
  the unique instance and checks its value at runtime (exit 0).
- Private thread-handoff declaration qualification now requires the exact
  abstract-whole formal and already matched generic binder, rather than
  rejecting legacy internal morphic flags. Trusted source, consuming contract,
  qualifier, result and environment checks remain. Its complete CTest passes
  (35.96 s); no thread runtime/protocol change.
- The negative-harness real compiler control now reaches an invalid member
  initializer in Sema, rather than a migrated fixture rejected in the lexer.
  Crash/bless mocks and object/IR rejection remain; CTest passes (1.20 s).
- The missing-quote negative now tests the still-unqualified projected unique
  transfer, requiring `ProjectedHandleRequiresSubroot`; the obsolete quote
  spelling snapshot requires E01268. The two precise FAIL checks pass 2/2 and
  are included in the G runner's parity/no-artifact matrix.
- English/Chinese syntax sections now describe whole T, concrete hats,
  non-collapsing references, explicit operation domains and unchanged explicit
  dependency requirements. Historical design records are not rewritten.

### Remaining finite work, not new semantics

1. Static factory/chain result contracts: Ring's inferred saved Vec loses the
   abstract T on pop().unwrap(); independent reproduction and exact source
   contract propagation are needed, not a Ring name exception.
2. Existing enum/reflection paths: Option/Result pattern handling and generated
   field access report E0550 / abstract-T member rejection. Separate generated
   validated access from illegal user access before making any change.
3. Library/factory writable raw return contracts: HashMap, crypto, arena and
   native adapter controls lose or lack a declared writable result. Do not
   compensate by granting write permission to all raw casts or all T.
4. Remaining snapshots/negative purposes, including the HashMap escape test
   currently blocked before its intended lifetime check. No blanket blessing.
5. Retained source-hidden callable and build-buffer dependency failures remain
   separately visible. Borrowed-record whole return remains unqualified as
   documented below; E and a new dependency framework are not prerequisites.

These items still prevent a clean G whole-candidate claim. No second full run
has been performed, and no post-fix full-green totals are inferred.
The expanded G runner passes again after these corrections (1/1, 93.95 s),
including the added explicit rejection-reason and no-artifact controls.

## Latest: approved receipt activation on `2574cec3`

Applied the exact user-approved `g_receipt_view_activation_unapplied.patch`
(SHA-256 `1f46a16cb03f2ffa7393feaa4dc6db0e26deeaf35ff3d26ea7bc2bc9029ce474`).
Production now calls `verifyArgumentBorrow` with the successful private receipt
only when the existing descriptor/referent view is consistent. A view mismatch
withdraws exclusion eligibility and falls back to ordinary conflict checking.
No broader descriptor path rewrite or active-borrow return fixture edit was
applied. The historical held-activation notes below describe earlier states.

Rebuilt tokac and the receipt unit. The receipt, G increment, permission views,
shared parameter ABI and native managed-slot CTests passed **5/5, 166.88 s**.
This includes scalar/unique/shared exact-once replacement, descriptor forwarding,
other active loans, duplicate arguments, parent/child and branch targets,
view-changing wrappers, normal/shadow parity, rollback and no-artifact controls.
An explicit receipt unit assertion also covers HandleRebind non-exemption.

The separate return-matrix rerun **failed** at the unchanged
`rebound_reference_active_borrow.tk`: E0454 for `first`, versus expected E0442.
It is not counted green or rewritten to evade the failure. No full suite was
run; G remains WIP, E and publishing remain unstarted. Other-worker RFC edits
are preserved and excluded from this implementation checkpoint.

## Earlier implementation checkpoints (historical results)

### G closeout matrix and reference regression

The G8 minimum matrix now additionally exercises a complete `View(text:str)`
through an explicitly dependent generic storage borrow, with two same-type
calls bound to different owners (`borrowed_record_values`), local escape
rejection (`borrowed_record_escape`), and repeated/zero-iteration qualified
unique-slot replacement (`qualified_slot_loop`). Runtime/parity and negative
no-artifact checks are part of the existing G runner, not a new freeze gate.

| Frozen requirement | Current evidence |
| --- | --- |
| G1/G2 full scalar/unique/shared/reference/raw type, explicit and inferred T | relay, local_relay, reference_forwarding, raw_local_relay |
| G3 members/index/call/return and concrete-vs-abstract views | structure_views, index_views, associated_values, alias_values; concrete/opaque rejection controls |
| G4 consuming handshake, no borrowed consumption or implicit Dup | named_copy_requires_cede, copy_source_invalidated, borrowed_parameter_cannot_move, reference_consumption_rejected, option_fallback |
| G5 full-slot storage, descriptor vs referent | whole_borrow IR controls, qualified_borrow, raw_slot_borrow, descriptor/local escape controls |
| G6 explicit interface domains | library_domains, Float/Copy/soul/raw/borrow bounds and source-hidden positive/negative calls |
| G7 slot permission vs inner capability, loans and cleanup | qualified_slot_write, qualified_slot_loop, permission_views, qualified_loan_* and shared_parameter_abi |
| G8 distinct actual sources, rollback, storage lifetime | borrowed_record_values/escape, branch targets, duplicate arguments, source-hidden nominal shadowing |
| Legacy spelling removal / cross-module consistency | literal_preservation, E01268 negatives, migration self-test and hidden-provider matrix |

Not qualified: whole-value return of `View(text:str)` currently rejects
IncompleteFacts in both the generic exploratory case and its independent
non-generic `borrowed_record_concrete_control`. The retained pre-G compiler
(`/private/tmp/toka-head-build-20260912`, source revision `3626db0a`) also rejects
that concrete control at the same return. This is not recorded as successful
whole-value transfer and does not trigger new dependency-elision work.

The E0454/E0442 difference was an implementation regression, not an oracle
migration: G's expanded persistent-loan registration also gave rebindable
references a permanent `BorrowedFrom/BorrowedPath` initializer alias. Preserve
the loan and existing lifetime checks, but do not install that fixed alias on
a rebindable reference; its current targets remain separately maintained.
The unchanged `rebound_reference_active_borrow` again rejects E0442 with the
original conflict location, as does the retained pre-G compiler. The temporary
G-only E04658 expectation for `rebound_reference_unknown` is reverted to its
original E0455 after the same cause is removed; the negative still reaches the
return lifetime gate. Precision of that conservative unknown-origin diagnostic
is not asserted as a proof of local storage.

The expanded G CTest passed (93.19 s), and managed-slot passed (58.67 s).
The corrected return-matrix rerun reached its final build-buffer integration
and failed on the already tracked Vec element-dependency / HashMap capability
paths; the suite remains red. Full comparison is running in
`/private/tmp/toka-G-final.0snzrT`; no full-green or G Accepted claim is made.

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

### Loan receipt revision — implemented core, activation held

The user approved the request-layer mapping and rejected the old
path/location-based `conflict.reset()` implementation. The request mapping is
now applied. The replacement PAL core uses a private, per-successful-acquisition
receipt. Failed acquisition clears the output; AST clones do not inherit it.
Snapshots preserve identity; rollback followed by a new acquisition produces a
different receipt even at the same source coordinate. Coalesced, branch-only,
merged-different and upgraded entries lose single-loan exclusion eligibility.
The verifier visits every overlapping entry and can exclude only the exact
identified loan for a compatible borrow operation, never invalidation/rebind.

The source matrix exposed persistence omissions, corrected without deleting
other loans: duplicate transient markers no longer erase a committed entry;
`&#` bindings commit their reference loan; rebindings in nested branches retain
the already-created loan in the binding's enclosing scope. Contextual writable
reference requests are supplied during generic deduction, so acquisition mode
does not depend on an absent expected type. These are requests, not source
permission proofs; independent capability checks remain.

The enabled intermediate implementation passed the receipt unit, G increment
and managed-slot selection **3/3 (134.95 s)**, including scalar/unique/shared
exact-once replacement, equal-view wrappers, other active loans, parent/child
paths, both branch targets, duplicate arguments, no-capability and changed-view
rejection, parity and no-artifact controls. This is historical intermediate
validation, NOT qualification of the current activation state.

Further inspection found that existing reference-descriptor path normalization
is not yet a sufficient view proof. Execution review rejected both a broader
path change and a conservative receipt/view guard. Neither rejected difference
was applied. Their complete original artifacts are preserved as:

- `g_descriptor_path_rejected.patch` (includes the rejected fixture rewrite);
- `g_receipt_view_guard_rejected.patch`.

To avoid publishing authority with that unresolved boundary, production call
checking has been restored to ordinary `verifyOperation`. The receipt API and
its unit matrix remain implemented, but the call-site exclusion is inactive.
Current `qualified_slot_write.tk` rejects with E0475 and produces no object;
it remains a required positive, not a blessed negative. The current receipt
unit passes. No full suite was run.

`g_receipt_view_activation_unapplied.patch` is a new, unapplied candidate against
this held state. It combines the conservative view guard with activation; it
does not apply the broader descriptor rewrite or change the active-borrow
return fixture. SHA-256:
`1f46a16cb03f2ffa7393feaa4dc6db0e26deeaf35ff3d26ea7bc2bc9029ce474`.
Explicit confirmation is needed before retrying the rejected view adjustment.

Return-matrix migration observations remain separately tracked: the anonymous
record permission negative now carries its target capability through a legal
complete type argument and still emits exactly E04573, with normal/shadow
parity and no object/IR. Unknown rebound origin now expects E04658 rather than
inventing a definitely-local origin. The original active-borrow return fixture
remains unchanged and still blocks that suite (E0454 versus its E0442 oracle);
it is not counted green or removed. G remains WIP; E/push/PR/release are off.
