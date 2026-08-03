# Toka Semantic Core

[`../1_0_closure_plan.md`](../1_0_closure_plan.md) is the frozen historical
ledger for its recorded 1.0/RC work and revision; its `Complete` labels are not
current-HEAD requalification. The active current-HEAD P-1 status, blockers, and
post-1.0 dependency order are tracked by
[`semantic_contract_evolution_roadmap_rfc.md`](semantic_contract_evolution_roadmap_rfc.md).
This directory provides the semantic contracts and dated evidence used by both.

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

The frozen 1.0 portion is an audit layer over the current compiler,
`docs/syntax.md`, and `docs/1_0_freeze_decision_list.md`. Explicitly marked
post-1.0 RFCs and planning records also live here; their presence does not
change current language behavior or implementation status.

## Documents

- `rule_template.md` defines the fields every semantic rule should carry.
- `rule_matrix.md` records the frozen 1.0 rule matrix and its recorded or
  qualification-target diagnostics, implementation areas, positive tests,
  negative tests, and gaps; it is not current-HEAD evidence by itself.
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
- [`../cede_obligation_evidence_v1.md`](../cede_obligation_evidence_v1.md)
  freezes the narrower, repair-oriented protocol for caller transfer, callee
  consumption, and `cede` return obligations.
- [`../taskhandle_lifecycle_v1.md`](../taskhandle_lifecycle_v1.md) freezes the
  versioned TaskHandle operation, resource, result-consumption, and redline
  test contract used by async repair tooling.
- [`../async_runtime_tcb_rfc.md`](../async_runtime_tcb_rfc.md) is the normative
  async TCB, wait-token, cancellation, result-discharge, and frame-reclamation
  design contract; implementation conformance remains gated at the current
  revision. [`../async_runtime_p5_spec.md`](../async_runtime_p5_spec.md) is its
  subordinate Phase 5 implementation/closure record.
- [`../capability_pilot_v1.md`](../capability_pilot_v1.md) freezes the narrow
  H/P call-argument explanation pilot and its explicit non-goals.
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
- `permission_flow_two_mode_rfc.md` freezes the bounded ownership-flow design:
  independent whole transfer versus non-amplifying shared propagation. Its
  historical `Partial` surfaces still require current-revision qualification,
  and every unlisted generalization remains fail-closed.
- `permission_flow_two_mode_audit.md` records the historical implementation
  evidence and the remaining generalization gaps; current-HEAD qualification
  is controlled by the semantic evolution roadmap.
- `partial_cede_lifecycle_rfc.md` freezes the bounded direct-record-field and
  fixed-array constant-index design matrix. A legacy mask slice exists, but
  conformance to the proposed PlaceState Core and the separate async bridge
  remains gated; this is not a general projection guarantee.
- `scoped_borrowed_task_rfc.md` proposes a post-1.0 lexical task-scope model
  for borrowed children without weakening the detached execution boundary or
  exposing user-written lifetime syntax.
- `owned_lazy_iterator_rfc.md` records the implemented first post-1.0 owned
  lazy-adapter slice (`Map<I,F>` over a consuming source); borrowed/lending
  adapters remain explicitly deferred.
- `encap_current_contract.md` is the normative semantic entry point for the
  implemented `@Encap` boundary: explicit governance, exact field grants,
  verified Copy, explicit Dup, compiler-owned cleanup, and source-less TKI
  agreement.
- `encap_hybrid_policy_rfc.md` is an archived pre-clean-break design record;
  it includes removed scoped/wildcard grant forms and is not a current
  decision source.
- `semantic_contract_evolution_roadmap_rfc.md` orders the next semantic work:
  P-1 baseline qualification precedes the shared PlaceState Core, bounded
  permission/partial-cede closure, `init`, Outcome Contracts, async task and
  place-cleanup closure, and a later provenance-bound semantic manifest
  payload.
- `place_state_core_rfc.md` proposes the candidate internal production-language
  construction-origin/availability facts, exact-place transitions, cleanup
  correspondence, and bounded fail-closed gates. It is not a user-visible
  general typestate system or the external research calculus.
- `outcome_contract_rfc.md` proposes the first branch-indexed postcondition
  slice: one synchronous whole-place construction formal, direct nominal result
  variants, an immediately consumed latent witness, linear `Init` authority,
  cleanup conservation, and two source-less completion levels.
- `semantic_manifest_envelope_rfc.md` proposes only stable identity, a canonical
  envelope, record criticality, trust classes, accepted producer provenance,
  exact-object binding, and fail-closed validation. Its language payload
  remains deferred until PlaceState, `init`, and `OutcomeTransition` are stable.
- `encap_slice0_baseline.md` records the legacy resolver, lifecycle, and TKI
  facts against which that blocking, non-semantic Slice 0 is audited.
- `encap_slice0_go_no_go.md` records the completed Slice 0 evidence gate and
  the Go to Slice 1 preparation; semantic activation remains disabled.
- `encap_slice0_redline_results.md` records the reproducible Slice 0
  access/identity, source/TKI/cache, lifecycle/resource, Copy/Dup, and legacy
  grant-inventory results for the proposed epoch.
- `encap_slice1_data_model.md` records the completed audit-only fact maps,
  generic marker bookkeeping, Dup candidate classification, and dyn-marker
  exclusion that prepare the epoch's later semantic slices.
- `typed_todo_rfc.md` freezes the adopted expression-only typed-todo boundary:
  complete expected contracts, zero authority, conditional facts, and the
  no-CodeGen/TKI/cache publication rule.
- `init_contract_rfc.md` separates the implemented `uninit` source state from
  the frozen but not activated explicit initialization-obligation design:
  two-sided `init` parameters, lexical proof scopes, and separation from raw
  `Uninit<T>` storage.
- `unit_void_never_rfc.md` freezes the separation of ordinary Unit, ABI/raw
  `void`, and explicit non-completion (`never`), including its initial narrow
  implementation surface.
- [`../typed_todo_goals_v1.md`](../typed_todo_goals_v1.md) freezes the
  independent requirement-only JSON protocol for incomplete typed-todo edits.
- [`../conditional_facts_v1.md`](../conditional_facts_v1.md) defines the
  separate, conservative editor-only binding facts for symbols conditional on
  a typed-todo requirement.
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
