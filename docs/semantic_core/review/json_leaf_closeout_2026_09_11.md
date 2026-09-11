# JSON leaf closeout — complete candidate, not Accepted

Implementation base: `35af4aae20a059a8cfa4ec9b2909f2ec3ea107e5`.
Last WIP checkpoint: `ee186ac27f72dd4a24caef71882545496ee69c40`.
The user explicitly authorized enum branch source refinement and complete
borrowed-destination replacement cleanup. No new public JsonFactory, generic
bounds, recursive container witness, receiver-write protocol, TKI or ABI work.

## Implemented

### Exact enum payload evidence

- Summaries are keyed by the checked producer FunctionDecl/instance. Each
  current result additionally carries its producing expression and exact enum
  declaration. Payload selections identify a formal, variant and slot, not a
  `Result` type name or `unwrap_err` spelling.
- A slot is static only when every returned possibility for that variant
  supplies static storage. Unknown returns invalidate the result summary;
  dynamic and static sources for the same slot intersect to non-static.
- The first implementation recognizes literal storage and checked immutable
  literal binding chains. It does not turn arbitrary missing dependencies into
  static evidence. Actual payload and selected result types must agree.
- Current binding sources are separate from producer summaries. Assignment and
  writable/unknown call exposure revoke or replace their evidence. AnalysisState
  captures, joins by intersection and restores it with existing transactions.
  Function checks isolate their binding maps.
- References/raw results are excluded from static payload refinement. Err
  evidence does not qualify Ok/rest or the local descriptor containing a view.
- `enum_metadata.tk` verifies through the existing non-call JSON that the
  selected Err binding has static storage and no dynamic dependency roots,
  while retaining an input-bound return signature. It passes without removing
  any global dependency facts.

### Old destination cleanup

- Sema seals a per-assignment BorrowedValueReplacementPlan only for a normally
  admitted, whole direct-value borrowed parameter destination whose entry
  snapshot is definitely live and fully initialized, with writable capability,
  exact destination identity and complete Drop facts.
- This is not scope ownership: no DropFlag is registered for the borrowed
  parameter. Initialized/local, moved-from/local and cede-owned parameters
  retain their existing flag/mask behavior.
- CodeGen validates source/destination/type/place/snapshot against the admitted
  assignment carrier. It rejects missing/inconsistent plans and any collision
  with scope DropFlag/mask or native replacement. After RHS production, it
  drops the old complete value once and installs the new value.
- The seven test-only fault modes do not exist in production option parsing.
  CodeGen faults are tested in object and IR emission modes, not shadow mode:
  `--non-call-transfer-shadow=json` is deliberately check-only.

## Verification

- Entry gate passed: five existing i32/string/str behavior checks, four string
  replacement/failure cleanup checks, six custom-resource/callee-scope and
  moved-from checks, fourteen missing/rejected/mismatched-plan no-artifact
  checks, and readonly rejection/source rollback with object/IR non-production.
- Factory core passed: 22 behavior checks, eight cleanup checks, exact Err
  metadata, and twelve lifetime/dependency refusals with normal/shadow parity
  and object/IR non-production. Different producers, mixed Err sources,
  rebinding, branch joins, loop paths, writable calls and descriptor addresses
  have explicit refusal cases.
- After applying the exact confirmed refinement, final directed CTest:
  **7/7 passed, 147.86 seconds**. Entry, core, static-error, return source,
  binding B3 static return, binding value dependencies and shared aggregate
  handoff all passed. Static-error covers the original lifetime positive,
  all three concrete error producers, static rebinding and failed-call rollback.
  All twelve core refusals were rerun after application, including normal/shadow
  parity and object/IR non-production; no earlier results were substituted.
- Incremental Debug tokac build, `git diff --check` and reverse applicability
  of the confirmed diff passed. No full PASS/FAIL suite or RC13 qualification
  was run. This is a complete concrete-factory candidate, not an Accepted marker
  for the candidate, full JSON module, binding slice or release.

## Applied return-edge refinement

After the user explicitly confirmed the exact saved diff, execution approval
allowed its application with `apply_patch`. No shared-scratch clearing change
was applied and no execution-review bypass was used. The historical exact diff
is retained in `enum_payload_return_refinement.patch`; its reverse applicability
check passes against the implementation.

The return collector retains `collectDeps`/`collectMemberDeps`, all shared
scratch state and subsequent lifecycle checks. Only the current expression's
conservative whole-Result merge is omitted when actual-origin collection is
complete, has no dynamic or addressed-storage roots, and the exact typed enum
payload has nonempty static storage evidence. These are not independent proofs:
the safety claim depends on the upstream producer/variant/slot summary remaining
complete and current.

The original static-error positive, all three concrete factory error returns,
static rebinding and failed-call rollback have been rerun after application.
Dynamic Err, different producers, rebinding/joins, descriptor addresses and
Ok/value/rest escapes are likewise rerun; earlier refusal results are not used
as substitutes. No tests were changed into expected failures or skipped.

No thread/iterator/shared freeze ref was moved. No push or PR was made.
