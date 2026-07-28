# Toka Semantic Core

The 1.0 execution order, phase status, blockers, and stop conditions are
tracked by [`../1_0_closure_plan.md`](../1_0_closure_plan.md). This directory
provides the semantic evidence indexed by that plan.

This directory is the phase-1 audit surface for the Toka 1.0 semantic core.
It turns the frozen language rules into a stable contract that can be reviewed
against implementation, diagnostics, interface replay, and tests.

The scope is intentionally narrow:

- PAL path safety and borrow validity.
- Ownership transfer, move state, `cede`, drop/clone obligations, and resource
  copy prevention.
- Escaping borrow dependencies and `effects:` routing.
- Async, task, and execution-boundary safety.
- `.tki` interface replay and cache invalidation for semantic facts.

This is not a new language proposal. It is an audit layer over the current
compiler, `docs/syntax.md`, and `docs/1_0_freeze_decision_list.md`.

## Documents

- `rule_template.md` defines the fields every semantic rule should carry.
- `rule_matrix.md` records the first 1.0 rule matrix and maps rules to current
  diagnostics, implementation areas, positive tests, negative tests, and gaps.
- `tki_semantic_contract.md` records the semantic facts that `.tki` interfaces
  and dependency-cache metadata must preserve for source-less replay.
- `phase2_cache_replay_closure.md` records phase-2 cache and replay completion
  evidence.
- `phase3_evidence_necessity_audit.md` audits the compiler need for structured
  evidence, alias analysis, and backend optimization contracts.
- `phase3a_structured_facts.md` records the implementation and verification of
  the first structured semantic-fact layer.
- [`../semantic_evidence_v1.md`](../semantic_evidence_v1.md) freezes the public
  decision-evidence protocol, schema, SDK entry point, and ABI gate.
- `phase3b_decision_evidence.md` records the implementation history, causal
  diagnostics, and source/interface equivalence checks that underpin it.
- `phase4a_trusted_memory_evidence.md` freezes the cross-module evidence trust
  model.
- `phase4b_trusted_evidence_cache.md` defines the object-bound cache sidecar.
- `phase4c_evidence_replay.md` records three-path replay and tamper coverage.
- `phase4d_cross_module_nocapture.md` records activation and the cross-module
  optimizer-benefit decision.
- `phase5a_bounded_readonly.md` freezes the bounded `readonly` experiment and
  its stop conditions.
- `phase5b_writeonly_preflight.md` records the bounded `writeonly` feasibility
  stop before backend emission.
- `fz1_async_suspension_closure.md` freezes the 1.0 in-function suspension,
  async consumer, PAL state, and source/TKI dependency boundary.
- `fz2_semantic_tki_closure.md` closes the high-risk PAL/ownership/effects/
  async combinations, removed-syntax revalidation, and same-version interface
  replay matrix.
- `permission_flow_two_mode_rfc.md` proposes the next ownership-flow layer:
  independent transfer versus shared propagation, without weakening
  declaration-backed authority.
- `permission_flow_two_mode_audit.md` records the implementation gap between
  that proposed flow model and the current compiler.
- `scoped_borrowed_task_rfc.md` proposes a post-1.0 lexical task-scope model
  for borrowed children without weakening the detached execution boundary or
  exposing user-written lifetime syntax.
- `owned_lazy_iterator_rfc.md` records the implemented first post-1.0 owned
  lazy-adapter slice (`Map<I,F>` over a consuming source); borrowed/lending
  adapters remain explicitly deferred.
- `dynamic_borrowing_exploration.md` records the evidence required before a
  single-thread, runtime-checked borrowing container can become an RFC; it is
  not a current Toka gap or implementation commitment.
- [`../droptime_spec.md`](../droptime_spec.md) is the post-1.0 `droptime`
  resource-cleanup specification/RFC: it freezes a local, raw-handle-anchored
  cleanup contract while retaining the 1.0 non-unwinding panic boundary.

## Rule Status

Rules use one of these phase labels:

- `Core guarantee`: required for Toka 1.0 safety and source/interface
  consistency.
- `Conservative rejection`: a pattern may be safe in principle, but Toka 1.0
  rejects it to keep the safety proof local and explainable.
- `Post-1.0 precision`: an accepted future extension that must not weaken any
  1.0 guarantee.
- `Syntax exclusion`: syntax intentionally outside the 1.0 surface.

## Maintenance Rule

Every change to PAL, ownership transfer, escaping effects, async/task capture,
or TKI export/import should update this directory when it changes any of:

- accepted source forms,
- rejected source forms,
- diagnostic identity,
- `.tki` replay behavior,
- cache invalidation behavior,
- or test coverage.
