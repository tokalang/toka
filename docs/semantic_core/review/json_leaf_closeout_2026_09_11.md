# JSON leaf closeout — execution-blocked, not Accepted

Implementation base: `35af4aae20a059a8cfa4ec9b2909f2ec3ea107e5`.
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
- Final directed CTest: **6/7 passed, 1 failed, 149.58 seconds**. Entry, core,
  return source, binding B3 static return, binding value dependencies and shared
  aggregate handoff passed. Only `toka_json_leaf_static_error` failed. Incremental
  Debug tokac build, `git diff --check` and unapplied-patch applicability check
  passed. This is not full PASS/FAIL or RC13 qualification.

## Exact remaining execution block

The producer/slot evidence is now available, but the return collector still
adds the conservative whole-Result `m_LastLifeDependencies`. Therefore the
required `static_error.tk` positive still reports E0455. The four concrete
producer/static-rebinding positives in `enum_static_sources.tk` remain blocked
by the same return dependency merge. The failed-call rollback fixture remains
a required test, not an expected-failure oracle.

Execution review rejected both a shared-scratch clearing approach and the
narrower return-edge refinement. Neither was applied; all debug tracing was
removed. No alternative tool or indirect write was used to apply the rejected
semantics.

The **exact unapplied narrower diff** is saved as
`enum_payload_return_refinement.patch`. `git apply --check` passes (check only;
the patch has not been applied). It retains all shared scratch facts and all
independently collected return dependencies, and would replace only the
conservative whole-result merge for an exact static selected payload with no
actual dynamic/storage roots. Execution review still considers that lifetime
admission change insufficiently established. This requires human confirmation
of the concrete diff through the execution-approval process, not another vague
semantic authorization or an automatic Accepted declaration.

No thread/iterator/shared freeze ref was moved. No push or PR was made.
