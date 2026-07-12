# Toka 1.0 Freeze Decision List

Execution status, blockers, and final release stop conditions are tracked in
[`1_0_closure_plan.md`](1_0_closure_plan.md). This document remains the
normative record of 1.0 scope and exclusions.

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
- `$` placement rules: `$` is the explicit read-only / blocked marker. It is
  omitted for ordinary read-only locals and parameters, where it would be
  redundant. It is meaningful where it blocks permission inheritance, such as
  `field$` for payload inheritance and `^$p` / `*$p` for handle-identity
  rebinding inheritance.
- Permission inheritance boundary: writable object payload access flows into
  ordinary fields, but handle fields stop payload inheritance at the handle
  layer. A writable parent can authorize handle rebinding, while the pointee
  payload remains read-only unless the field or binding explicitly carries a
  payload-side `#`, such as `^p#` or `*p#`.
- `cede` as an explicit transfer contract: both caller and callee must honor
  the transfer path.
- Hatted parameter contract: unused handle views remain warning-level
  diagnostics; redundant `&param` remains an error.
- Trait syntax: `trait @Name`, `Type@Trait`, `@{A, B}` facet sets, and `where:`
  constraints.
- Prelude trait visibility: `@encap`, `@Send`, and `@Sync` are implicitly
  visible through the standard prelude. Every other trait name follows normal
  lexical import rules and must be declared locally or explicitly imported.
- Associated types: keep `type` and `per type` as the stable 1.0 model.
- Iterator protocol: non-array `for` is trait-based. `@Iterable::iter` returns
  an associated `Iter` with `return <- self`; value iteration uses
  `@Iterator::Item`, while reference morphology additionally selects
  `@BorrowIterator::BorrowedItem`. The source remains PAL-borrowed for the
  cursor lifetime, and the hidden cursor follows ordinary deterministic drop.
  These traits remain ordinary explicitly imported traits, not implicit
  prelude names.
- `dyn @Trait`: single-facet trait objects only. Associated-type binding syntax
  is not part of 1.0, so traits with associated types are not object-safe as
  `dyn @Trait`, and forms such as `dyn @Readable<Item = i32>` are rejected.
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
- Match / pattern safety: freeze enum exhaustiveness, variant payload
  shape-checking, guard handling, or-pattern binding consistency, wildcard /
  `default` fallback, and resource-safe destructuring. Guarded arms refine a
  case but do not count as exhaustive. Non-enum matches require an unguarded
  wildcard, `default`, or unconditional variable arm rather than relying on
  value-domain proof over literals, ranges, or strings.
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
- Shape structural facts: a shape definition is part of the compiler-visible
  type contract. `.tki` interfaces must preserve the complete structure needed
  for semantic checking, including private fields, field morphology,
  mutability, nullability, layout-relevant attributes, and borrow-like member
  types. Visibility controls user access, not compiler knowledge.
- Shape header dependencies such as `shape Ref <- field` are removed from the
  1.0 public surface. Borrow-like fields carry dependency facts directly during
  construction and assignment; escaping borrowed values still use function
  signature dependencies or `effects:` routing.
- Shape-internal member dependencies such as `&view: T <- owner` are not part of
  the 1.0 surface and are rejected as unsupported. They express a stronger
  self-referential relation and require stable-placement / immovable
  construction semantics. 1.0 code should expose borrowed views through
  functions with signature dependencies or `effects:` return-member routing
  instead.
- Structural return dependencies: `effects:` may route dependencies to
  returned members, such as `return.left <- a` and `return.right <- b`.
  The callee must prove each returned member carries only the dependency
  sources declared for that member; a whole-return dependency check is not
  enough to justify field-swapped returns.
- PAL analysis scope: local control-flow analysis may be used inside a
  function, but calls do not require inspecting the callee body. Call sites
  consume the callee signature, and callees must validate that their bodies
  honor the dependencies declared in the signature.
- PAL call-site model: a function call is checked as a simultaneous temporary
  borrow group. Payload arguments are implicit PAL borrows, not invisible
  value copies: `x: T` is a shared payload borrow, `x#: T` is an exclusive
  payload borrow, and `cede x` is an invalidating transfer. Overlapping
  arguments conflict if any one of them requires exclusive access or transfer.
- PAL path model: disjoint field borrows are allowed, and overlapping path
  prefixes are the unit that PAL uses to check possible conflicts. Moving or
  `cede`-ing a value with an active borrow is an error. If a hard-to-prove case
  cannot be verified locally, 1.0 should reject it rather than weaken the
  safety contract.
- PAL operation classes: PAL distinguishes ordinary payload writes, shared
  payload borrows, exclusive payload borrows, handle-view borrows, exclusive
  mutations, and invalidating transfers. A shared borrow is a read-only view of
  that borrow path, not a global freeze promise for every ordinary payload
  write on the original storage; invalidating replacement of a parent path
  remains rejected.
- Interior mutability boundary: payload-side `#` can express local interior
  mutability, but it is not a thread-safety proof. Cross-thread sharing must be
  mediated by appropriate library types and trait bounds such as
  `Atomic`/`Mutex`/`RwLock`, `Send`, and `Sync`.
- Execution-boundary capture rule: thread / task handoff must not carry hidden
  borrowed state. Closures passed to `thread_spawn` cannot implicitly capture
  outer variables; state crossing such a boundary must be explicit through
  `[cede ...]` transfer or `[copy ...]` for copyable data. `.start` uses the
  same conservative boundary: only non-borrowing scalar arguments or values
  transferred through both a `cede` parameter and a `cede` call argument may
  cross it. Ordinary shapes remain logical in-place captures even when they
  are copyable, so `.start` does not copy them implicitly.
- Source-call versus ABI boundary: ordinary parameters have the frozen logical
  in-place capture semantics described by PAL. The compiler may lower scalars,
  aggregates, handles, closures, and return values differently for a target
  ABI, but that physical layout is not a source-level copy/borrow rule and is
  not a stable binary ABI commitment.
- Core runtime failure boundary: normal scope exits perform deterministic
  cleanup for live owned values. `panic` is non-returning process termination,
  not a catchable exception or unwinding guarantee; cleanup after panic is not
  part of the 1.0 contract.
- Public unsafe/raw API redlines: raw pointer exposure requires explicit
  unsafe/raw naming.
- TKI replay baseline: associated types, `pub import`, `dyn @Trait`, generic
  `where:`, and `@encap` visibility must remain source-less compatible.

## Late / Post-1.0 Tracking

These items do not block the 1.0 syntax freeze. They record the extension
surface that should remain conservative in 1.0 and can be refined after the
core contract ships.

- `dyn @Trait` boundary: the 1.0 surface is single-facet `dyn @Trait` without
  associated-type binding. Future work may specify multi-facet objects,
  associated-type binding, object lifetime / ownership forms, and dyn object
  ABI details.
- Async color model: keep `fn f() -> async T`, `.await`, `.wait`, and `.start`
  as the current stable direction. Async return and dependency annotations
  remain orthogonal: `fn f(x: str) -> async str <- x` means the eventual `str`
  depends on `x`. `.await` requires an async function, `.wait` is rejected
  inside an async function, and suspension preserves frame-local init, move,
  and PAL state instead of creating a semantic reset point. Richer async
  ownership, cancellation, and task-handoff syntax belongs after 1.0.
- Threading / task ownership model: the 1.0 safety contract is conservative:
  thread / task handoff must not carry hidden borrowed state, and crossing
  state must be explicit through `cede`, `copy`, or library types with
  appropriate `Send` / `Sync`-style bounds. More expressive structured
  concurrency can build on this boundary later.
- Consuming iteration, async iteration, and a larger combinator library remain
  post-1.0 work; they must extend the frozen synchronous iterator facets.
- Shape-internal member dependency model: future work may specify syntax such as
  `self.view <- self.owner`, but only together with construction, assignment,
  move, clone, drop, container-storage, and `.tki` rules for stable internal
  borrows.

## Postpone After 1.0

- Full resolver-normalized package identity for `pub(path)`, including package
  roots, library roots, and cross-package path normalization. This should refine
  the import/path model rather than introduce a source-level `mod` concept.
- Multi-facet trait objects such as `dyn @{A, B}`.
- Associated type binding syntax for `dyn @Trait`.
- Object lifetime / ownership annotations for dyn objects.
- Full value-domain exhaustiveness checking for non-enum literal, range, and
  string patterns.
- More aggressive PAL acceptance for hard-to-prove but possibly safe cases.
- Shape-internal member dependencies and stable-placement / immovable shape
  construction.
- Private-helper inference for missing borrow dependency annotations.
- Upgrading hatted parameter unused-handle warnings into hard errors.
- Consuming iterators, async iterators, and larger iterator combinator
  formalization.
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
- Shape-internal member dependencies such as `self.view <- self.owner` remain
  outside the 1.0 surface until their immovable / stable-placement construction
  semantics are fully specified. The removed shape header dependency syntax must
  not be used as a substitute for this stronger relation.
- Any diagnostic that makes a frozen rule practically impossible to understand
  in common usage.

## 1.x Compatibility Policy

- A valid frozen 1.0 source program keeps its source-level meaning throughout
  1.x. Additive syntax and conservative-analysis improvements may be introduced
  when they do not reinterpret frozen code.
- A memory-safety or miscompile fix may reject previously accepted source that
  depended on unsound behavior. The change must be recorded as a safety fix.
- Published diagnostic codes are not reused for a different rule in 1.x.
  Wording and source highlighting may improve while preserving rule identity.
- `.tki`, build-cache data, generated object layout, and binary ABI remain tied
  to compiler and format versions. They are not cross-version compatibility
  promises.

## Guiding Principle

Toka 1.0 should freeze the language's core contract, not exhaust every future
expressiveness path. The release should guarantee that today's public syntax is
coherent, documented, test-locked, and safe under the current compiler model.
New expressive power can come after 1.0, but existing semantics must not remain
ambiguous.
