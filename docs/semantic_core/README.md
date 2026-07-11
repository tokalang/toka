# Toka Semantic Core

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
