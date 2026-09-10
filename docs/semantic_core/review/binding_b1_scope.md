# Binding B1 — independent implementation, not Accepted

Base: `7240711202a80481ddbc3a123da0013f94fad1a0`.
Branch: `impl/binding-b1`. The accepted thread/sync branch and
`freeze/thread-sync-7ef33d9d` are not moved or reopened.

The user authorized B1 implementation without per-helper review pauses. The
delivery remains new cede rules working and frozen, followed by RC13
qualification; B1 is one bounded part, not that entire release.

## Fixed B1 scope

- Binding source classification for scalar unary operations (the existing
  bitwise test's `~ a` is typed i32 but classified SharedHandle).
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

The generated SourceLoc literal-coordinate fix is applied to the compiler.
The original `g03_default_args.tk` passes actual runtime, strict normal/shadow
parity and its final SourceLoc binding receipt through
`tools/scripts/test_binding_b1_default_args.py`. No full suite was run.

The local scalar-label-only attempt did not fix its contradictory ownership
tuple. The binding-local result-origin attempt also did not restore the
propagated-view positive. Both ineffective experiments were removed; the shared
return collector and its accepted behavior remain unchanged.

Automatic execution review initially rejected the ownership/eligibility
correction and temporary-owner test migration. The historical unapplied diff
is preserved in [binding_b1_unapplied.diff.txt](binding_b1_unapplied.diff.txt).
The user subsequently explicitly approved both complete differences; they are
now applied. This approval does not include Lexer/Parser/formatter changes or
the earlier rejected global return-source collector change.

`test_binding_b1_scalar_view.py` passes four runtime/parity positives, scalar
plan assertions, owner cleanup IR, four negative parity pairs and eight
object/IR no-artifact checks. The negatives include local descriptor reference
escape and a view retained from an owning temporary. No full suite was run.

An additional probe returning a `str` view of a local string (not `&str` to its
descriptor) was accepted in current normal/shadow modes. It is preserved as
`b1_view_return_probe.tk`, not counted as a passing rejection test. Its baseline
status is not yet independently established; it is outside these two fixes
and must not be reported as resolved or silently change the return rules.

In legacy-mode IR for `g03_chain_static.tk`, `Encap_string_drop(sret.tmp)` occurs
before the returned view is stored in `v`. The proposed named-owner migration
keeps the owner alive explicitly; it does not invent temporary lifetime
extension. The original immediate `.as_view().len()` chain is retained.

## Propagation integration increment (not Accepted)

The `result!` minimal borrowed-view positive now succeeds. Two distinct losses
were identified: the destination's initializer source was invalidated before
RHS checking, and the later binding collector did not recognize the success
value. Updating normal dependency metadata could then record the destination
as its own source.

For the supported resolved-call borrowed-value success path, a binding-local
collector reuses the selected call's actual formal-to-actual mapping. Whole
assignment captures those origins after successful RHS Sema and before target
mutation, in the existing stack-local binding transaction (not a persistent
AST cache). The same saved origins feed dependency updates and final planning.
Normal lifetime, permission, overlap, rollback and final validation still run.
Reference/raw success and missing selected declarations gain no new authority.
The global return collector, return grammar and cleanup lowering are unchanged.

Runtime tests cover view initialization, self-replacement, changing dependency
from `a` to `b`, repeated replacement, and owned success/error cleanup during
initialization/replacement. Evidence asserts the exact referent, not just
successful compilation. Negative tests cover local escape, incorrect dependency
ceilings, retained temporary views and rejected-assignment state rollback.

`g07_test_json_serde.tk` no longer first fails at the `rem = ...!` assignment
on JSON line 529. It now reports the raw-buffer element transfers at lines
546/563 (`ContradictoryFacts`). These are not fixed or counted as restored PASS
tests, and the excluded container scope is not added to B1.

B1 still needs its remaining integration-test-purpose reconciliation and final
bounded candidate verification. No whole-suite result or Accepted is claimed.

Validation for this increment: incremental **Debug** compiler build passes;
seven targeted CTest entries pass (`binding_b1_default_args`,
`binding_b1_scalar_view`, `binding_b1_propagation`, `stage1_binding_transfer`,
`binding_value_dependencies`, `stage1_standalone_cede`,
`dyn_fn_binding_lifecycle`, all with the `toka_` prefix).
The propagation rejection test checks the actual intended diagnostics:
E0456 local lifetime, E0454 wrong dependency, E04661 temporary view, and E0408
incompatible assignment with no leaked E0438/E0410. Parser errors cannot count
as successful semantic negatives. Normal/shadow diagnostics match and rejected
cases produce neither object nor IR. `git diff --check` passes.

No full PASS/FAIL suite, Lexer/Parser/formatter change, return-rule change,
thread implementation change, push or PR is included. The accepted thread tag,
thread branch and RC13 ref remain unchanged.
