# RC13 source-hidden qualification: bounded decision proposal

Status: **implementation authorized on 2026-09-23; candidate not Accepted**. Inspection baseline:
`cc391e88`; the concurrent global declaration-location correction is unrelated.
E, `fn_`, publication and push remain paused.

## Observed boundary

| Target | What its emitted interface retains | Missing qualification |
| --- | --- | --- |
| async_suspend_001_return_deps | ordinary async signatures, not bodies | actual task-result sources |
| callable_001_modes | Counter declaration and ordinary factory signature | actual mutable callable environment/mode |
| ergonomics_002_closure_dependencies | ClosureBase declaration and factory signature | actual borrowed capture mapping |
| permission_003_independent_flow | ordinary async signatures; generic bodies where applicable | actual async result summary |
| existing indirect-parameter CTest | ordinary no-capture factory signature | validated environment, even for this simple factory |

`TKIExporter::exportFunction` retains generic/forced generic-impl bodies and
the existing controlled Outcome bodies. Ordinary factories/async functions
are generally declaration-only. Existing `prepareCallableFactory` and task
result collection cannot reconstruct missing captures or result sources from
these signatures. Dependency ceilings are not actual-source evidence.

An isolated generic factory whose body is already retained passes checking,
linking and execution. This existing route need not be broadened to eliminate
the distinction between body-backed and declaration-only functions.

## Why retaining ordinary body text alone is insufficient

Reproducible arithmetic-only probes and commands:
`/Users/zhyi/GitDP/tokalang/validation/rc13-source-hidden-inventory.py`, with
results and IR under the corresponding `rc13-source-hidden-inventory/` folder.

1. Emit an ordinary factory returning a `value + 1` callback, then hide source:
   its declaration-only interface rejects qualification.
2. In the isolated interface, retain that same original factory body: checking
   and linking against the original provider succeed; runtime returns 5 for 4.
3. Change only retained body text to `value + 10`: checking/linking still succeed,
   but runtime returns **5**, not 14. Without the provider object linking fails.

These probes did not execute a dangling-reference or resource-violation program.
They demonstrate a proof/execution split, not a claim about all interface trust
mechanisms. `CodeGen::generate` treats ordinary imported-interface functions
as declarations; only the existing Outcome exception differs. Simply enabling
their body emission with `LinkOnceODR` also does not guarantee selection over
the external provider definition.

## Recommended bounded route: rechecked executable body

Request one authorization for the following connected implementation, rather
than serializing an environment-independent boolean or trusting a body which
will not execute:

1. **Retain the required source bodies.** For the actual selected factory/task
   roots in this package, export their body and the finite declaration/body
   dependencies needed to validate their result. Selection uses resolved
   declarations and operations, not names such as `make` or `block_on`.
   This discloses those implementation bodies in the interface; it is not an
   opaque-binary qualification facility. A missing required body stays unknown.
2. **Recheck normally.** Reuse the existing preparation states, instance identity,
   actual source mapping, semantic validation, journals and rollback. A summary
   is usable only after complete validation. Invalid/recursive/incomplete
   preparation grants no authority. No new automatic dependency elision.
3. **Bind qualification to execution.** The checked call must lower to a private
   consumer-local implementation of that same checked definition/instance,
   including required invoke/cleanup helpers. Do not rewrite shared AST names,
   select an old external definition by symbol coincidence, or rely on linker
   precedence. CodeGen requires this association for the new qualification path.
4. **Preserve other identities and effects.** Do not duplicate global storage,
   native handles or allocations, reinterpret layouts, change callable mode or
   refcounts, or replace a private external dependency with a same-named entity.
   Required opaque dependencies without existing sufficient contracts remain
   refused. Observable function identity must not silently change: unsupported
   address-taking/indirect uses cannot gain this new qualification merely because
   a direct call can use a local body. Whether those uses need more support is
   outside this first package, not a new general source-language prohibition.
5. **Version and trust are explicit.** Add a versioned interface policy for this
   executable-body association and reject unsupported/missing associations in
   this path; preserve existing integrity and anti-forgery checks. Native ABI,
   carriers, runtime, TaskHandle consumption and cleanup rules stay unchanged.
   Body presence or a checksum alone never authenticates an unrelated object's
   behavior. Old declaration-only interfaces remain usable only where their
   existing contracts suffice, not where this qualification is required.

This is a requested interface/execution-policy change, **not** a claim that all
five targets have already been proven implementable by a small exporter patch.
If a target requires an opaque private dependency beyond this finite body path,
report the exact dependency rather than introducing a general recursive proof
system or quietly treating it as independent.

The alternative is an object-bound, trusted provider qualification contract
with serialized actual-source/environment summaries. That retains opaque
execution but needs an explicit trust/attestation policy; a self-declared hash
or recomputed signature manifest is insufficient to establish semantics. It
is not silently included in the recommended implementation authorization.

## One candidate's validation matrix

- All four original replay directories and the existing indirect-parameter CTest;
  source-visible and source-hidden use the same functional/lifetime objectives.
- Retained ordinary and existing retained generic positives; declaration-only,
  missing helper, incomplete/invalid preparation, recursion and stale-cache
  negatives. No type-only independence fallback.
- Actual callback output, mutable/consuming cleanup mode, owned capture exact-once,
  borrowed external source survival and local-source escape refusal.
- Task external result survives task cleanup; task-frame result cannot escape;
  result pickup and repeated-consumption rules unchanged.
- The arithmetic body/object disagreement probe must **execute the rechecked
  body or reject**, never validate one body and silently execute the other.
  Check LLVM call targets and runtime with and without an old provider object.
- Mixed/missing policy versions and existing tamper tests reject; tests cannot
  fail early from a missing SDK or unknown compiler option and count as semantic
  success. Negative object/LLVM modes check intended diagnostics and no artifact.

The user approved finite body retention, consumer-local execution and necessary
interface/cache policy changes. This does not authorize opaque binary proof
authentication, E, push or release. Implementation and the complete matrix are
in progress; no candidate acceptance is claimed. The two container return cases
and the declaration-location fix remain separately accounted for.

## Implementation candidate (2026-09-23, WIP)

- Interface version `0.9.9-23`; `checked-local-v1` metadata associates explicit
  declaration ordinals with retained bodies. Missing/duplicate/unknown targets
  or absent bodies are rejected. An unassociated ordinary body cannot publish
  callable/task-result qualification just because its text is present.
- Export retains source snapshots only for selected callable/async producers
  and their checked helper/cleanup closure. The snapshots precede closure
  lowering; imports preserve them for replay-surface/digest re-export too.
  This policy does not serialize environment-independent booleans.
- Sema rechecks ordinary bodies. Dependency inspection reads the final selected
  AST, not a history of candidate probes, and uses exact resolved calls, values
  and destructor declarations. Unexportable operations do not acquire an
  executable-body association. Final validation checks the finite execution closure,
  rejects unavailable/invalid/recursive dependencies and escaped function
  identities. Global declarations/storage are not cloned. Compiler field-walk
  cleanup stays compiler-generated; user drop bodies require their real identity.
- CodeGen emits these validated implementations with **internal linkage**, not
  LinkOnceODR. Declaration pass and body pass remain separate. Exact symbol to
  declaration associations reject a collision rather than select another body;
  this first route does not silently rename an externally observable function
  identity. Closure invokes and custom drop/helper bodies share the local closure;
  existing generated drop cascades already have private linkage.
- The original four replay directories and indirect-parameter gate passed during
  development. The new package test checks old-object/new-body arithmetic and
  drop mismatches, actual task argument projection after cleanup, invalid helper,
  recursive helper, missing policy/association, function identity and missing
  private global refusal. Positive runs, normal/shadow and object/LLVM refusal
  are separate checks. Final fixed-candidate rerun is recorded in the existing
  PASS-tail ledger, not inferred from these development runs.

No native layout/protocol, reference counting, raw_take, byte-owner qualification,
E or `fn_` change is included. No generalized opaque-binary authentication is
introduced. Unsupported mixed/opaque capture qualification remains subject to
the previous environment rules; these were not loosened to test the new path.

The first fixed WIP `cb46eda8` passed four replay directories, cache validation,
unsafe-TKI anti-forgery, Outcome recheck and compatibility. Selected CTest was
**6/7**: the old task-result `source_hidden` negative now has a rechecked,
executed body and legitimately succeeds. That test is migrated to a runtime
positive; a separate declaration-only negative and the proof-rejection rollback
case explicitly remove the body and its association, preserving their original
purposes. Evidence: `validation/rc13-source-hidden-cb46eda8/metadata.json`.
This intermediate outcome is not a replacement for the final candidate rerun.

The follow-up fixed `a4b47072` run passed **7/7 selected CTests**, all four replay
directories and the same cache/anti-forgery/Outcome/compatibility scripts with no
tracked-source changes (`validation/rc13-source-hidden-a4b47072/metadata.json`).
An additional generic probe returned 14 but still used LinkOnceODR, which did
not meet the execution-binding requirement. The final candidate therefore also
associates existing retained templates with deferred policy: only qualified
concrete instances and their selected dependency closure become local. Ordinary
unselected probes are not promoted just because they entered an instantiation
cache. Clone provenance retains the original definition without reclassifying
generic impl methods as ordinary function-template cache entries.

The package matrix now primes a provider specialization, changes the interface
body and requires internal linkage plus result 14 for both generic functions
and generic impl methods. This does not disclose extra template bodies (they
were already retained), alter native layouts or authorize missing dependencies.
The declaration-only rollback control additionally requires
`TaskResultOriginsUnproven` at the actual consuming call, not an earlier error
which would trivially leave the owner untouched.
