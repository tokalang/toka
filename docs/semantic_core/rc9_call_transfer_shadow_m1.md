# RC9 M1 Call Transfer Shadow

**Status:** Stage-0 audit-only receiver-plus-arguments transaction implemented.
The transaction has no commit authority and cannot change public behavior.
Non-normative and not an activation of the signature-driven call-transfer ADR.

**Baseline:** `a3de6d4787f22f2b003949e62bf9aa1c83b40d17`.

M1 records an Accepted-RFC call plan beside the current normal call semantics.
Normal Sema, PAL, diagnostics, cede obligation evidence v1, and CodeGen do not
consume this plan. Call-transfer shadow inherits normal signature-driven call
behavior exactly; it does not switch the compiler to the frozen RC8 profile.
Historical cede-obligation v1, D3, and authority-facts profiles remain frozen.

Shadow computation itself is enabled only by the audit command. Ordinary
compilation and every existing evidence mode do not run the planner or populate
its Copy/dependency caches as a side effect.

## Audit command

```text
tokac --call-transfer-shadow=json --check-only source.tk
```

The command emits the internal, audit-only schema:

```text
toka.internal.call-transfer-shadow / version 5
```

It is not cede obligation evidence v2 and has no public compatibility promise.
It exists to qualify the planner before any behavior or public evidence change.
It cannot be combined with another JSON, semantic, or evaluation output mode.
Version 5 retains the version-4 argument records and adds audit-only whole-call
transactions prepared before legacy ownership mutation. Each transaction has
receiver/argument slots, per-item outcome/source/drop/obligation dimensions,
`local_plan_admitted`, final validation state, and a `commit_allowed`
observation bit. That bit is published only after legacy call validation and
is never consumed by Sema or CodeGen. Version 4 added the nested audit-only Stage-0
plan produced by the Accepted explicit-cede pure classifier. The legacy fields
remain the behavior oracle; Stage 0 neither changes diagnostics nor grants
CodeGen authority. Version 3 originally replaced the M1a.1 schema and retains
structured source/referent
identity, stable indices, value category, drop disposition, dependency paths,
and cleanup-mask capacity, and adds actual `init` spelling. Postfix value
operations, `T | miss` payloads, and thread boundaries now use qualified
semantic facts rather than syntax/name shortcuts.

## Recorded dimensions

Each record is created after a call route resolves its formal parameter:

```text
route             ordinary | static | method | callable | extern
                  | indirect-fn | indirect-dyn-fn | dynamic-trait-method
argument/formal   stable original indices; receiver is a distinct role
value_category    Place | Temporary | InitStorage | Indeterminate
spelling          implicit | explicit
init spelling     formal_init and actual_init are independent facts
transfer          BorrowCapture | CopyValue | CopyIdentity
                  | TransferShared | MoveOwned | ConsumeTemporary | Reject
source            KeepLive | InvalidatePlace | NoSourcePlace | NoStateChange
dependency        None | Borrowed | RawUnsafe | Indeterminate | Unclassified
place_eligibility NotApplicable | PendingValidation | Eligible | Reject
drop               SourceRetainsLiability | DestinationAssumesLiability
                   | NoLiability | NoStateChange | PendingValidation
execution_boundary None | StartHandoff | ThreadHandoff
                   | StartAndThreadHandoff
source_identity    structured AccessPath plus display path
referent/deps      PAL referent and lifetime dependency paths
cleanup_mask       null until an admitted partial cleanup plan exists
legacy            cede exemption and missing-explicit-cede facts
effect             async fact
stage0             outcome/rejection, independent value/source/destination/
                   drop/obligation dimensions, actual/formal type and
                   morphology, H/P capability facts, exact SourceView/path,
                   typed referent/dependency roots, type/temporary/route
                   eligibility, reachability, and deterministic semantic root
                   when proven
transaction        receiver-plus-arguments item plans, aggregate outcome,
                   arity, shared source/PAL snapshot revision, liveness,
                   init/cleanup/liability, local-plan result, final validation,
                   and non-authoritative commit observation
```

Stage-0 obligation evidence has separate source and destination actions. A
partial source can therefore preserve an outstanding caller-root obligation
while independently creating an outstanding callee obligation. Formal
ownership and transfer class are also separate from payload Copy proof, so
borrowed/raw/callable identity classification remains identity-first.
The formal record retains concrete versus generic-value declaration origin;
instantiating a generic value contract with a borrowed payload therefore does
not silently turn it into a concrete borrowed-identity contract. Until typed
ownership subroots exist, a projected unique-handle source rejects
`ProjectedHandleRequiresSubroot` rather than invalidating its lexical root.
Raw `*` selector syntax is removed from the typed place projections before
whole-source obligation coverage is decided; it cannot masquerade as a partial
dereference move.
The transaction queries PAL through a snapshot and reads only already-proven
shape Copy facts. `Addr` and `OAddr` are frozen as scalar Copy facts inside the
Stage-0 provider without changing the legacy `proveSlice4CopyType()` policy.

Every Stage-0 rejection carries `source=NoStateChange`. A place whose stable
declaration coordinate cannot yet be recovered rejects `IncompleteFacts`
rather than falling back to the legacy process-local `source_root_id`.
The Sema audit path now captures one immutable source/PAL revision before
receiver checking, generic deduction, or argument checking, then prepares
receiver and every selected argument from that shared revision,
and submits the receiver and arguments to the pure whole-call planner as one
transaction. Pairwise invalidation/read/referent/dependency conflicts reject
the aggregate with `commit_allowed=false`. Moved, uninitialized, and partially
cleaned places carry their exact liveness and cleanup mask and cannot be
admitted as live sources. Arity and unmatched slots are explicit. Legacy
checking then runs unchanged; only after it finishes may a locally admitted
plan report `commit_allowed=true`. The final conjunction also requires an
explicit route-completion marker, pre-mutation preparation, and one shared
snapshot revision. Neither the transaction nor the older
per-argument shadow records are commit authority.

Caller callable spelling and the selected receiver contract are frozen as
separate entry facts, including indirect `fn`/`dyn fn` calls. Same-typed owned
temporaries use a deterministic cleanup identity containing the logical module,
call coordinate, receiver/argument role, slot index, and expression coordinate.
This is a source-transfer-edge identity: it is unique between roles and slots
of one logical source call, while generic monomorphizations of that same edge
intentionally share it. It is not a globally unique runtime-instance identity.
Any future cross-instantiation cleanup correlation must introduce a separate
instantiation key rather than silently widening this identity's meaning.
Post-legacy receipts may only reuse a pending pre-mutation item; speculative or
missing-pending paths cannot synthesize a post-state Stage-0 plan.

Argument indices are one-based. Method/callable receiver roles are separate;
an indirect callable receiver has no declaration-side formal and therefore
uses formal index zero and fails closed. Shadow plans are discarded when an
AST call is cloned and its resolved formal is reset.

Version 5 deliberately represents indirect arity failure with
`expected_argument_count`, `actual_argument_count`, collected item count, and
`arity_complete=false`; it does not synthesize fake missing-formal or
extra-actual item plans. These counts are sufficient for fail-closed authority.
Per-unmatched-slot forensic records require a later audit-schema revision and
are not part of the Stage-0 call-transaction contract.

## Deliberate M1 limitations

- `PendingValidation` is not permission to move. The audit does not replace the
  whole/partial exact-place eligibility checker with calls.
- A shape, smart pointer, array, or dyn callable whose complete dependency
  closure has not been routed into the planner is `Unclassified`, not
  dependency-free. Direct borrowed-view dependency paths are recorded
  separately from the source binding and PAL referent.
- CodeGen continues to use `CedeExpr` and the existing aggregate transfer
  paths. It never reads `ShadowArgumentTransfers`.
- Existing RC8 thread-safety diagnostics still use their source-spelling
  classifier. M1a.2 records declaration-identity boundary facts but does not
  switch those diagnostics; a user same-named function therefore retains its
  historical diagnostic until a later admitted replacement.
- Callable, method, and dynamic-trait receiver slots are present in the audit
  transaction. The accepted `InvokeExpr` spelling and callable behavior change
  remain outside Stage 0.
- Cede obligation evidence v1 is unchanged.

## Qualification

`tools/scripts/test_call_transfer_shadow_m1.py`, registered as
`toka_call_transfer_shadow_m1` in CTest, checks:

- named Copy place preservation and explicit invalidation;
- whole temporary consumption;
- ordinary, static, method, callable protocol, indirect `fn`, indirect
  `dyn fn`, dynamic-trait method, generic, and extern routes;
- current normal implicit-transfer receipts plus independent future
  `MissingCedeForNamedSource` local-plan rejection;
- shared-handle transfer classification;
- exact audit/normal return-code and stderr parity, including dynamic-trait,
  argument-alias, and all three former shadow-only `E04570` cases;
- suppression of speculative/capture-precompute transactions;
- async `.start`, nested non-boundary calls, and resolved-declaration thread
  handoff annotation, including aliases and a user same-named function;
- init formal/actual spelling, unknown actuals, unary/cast/address/postfix
  temporaries, exact source identity, borrowed-view and projected referent
  paths, recursive `T | miss` carriers, and multi-argument indices;
- source/source-less plan parity;
- forced check-only single-document JSON and fail-closed output conflicts;
- four normal-mode diagnostic/success receipts;
- non-`cede` formal rejection with no source-state commit;
- receiver-versus-argument and argument-versus-argument atomic alias rejection;
- one shared receiver/argument source-and-PAL revision, moved/uninitialized/
  partial-cleanup rejection, explicit arity, and unmatched-slot evidence;
- locally admitted calls rejected by final execution-boundary validation;
- explicit route completion and pre-state/same-revision final commit gates;
- method, callable, and indirect callable receivers, including consuming
  callable spelling and member/index receiver facts;
- the four indirect caller/formal receiver handshake combinations;
- distinct deterministic liability identities for two same-typed temporaries;
- indirect `fn` and `dyn fn` too-few/too-many arity transactions;
- typed declaration provenance, source-hidden fail-closed transactions, and
  route-specific transaction outcome/source/drop/obligation assertions; and
- hermetic external-build CTest execution through an injected source
  `TOKA_LIB`.

The script emits a compact receipt named
`toka.rc9-m1-call-transfer-shadow-audit`.

## Next admission step

Stage 0 does not authorize the caller-spelling behavior flip. Non-call
destinations, CodeGen authority, interface-key changes, and Stage 1 remain
outside this milestone and require separate authorization after transaction
review.
