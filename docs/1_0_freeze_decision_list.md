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
- Path-scoped visibility: Toka has no source-level `mod` declaration. The path
  in `pub(path)` uses the same module-location path grammar as the left side of
  an `import`, before item selection. It is interpreted by the import resolver,
  not by raw substring matching or a Rust-style module tree.
- Hyphen boundary: kebab-case is allowed only in filesystem-oriented
  module-location paths, including `.tk` file names. Names created inside `.tk`
  source and entering Toka's semantic namespace, including import aliases,
  fields, declarations, and selectable namespaces, must not use `-`. Binary `-`
  remains an operator and must be surrounded by spaces.
- Closure capture rules: explicit `cede` / `copy`, with resource copy capture
  rejected.
- PAL terminology: PAL is frozen as **Path-Anchored Ledger** (路径锚定账本),
  not the earlier placeholder "Provenance, Alias, and Lifetime".
- PAL boundary: PAL is the safe-borrow and resource-contract layer for Toka borrow semantics,
  including implicit parameter capture, `&` borrow handles, and borrowed views
  such as `str` and `bytes`. Raw pointers remain in the unsafe / FFI layer and
  are not part of PAL's safe-borrow guarantee. Ownership / sharing handles such
  as `^` and `~` are not borrow-like dependencies by themselves, though shapes
  stored behind them may still contain borrowed fields.
- Escaping borrow dependencies: for 1.0, any borrow-like value that crosses a
  function boundary must have an explicit dependency annotation in the
  signature. This applies uniformly to private and public functions; later
  releases may infer private body-visible helpers as an ergonomics improvement.
- PAL analysis scope: local control-flow analysis may be used inside a
  function, but calls do not require inspecting the callee body. Call sites
  consume the callee signature, and callees must validate that their bodies
  honor the dependencies declared in the signature.
- PAL path model: disjoint field borrows are allowed, and overlapping path
  prefixes are the unit that PAL uses to check possible conflicts. Moving or
  `cede`-ing a value with an active borrow is an error. If a hard-to-prove case
  cannot be verified locally, 1.0 should reject it rather than weaken the
  safety contract.
- Interior mutability boundary: payload-side `#` can express local interior
  mutability, but it is not a thread-safety proof. Cross-thread sharing must be
  mediated by appropriate library types and trait bounds such as
  `Atomic`/`Mutex`/`RwLock`, `Send`, and `Sync`.
- Public unsafe/raw API redlines: raw pointer exposure requires explicit
  unsafe/raw naming.
- TKI replay baseline: associated types, `pub import`, `dyn @Trait`, generic
  `where:`, and `@encap` visibility must remain source-less compatible.

## Under Discussion / In Progress

- `dyn @Trait` boundary: keep the current single-facet object model stable, and
  decide how explicitly to document the future space around multi-facet
  objects, associated type binding, object safety, and dyn object ABI.
- PAL implementation work: strengthen the current path-anchored checker into a
  local CFG-based analysis without crossing function bodies.
- Shared immutable borrow rule: the intended Toka rule is that an immutable
  borrow is a read-only capability of that borrow, not a global freeze promise
  for the borrowed storage. The compiler must still preserve borrow validity:
  move, `cede`, drop, handle rebinding, reallocation, exclusive mutation, and
  any operation that can invalidate the borrow remain conflicts. Current PAL is
  stricter than this rule: overlapping payload writes also conflict with
  `BorrowedShared` because `verifyMutation` does not yet distinguish ordinary
  payload writes from invalidating or exclusive mutations. TODO: split these
  mutation classes before freezing the final 1.0 PAL behavior.
- Async color model: keep `fn f() -> async T`, `.await`, `.wait`, and `.start`
  as the current direction, but freeze this late because async crosses value
  color, task ownership, suspension, cancellation, and PAL dependency
  propagation. Async return and dependency annotations remain orthogonal:
  `fn f(x: str) -> async str <- x` means the eventual `str` depends on `x`.
- Threading / task ownership model: keep thread and task primitives library-led
  where possible, but freeze the safety contract late because it interacts with
  `Send` / `Sync`, `cede`, detached tasks, cancellation, and borrowed state
  crossing execution boundaries.
- Match exhaustiveness: decide whether full exhaustiveness checking is required
  before 1.0, or whether current safe rejection / safe execution behavior is
  enough for the first stable release.
- Iterator / async trait formalization: decide how much of the current library
  and compiler convention should be documented as stable language contract
  before 1.0.

## Postpone After 1.0

- Full resolver-normalized package identity for `pub(path)`, including package
  roots, library roots, and cross-package path normalization. This should refine
  the import/path model rather than introduce a source-level `mod` concept.
- Multi-facet trait objects such as `dyn @{A, B}`.
- Associated type binding syntax for `dyn @Trait`.
- Object lifetime / ownership annotations for dyn objects.
- Full match exhaustiveness checking.
- More aggressive PAL acceptance for hard-to-prove but possibly safe cases.
- Private-helper inference for missing borrow dependency annotations.
- Upgrading hatted parameter unused-handle warnings into hard errors.
- Larger iterator / async trait formalization.
- Async blocks, parameterized `.start(...)`, and richer structured-concurrency
  syntax. These should extend the frozen async color model rather than replace
  it.
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
