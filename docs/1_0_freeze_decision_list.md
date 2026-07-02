# Toka 1.0 Freeze Decision List

This document records the current freeze direction for Toka 1.0. It is not a
new language proposal. Its purpose is to separate the public syntax and
semantics that should be stabilized for 1.0 from work that can safely move to a
later release.

## Freeze For 1.0

- Payload / handle separation: bare names access payload; hatted forms access
  handle identity.
- Hat semantics: `&`, `*`, `^`, and `~` keep their current meanings.
- `#` placement rules: `^#p` means handle rebinding; `^p#` means payload-side
  mutability.
- `cede` as an explicit transfer contract: both caller and callee must honor
  the transfer path.
- Hatted parameter contract: unused handle views remain warning-level
  diagnostics; redundant `&param` remains an error.
- Trait syntax: `trait @Name`, `Type@Trait`, `@{A, B}` facet sets, and `where:`
  constraints.
- Associated types: keep `type` and `per type` as the stable 1.0 model.
- `dyn @Trait`: single-facet trait objects only.
- `@encap`: keep the current visibility rules, including `pub`, `pub(crate)`,
  `pub(path)`, and wildcard forms.
- Closure capture rules: explicit `cede` / `copy`, with resource copy capture
  rejected.
- Public unsafe/raw API redlines: raw pointer exposure requires explicit
  unsafe/raw naming.
- TKI replay baseline: associated types, `pub import`, `dyn @Trait`, generic
  `where:`, and `@encap` visibility must remain source-less compatible.

## Postpone After 1.0

- True module-identity semantics for `pub(path)`.
- Multi-facet trait objects such as `dyn @{A, B}`.
- Associated type binding syntax for `dyn @Trait`.
- Object lifetime / ownership annotations for dyn objects.
- Full match exhaustiveness checking.
- More aggressive PAL acceptance for hard-to-prove but possibly safe cases.
- Upgrading hatted parameter unused-handle warnings into hard errors.
- Larger iterator / async trait formalization.
- Further build-cache performance redesign unless required by correctness.

## Must Fix Before 1.0

- Any compiler crash on valid or invalid Toka source.
- Any known miscompile or memory ownership violation in already-public syntax.
- Any source / TKI semantic divergence for frozen syntax.
- Any platform build failure in supported release targets.
- Any contradiction between `docs/syntax.md`, `docs/syntax_zh.md`, and actual
  compiler behavior.
- Any `cede`, drop, clone, or PAL bug that allows resource duplication,
  use-after-move, or invalid escape.
- Any diagnostic that makes a frozen rule practically impossible to understand
  in common usage.

## Guiding Principle

Toka 1.0 should freeze the language's core contract, not exhaust every future
expressiveness path. The release should guarantee that today's public syntax is
coherent, documented, test-locked, and safe under the current compiler model.
New expressive power can come after 1.0, but existing semantics must not remain
ambiguous.
