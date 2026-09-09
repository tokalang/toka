# Stage 1: initialization and whole-binding assignment

Status: not ready for freeze — the full integration gate failed.

Base: `9efb492ebc5418ec3c3af57c397c6f3252fec62e` (accepted standalone cede).

The user authorized a shared implementation for variable initialization and
`=` replacement of a whole binding. The transaction captures source and PAL
state before RHS evaluation, retains ordinary type/permission/lifetime
validation, and restores source state on rejection. The existing non-call
planner supplies Copy KeepLive, explicit source invalidation, temporary
transfer and overlap rejection; normal validation must complete before any
carrier becomes Sema-validated. Existing old-target cleanup lowering is to be
qualified with immediate and exact-once runtime checks, not assumed correct.

Required targeted matrix: Copy/NonCopy/unique/shared source flows, source use
after transfer, bare complete temporary, old target destruction, self/region
overlap, nested evaluation followed by a failed destination check, and
normal/shadow parity. Unknown facts remain rejected, not new move authority.

Excluded: compound assignment, projected destination updates, aggregate
member activation, match binding, closure capture activation, parameter-route
expansion, receiver spelling, InvokeExpr, protocol cleanup and release work.
Constructing an aggregate/closure as the RHS does not authorize changing its
internal destination semantics. Return/source and standalone remain frozen.

Run targeted gates during development. Run the complete build, CTest and
PASS/FAIL suites once after the candidate converges, then seek independent
acceptance. There is no new Accepted revision for this slice yet.

## Current development checkpoint

The compiler builds. The initial six-fixture directed set covers Copy
invalidation, explicit source-less rejection, initialization/ascription and
assignment/write-permission rollback, self-overlap, and executable
direct/unique/shared replacement cleanup. These six directed fixtures pass,
including normal/shadow parity and rejected object/IR artifact checks;
`git diff --check` also passes. This is not full slice coverage.

## Callable classification follow-up

After the user explicitly authorized the associated planner admission and
cleanup-classification changes, automated review allowed the complete patch.
Consuming callable initialization no longer produces OwnedCallable plus
ProvenCopy. The environment reader uses the checked physical capture layout,
recursively proves independence without relying on Copy or lack of Drop, and
otherwise preserves actual referents. Unknown external results and raw-address-only
proofs fail closed. A validated parameter or field contract instead provides a
distinct LocalBounds witness: it permits bounded local flow without inventing
capture referents or claiming an independent environment. These bounds propagate
through copies into the existing lifetime collector; an undeclared return escape
is rejected. Environment facts participate in AnalysisState rollback and conservative
branch joins; a missing/different witness at a join is not promoted to known.

Ordinary dyn-fn initialization records SharedLiabilityIncremented only when
normal Sema has already selected DynFnEnvironment::Retain. Destructive transfer
and fresh environments carry DestinationAssumesLiability; thin fn values do not
acquire heap-environment cleanup. Consuming mode never gains Copy permission.
Initial preflight records are private to the binding transaction; final
callable records no longer coexist with stale preliminary tuple rejections.
That classification follow-up did not change capture admission or runtime
retain/transfer/destructor implementations. The separately authorized CodeGen
assignment wiring is recorded below.

Verification: compiler build and diff check passed. The expanded binding gate
has **16 passing fixtures**; the frozen standalone gate also passed (both CTest
targets passed together in 25.89 seconds). Actual ordinary/consuming replacement
and transfer executables clean up each capture exactly once. Borrowed-copy,
local-escape, unknown/raw environment, overlap, immutable-target rollback,
environment-fact rollback and repeated-transfer controls pass. Original direct
and consuming transfer runtime fixtures pass, as do the E04653, E04591 and
E04571 refusal controls.

The temporary rejection of whole-binding dyn-fn copy assignment was removed
by the independently authorized execution-path change below.

## Contract restoration verification

The parameter-contract and synthesized-callable metadata gaps are closed.
Both capture discovery and actual invoke-body checking receive real parameter
flags, permissions, declaration locations and cede obligations. They are not
declared as ordinary locals to obtain move authority. Discovery no longer
simulates invalidation of an ordinary parameter whose contract forbids cede.

The original 16 directed scenarios plus three contract controls pass (**19/19**).
The unknown-environment negative now uses an external result with no proof;
a valid opaque parameter's local copy chain is a positive case, as explicitly
requested. New controls reject undeclared parameter-bound escape with E0454
and ordinary synthesized-parameter transfer with E0473, and assert a real
Outstanding → DischargeToStorage transition for the cede parameter.

The binding, frozen dyn-fn lifecycle, and frozen standalone CTest gates passed
together (**3/3**, 30.54 seconds). No lifecycle or standalone gate was weakened.
Compiler build and diff check pass. No full regression run has begun.

## Separately authorized callable assignment execution path

Sema commits an internal BinaryExpr disposition only after the binding plan and
normal validation succeed: ThinValue, RetainDynamic or TransferDynamic. A clone
does not inherit that qualification. CodeGen checks the disposition against
the destination, Sema-validated item plan, completion and snapshot revision.
Missing or inconsistent markers fail with E0701 before RHS lowering.

For RetainDynamic, CodeGen obtains the RHS once and calls the existing
emitDynFnRetain before the existing old-target release/store path. TransferDynamic
does not add a retain. Capture rules, carrier layout and the reference-count
algorithm were not changed. Ordinary borrowed parameters and place aliases are
not assumed to own an old target's cleanup responsibility.

The temporary negative is now the positive callable_copy_assignment.tk. It
executes old-target immediate cleanup, source reuse, self-copy, same-environment
aliases, same-environment explicit transfer and thin-fn assignment, with final
exact-once counters. Consuming bare copy and rejected-target rollback remain
negative controls. A test-build-only local missing/mismatch fault flag verifies
that both object and LLVM-IR artifacts are absent on disposition rejection.

Targeted result: **21 fixtures plus the disposition positive/fault matrix pass**.
Binding, frozen lifecycle and frozen standalone CTest gates passed together
(3/3, 31.40 seconds). This is not the full integration result.

## One full integration run: failed

The final directed compiler revision was held unchanged throughout this run;
no exclusions or snapshot blessing were used.

| Gate | Result |
| --- | --- |
| Full build | Failed while building toka and tokafmt; tokac itself builds. |
| CTest | 37/44 passed; 7 failed. |
| PASS | 125/451 passed; 326 failed. |
| FAIL | 392/473 passed; 81 failed. |

The first shared tool-build blocker is the binding planner's RouteIneligible
at lib/std/vec.tk:213 (`auto 'val = cede self.buf[self.len]`). Many PASS failures
stem from shared library paths, including this morphic/raw-index transfer and
raw-view/JSON source flows. No unsafe-source permission was invented to hide
those failures.

The seven failed CTest gates are call-transfer shadow M1, Stage 1 method
parameters, indirect parameters, return/source, return matrix, D4a overload
probe, and signature-driven remaining routes. Examples also include interaction
with earlier diagnostic oracles: an initializer now rolls back a rejected
method RHS, and a synthesized unique-return fixture still uses `cede ^cell`
instead of the frozen canonical return form. These require explicit migration
and scope analysis, not blanket oracle replacement. The 81 FAIL-suite failures
have not yet been individually classified; they must not all be assumed to be
mere snapshot differences.

The worktree remains uncommitted and must not be accepted or released. The
callable assignment wiring has directed evidence, but the wider initialization
activation boundary and standard-library source eligibility remain blockers.

## Integration triage and crash correction

The original negative verifier was rerun without blessing while recording every
compiler exit code. All 81 ordinary verifier failures still exit nonzero: 55
snapshot mismatches and 26 legacy-code mismatches; no unexpected success was
found. Separately, member_access_sema_prefix_fail.tk exits by SIGSEGV after its
expected E0406 and was incorrectly counted as a passing test. This also
reproduces with the standalone freeze compiler in its own workspace and in
historical mode. The null initializer type is now handled without dereference,
and the verifier rejects abnormal termination before matching or blessing a
snapshot. Normal and historical modes both now exit 1 with E0406 and E0461 and
produce no object/IR artifact. No historical oracle was rewritten.

The raw failure details are in explicit_call_boundary_cede_stage1_negative_triage.json.
First-diagnostic check-only screening of all 451 PASS sources reproduces 326
compile failures, with no abnormal exit. This is not a second full PASS run.
The groups are in explicit_call_boundary_cede_stage1_positive_triage.json:

- Vec raw-index extraction is the first diagnostic for 259 PASS sources and
  36 of the 81 negative mismatches.
- Raw-view capability checks in sync/net/BTree account for 39 PASS sources.
- JSON unwrap/source facts account for 11 PASS sources.
- The remaining local/generated cases need individual classification; generated
  for-alias bindings and unsupported preflight forms must not be conflated with
  user-owned initialization or accepted blindly.
- path.tk's cede of a constructed temporary and old unique-return fixture
  spelling are source-migration candidates, not reasons to relax source rules.

The Vec issue is **not closed**. Investigation found the legacy raw-index load
and cede lowering, plus free[count] cleanup, but no existing qualified extraction
carrier containing initialized-extent and remainder-transfer evidence. A trial
that recognized the destructor/guard/decrement AST pattern was withdrawn: that
pattern is not itself proof of the pre-existing initialized raw extent. No
RawStorageElement/retired-tail admission, mask exemption, standard-library
allowlist, or NoSourcePlace relabeling remains in the code.

The proposed next bridge is an explicit unsafe raw-element take contract; see
[the separate design and validation matrix](unsafe_raw_element_take_contract_rfc.md).
It distinguishes compiler-checked type/source/dependency facts from caller-assumed
initialized storage, transfer rights and remainder maintenance. No implemented
contract currently proves the initialized extent. The contract is now Accepted
for first-batch implementation, but the primitive is not qualified and Vec is
not yet migrated. Ordinary unsafe or recognizable source spelling cannot
substitute for this explicit contract. See its implementation-status section
and the current lowering candidate for the historical rejected patch, the newly
implemented strict lowering, and the explicitly unqualified generic shared ABI.

After removing the trial admission, tokac and the pure planner build; the pure
planner, negative-termination harness, 21-case binding/disposition matrix,
frozen lifecycle and standalone gates pass together (5/5, 31.67 seconds). The
tools, seven failed integration CTests and full PASS/FAIL gates have not been
rerun as if the Vec blocker were solved.

Additional implementation review remains necessary for exact whole-binding
route selection (excluding payload/alias/projected updates), final evidence
publication and the expanded adversarial matrix. This worktree must not be
presented as Accepted or ready to release.
