# Binding continuation: validated static factory results

Base: `ab073f160a04828dcc1fbae144995e74379e1308`.
Branch: `impl/binding-b3-static-return`.
Status: implemented and locally verified; **not Accepted**, not a complete
binding or release qualification.

## Cause and change

`static_storage_chain.tk` already had a checked return carrying actual static
literal origins. The subsequent `auto result = text()` only tried to map the
selected function's parameter dependencies. An empty dynamic dependency list
therefore lost the real static-storage proof and produced E04661.

This patch changes planner admission for call-produced borrowed values when a
completed static proof is available. It is not merely passive evidence output.
It does not alter the return-source matrix or infer lifetime from a type or an
empty dependency declaration.

### Production

- Select ordinary, source-visible, borrowed-value return definitions as
  candidates. Reference/raw returns, closure invokes and effectful functions
  are not admitted by this static-result path. Candidate selection is not proof.
- Collect only after the normal return checks: admitted `CopyIdentity`, proven
  `NoLiability`, complete `Dependency=None`, no root/referent/structural/field
  dynamic dependencies, and actual nonempty static witnesses.
- Recollect the exact literal source locations while the callee scope exists;
  addressed local storage and dynamic referents must both be absent.
- Every return contributes to completeness. A single unknown/mixed return
  prevents publication. Closure returns cannot qualify an enclosing function.
- Publish only after the whole function, all return paths and obligations have
  passed. Literal identities are unioned, not counted as separate cleanup
  responsibilities.
- Invalid/Unchecked generic dependencies poison static completeness on all
  existing direct/cache/mangled validation paths. Generic consumers also require
  the actual specialization's persistent `Valid` cache state.

### Preparation and consumption

Reuse the existing isolated definition-checking path and its
Unprepared/Preparing/Valid/Invalid cache and definition journal. The historically
callable-named private cache also handles this static-return proof; there is no
second body-evaluation engine. Forward declarations are selected using the
normally checked call type, but publication uses the callee's own resolved type
and actual returns. The later module walk does not re-run a checked body.

Fact queries remain read-only. Preparation happens explicitly after normal
expression checking and before committing a binding/return plan. Caller state
is restored by the existing transaction if preparation reports a body error.
The selected declaration and actual result type must match before consuming
the stored origins. Existing dynamic dependency mapping remains in place.

No new TKI/Evidence schema, public annotation, Parser syntax, CodeGen ownership
inference, ABI or interface key is introduced. A source-hidden function without
this proof still fails closed; its signature is not treated as a static witness.

## Focused verification

`toka_binding_b3_static_return` checks:

- the original static chain, forward declaration order, nested factory/clone
  chains, repeated cache use, two static return branches and two generic instances;
- one definition receipt per ordinary return site after on-demand preparation;
- consuming resource input still drops exactly once, while its static result
  acquires no cleanup liability;
- a mixed static/dynamic function with an explicit parameter dependency retains
  the actual owner dependency, not a static-result claim;
- normal/shadow parity and actual execution for three repository fixtures plus
  a source-visible imported factory;
- mixed/local results, nested-closure contamination, descriptor escape, invalid
  functions, missing return paths, self/mutual recursion, dynamic local escape,
  failed generic cache reuse, rejected-call source rollback, definition-journal
  recovery after a failed generic caller, and redundant `cede` of a temporary;
- the same imported factory with its source hidden: missing proof still rejects.

There are 12 ordinary negative parity cases plus the source-hidden pair, and
26 object/IR no-artifact checks. Negative tests require their intended semantic
diagnostics; parser errors cannot count as successful rejection coverage.

Related verification: the six-target group passed 5/6 in 208.16s; generic-body
qualification separately passed 1/1 in 14.60s. Pure planner and CodeGen authority
also passed 2/2 in 6.42s. Incremental `toka-tools` build passed.

After the final explicit no-dynamic-field checks and witness deduplication,
the static-return gate passed again in 40.11s. Binding dependencies and CodeGen
authority also passed again; the final return-matrix rerun reached the same
last build-buffer blocker (the three-target group was 2/3, 127.88s). Across
the nine distinct targeted CTest entries, eight pass and that one known
integration remains failed. These are targeted results, not a full-suite total.

The return matrix is the retained failure: all its earlier language/source
assertions ran through, but its final build-buffer integration is still blocked
at JSON lines 546/563 (`ContradictoryFacts`). No positive or oracle was removed,
and this known JSON issue is not folded into the static-return patch.

The separately registered local-owning-string view-return defect remains open;
this patch does not declare it fixed. No full PASS/FAIL suite was rerun, so no
updated global success count is claimed.

B1, Arena and thread freeze refs remain unchanged. No push, PR, Actions run or
new acceptance/freeze marker is included.
