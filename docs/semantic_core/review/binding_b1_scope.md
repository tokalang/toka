# Binding B1 — independent implementation, not Accepted

Base: `7240711202a80481ddbc3a123da0013f94fad1a0`.
Branch: `impl/binding-b1`. The accepted thread/sync branch and
`freeze/thread-sync-7ef33d9d` are not moved or reopened.

The user authorized B1 implementation without per-helper review pauses. The
delivery remains new cede rules working and frozen, followed by RC13
qualification; B1 is one bounded part, not that entire release.

## Fixed B1 scope

- Binding source classification for scalar unary operations (the existing
  bitwise test's `~a` is typed i32 but classified SharedHandle).
- Already checked ordinary/default-argument results: preserve actual generated
  source coordinates and reuse normal Sema facts, not guessed overload results.
- Binding initialization/whole assignment from `result!`: preserve normal
  propagation, dependencies, cleanup and failed-binding rollback. Do not rewrite
  the accepted return matrix or broaden other ownership destinations.
- Separately reconcile the three method/indirect/return-matrix integration
  fixtures with their original purposes; never blanket-bless an oracle.

Exclude networking/raw-permission changes, raw-container extraction/remainder,
partial-move/subroot capabilities, aggregate/match/capture activation, parameter
route expansion, receiver/InvokeExpr and protocol cleanup. Existing thread/sync
remains a regression baseline, not another implementation target.

## Initial evidence (read-only)

- `g03_bitwise.tk`: final type i32, ProvenCopy, no source place, no Drop, but
  SharedHandle source view; rejected ContradictoryFacts.
- `g03_default_args.tk`: generated `__LOC__` contains real literal fields but
  only the outer initializer gets a source location. The literal-origin
  collector requires a location, so the result loses its static witness.
- JSON's first `result!` assignment is typed str but has no supplied actual
  referent. `propagated_view.tk` is an isolated B1 positive for this path.
- `g03_chain_static.tk` returns a view from an owning string temporary. Its
  lifetime must be checked before claiming that the entire existing source
  ought to pass; do not silently invent lifetime extension to restore a test.

The first combined implementation patch was rejected by automatic execution
review and was not applied. It included changes to the shared return-source
collector, which were too broad for the binding boundary. The safe independent
coordinate fix preserves only existing generated literal metadata; it changes
neither types nor values nor dependency classification. Remaining source-flow
work must stay at the authorized binding boundary and preserve safety checks.

Completion requires runtime/evidence positives, negative no-artifact/rollback
controls, related regressions and one final integration comparison after the
candidate converges. No full suite is started just to rediscover known debt.

## Current checkpoint

Only the generated SourceLoc literal-coordinate fix is applied to the compiler.
The original `g03_default_args.tk` passes actual runtime, strict normal/shadow
parity and its final SourceLoc binding receipt through
`tools/scripts/test_binding_b1_default_args.py`. No full suite was run.

The local scalar-label-only attempt did not fix its contradictory ownership
tuple. The binding-local result-origin attempt also did not restore the
propagated-view positive. Both ineffective experiments were removed; the shared
return collector and its accepted behavior remain unchanged.

Automatic execution review rejected the proposed ownership/eligibility
correction and temporary-owner test migration. The complete remaining concrete
diff is saved **unapplied** in [binding_b1_unapplied.diff.txt](binding_b1_unapplied.diff.txt).
It requires explicit confirmation of those admission/lifetime changes, not a
new generic capture or thread authorization. No alternate tool applied it.

In legacy-mode IR for `g03_chain_static.tk`, `Encap_string_drop(sret.tmp)` occurs
before the returned view is stored in `v`. The proposed named-owner migration
keeps the owner alive explicitly; it does not invent temporary lifetime
extension. The original immediate `.as_view().len()` chain is retained.

`result!` remains an unresolved B1 data-flow task, not an accepted negative.
The next implementation must reuse validated binding-side facts without
reopening the global return-source model. B1 is not Accepted or ready to freeze.
