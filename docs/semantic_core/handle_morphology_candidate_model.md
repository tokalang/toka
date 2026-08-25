# Handle Morphology Candidate Model

**Status:** Candidate design record; non-normative and not frozen for Toka 1.0

**Purpose:** Guide the next Handle Grammar design review and implementation work

**Supersedes:** Nothing. It records a candidate model and identifies parts of the
current Phase 2 implementation that remain under review.

The staged execution plan is maintained in
[Morphology Constraints and Borrow API Migration Plan](morphology_constraints_and_borrow_api_plan.md).

## 1. Decision boundary

The hat model is not declared successful or failed by this document. The
semantic distinction between a payload and its handle remains valuable, but
the surface language may be frozen only after the model below is shown to be
compositional across parameters, returns, aliases, generics, TKI, and lowering.

Until that review is complete:

- do not describe the current Handle Grammar surface as the final Toka 1.0
  syntax;
- do not add new context-specific exceptions merely to make a fixture compile;
- preserve existing implementation evidence, but treat it as migration
  evidence rather than proof that the surface design is final.

## 2. Three orthogonal layers

Every source construct must lower through three distinct concepts.

### 2.1 `SoulType`

`SoulType` is the nominal or structural payload type, such as `i32`, `Node`,
`Option<T>`, or `Vec<T>`. It does not grant access to a handle identity.

### 2.2 `HandleType<SoulType>`

A handle chain describes the physical handle value around a soul. The globally
admitted chains are currently:

```text
Managed = Soul | ^Soul | ~Soul | &Soul | &^Soul | &~Soul | &&Soul
Raw     = *Soul | **Soul | ***Soul | ...
```

Raw nullability remains an attribute of an individual raw layer. Managed/raw
mixing within one continuous chain remains illegal. A non-pointer structural
node terminates a continuous chain, so every child starts an independent
classification:

```toka
Option<&^Node>   // Option boundary; inner &^Node is independently legal
Option<*&Node>   // Option boundary; inner *&Node is independently illegal
```

### 2.3 `BindingMode` and PAL effects

Binding mode describes how a source name exposes or may mutate a place. PAL
records capture, alias, loan, invalidation, and lifetime facts for an operation.
Neither one implicitly constructs another handle layer.

In particular, a function call's logical in-place capture is a PAL relation;
it is not a type constructor. Passing an `&^T` value through a captured
parameter does not change its type to `&&^T`.

Hats select a handle view or identity; by themselves they do not copy or
transfer ownership. A selected unique handle nevertheless has affine value
semantics: using `^source` to initialize, assign, or return a unique value is
an intrinsic direct move and omits `cede`. This move belongs to the surrounding
value context, not to every occurrence of `^source`:

```toka
fn observe(^x: Node)          // non-transferring unique-handle view
fn take(cede ^x: Node)        // explicit ownership-transfer contract
auto ^next = ^owner           // direct unique value move
return ^next                  // direct unique value return
```

The caller of a cede parameter still writes `cede ^owner`; the callee can
discharge that contract with `auto ^owned = ^x` or `return ^x`. The design
review must retain the separation among handle selection, affine value use,
and cede call/capture contracts in Sema, TKI, diagnostics, and CodeGen.

### 2.4 View selection and target-aware borrowing

`&` is not an unqualified textual prefix over the physical type of a binding.
It borrows the view selected by its operand spelling:

```text
bare operand          selects the Soul/payload view
hatted operand        selects that Handle identity view
```

The candidate desugaring is therefore target-aware:

```text
borrow(payload(^T)) = &T
borrow(handle(^T))  = &^T
borrow(handle(~T))  = &~T
borrow(handle(&T))  = &&T
```

In source terms:

```toka
u          // payload selected through a unique owner
^u         // unique handle identity selected
&u         // borrow the payload; result is &T
&^u        // borrow the unique handle identity; result is &^T
cede ^u    // transfer the unique handle
```

Borrowing the payload keeps the owner alive and blocks conflicting mutation,
rebinding, drop, or `cede` for the loan lifetime. It does not borrow or
transfer the owner identity. Borrowing the handle identity is a distinct,
explicit operation and is expected to be substantially rarer.

This rule is a source semantic invariant, not an optional coercion selected by
the expected type. Sema and PAL must preserve the selected target before type
composition and lowering.

Formal parameters are a separate binding boundary. A non-`cede` parameter
already captures/aliases its argument without transfer, so adding another
borrow layer to the formal root is redundant and prohibited:

```toka
fn observe(^x: Node)       // admitted handle capture
fn take(cede ^x: Node)     // admitted transfer
fn bad(&^x: Node)          // rejected redundant level-2 formal root
```

The first-class `&^Node` value remains legal in locals, returns, and structural
children; it simply is not spelled as a formal parameter root.

## 3. Surface placement

Where a declaration has a binding name, morphology is written on that name:

```toka
^x: Node
&^x: Node
'x: T
```

Where no binding name exists, morphology remains part of type syntax:

```toka
-> &^Node
Option<&^Node>
fn(&^Node) -> i32
sizeof(&^Node)
```

A transparent alias may not hide a root handle:

```toka
alias UniqueNode = ^Node          // rejected: root hat is hidden
alias BorrowedNode = &^Node       // rejected: root chain is hidden
alias MaybeUnique = Option<^Node> // admitted alias boundary
alias BorrowList = Vec<&Node>     // admitted alias boundary
```

Morphology below a structural boundary is not the morphology of the alias
root. It remains independently subject to Handle Grammar validation.

## 4. Rigid and morphic generic variables

### 4.1 Rigid `T`

A rigid generic parameter ranges over soul types. Its root morphology is known
to be empty.

### 4.2 Morphic `'T`

A morphic generic parameter preserves an unknown admitted morphology. It must
not be modeled as one particular concrete hat.

Two separate facts are required:

```text
MorphologySlotCost('T) = 1
MorphologyDomain('T)   = Unknown admitted handle morphology
```

The slot cost is a conservative composition budget. It is not a statement
that every substitution has physical depth exactly one. A substitution may be
a soul, a level-1 handle, a legal level-2 borrow, or a raw chain if the generic
domain permits it.

At binding positions the quote belongs to the name:

```toka
fn identity<'T>('x: T) -> 'T {
    return 'x
}
```

At pure type positions it belongs to the type expression:

```toka
Option<'T>
-> 'T
sizeof('T)
```

## 5. Morphology composition constraints

Applying a concrete hat to a morphic variable creates a domain obligation. It
is not valid merely because the textual slot count is small.

### 5.1 Borrow extension

`&` is the only managed hat that may extend an admitted level-1 managed chain
to level 2:

```text
BorrowExtendable('T) := 'T in { Soul, ^Soul, ~Soul, &Soul }
```

`&'T` explicitly selects and borrows the abstract morphic identity represented
by `'T`. It is a valid generic expression only with a visible
`BorrowExtendable` obligation:

```text
Soul   -> &Soul
^Soul  -> &^Soul
~Soul  -> &~Soul
&Soul  -> &&Soul
*Soul  -> &*Soul   (rejected by the obligation)
&^Soul -> &&^Soul  (rejected by the obligation)
&&Soul -> &&&Soul  (rejected by the obligation)
```

Concrete binding-side forms remain candidates outside formal-parameter roots:

```toka
auto &^unique_handle = &^unique
auto &~shared_handle = &~shared
auto &&reference_handle = &&reference
```

Expression and type positions must preserve the view distinction: `&x` borrows
the payload view, while `&^x`, `&~x`, or `&'x` borrow the explicitly selected
handle identity. A function boundary receives the required level-1 handle view
through its ordinary capture contract instead of adding a level-2 formal root.

For a non-binding expression whose concrete morphology is abstract, the
parenthesized selector is explicit:

```toka
&items[index]       // borrow Soul(items[index])
&'(items[index])    // borrow the abstract `'T` handle identity
```

`BorrowIterator` uses the second form because `Vec<&T>` must yield `&&T`.
Ordinary container `borrow`/`borrow_mut` use the first form and yield `&T`.
This selector does not itself prove that every substitution is legal; a public
generic `&'T` signature still requires the planned `BorrowExtendable` bound.

The present implementation qualifies the selector in the iterator projection
path. General first-class local binding and direct function-return ABI coverage
for `&^T`, `&~T`, and `&&T` remains a separate lowering milestone and must not
be inferred from iterator success.

### 5.2 Other prefixes

Without a narrower declared domain:

- `^'T` and `~'T` are not universally valid; only a soul substitution works,
  for which a rigid `T` should normally be used;
- `*'T` requires a `RawExtendable`-like obligation whose domain contains only
  a soul or a compatible raw chain;
- `&&'T` is not universally valid and should not be admitted merely by
  counting tokens.

The final spelling of morphology constraints is open. Regardless of spelling,
the constraint must be represented in the semantic signature, serialized in
TKI, visible to reflection/tooling, and diagnosed at an unsatisfied call or
instantiation.

## 6. Generic definition closure

A generic declaration must satisfy one of two conditions:

1. its signature and body are valid for every substitution in the declared
   morphology domain; or
2. the declaration carries an explicit, inspectable morphology constraint that
   narrows that domain.

Ordinary API members must not silently disappear after substitution. An
unsatisfied morphology constraint is a normal, named constraint failure, not
SFINAE.

For example:

```toka
fn raw_identity<'T>() -> *'T
```

is not universally valid because substituting `&Node` or `^Node` forms an
illegal mixed chain. The declaration must be rejected, rewritten, or given an
explicit compatible-raw constraint.

The current `Vec<'T>::get_ref() -> *'T` and `unsafe_get() -> *'T` cases are the
canonical migration examples. Their substitution-time filtering is useful
evidence that LLVM lowering is fail-closed, but it is not the desired final
language behavior. These methods must become universally valid, explicitly
constrained, or moved to a constrained implementation surface.

## 7. Audit consequences

During migration, `RejectedSFINAE` evidence proves only that an illegal
substitution was not instantiated or lowered. It must not remain a positive
Toka 1.0 language target.

The intended terminal audit distinction is:

```text
Implicit morphology SFINAE       = 0
Explicit constrained exclusions  = recorded and inspectable
Illegal instantiated morphology  = 0
Illegal LLVM-lowered morphology  = 0
```

Audit events for an explicit constraint must identify the constraint and the
substitution that failed it. They must not reuse the meaning of an invisible
candidate disappearance.

## 8. Required qualification matrix

Before this candidate can become normative, tests must prove all of the
following.

### 8.1 Concrete composition

```toka
fn observe_unique(^x: Node)
fn observe_shared(~x: Node)
fn observe_reference(&x: Node)
```

Parameter capture must preserve the declared level-1 handle view without adding
a handle layer. Level-2 formal roots must be rejected as redundant. `cede`
tests must independently prove that no hat implies an ownership transfer.

View-selection tests must independently prove:

```toka
auto ^u = new Node(...)
auto &payload = &u          // resolved type &Node
auto &^owner = &^u          // resolved type &^Node

auto ~s = new Node(...)
auto &shared_payload = &s   // resolved type &Node
auto &~shared_owner = &~s   // resolved type &~Node
```

The tests must inspect resolved semantic types; equivalent payload reads are
not sufficient evidence.

### 8.2 Morphic composition

For a non-parameter type or expression using `&'T`, substitutions with `Soul`,
`^Soul`, `~Soul`, and `&Soul` must pass. Raw and already-level-2 substitutions
must produce a direct morphology-constraint diagnostic and must not be
filtered by SFINAE. A formal parameter must use the captured morphic binding
without adding the outer `&`.

### 8.3 Boundaries and aliases

Root-hatted aliases must fail, structural aliases containing admitted child
morphologies must pass, and every child chain must still be independently
validated.

### 8.4 Source/TKI equivalence

Binding-side morphology, morphology constraints, generic domains, and
diagnostics must survive source-less interface replay without weakening or
reinterpretation.

### 8.5 Standard-library API closure

Representative containers must expose the same documented member set for all
substitutions in their declared domain. Any conditional member must display an
explicit morphology constraint in source, TKI, semantic evidence, and
diagnostics.

### 8.6 Borrowing API naming convention

The safe standard-library borrowing surface uses one capability-oriented verb
family:

```toka
borrow(index)          -> &'T
borrow_mut(index)      -> &'T#
try_borrow(index)      -> Option<&'T>
try_borrow_mut(index)  -> Option<&'T#>
```

The method name does not create the loan by itself; the reference return and
its dependency contract remain the semantic source of truth. The name makes
that operation visible and consistent at the API surface.

Other access families remain disjoint:

```toka
dup(index)         -> 'T
try_dup(index)     -> Option<'T>
unsafe_get(index)  -> *'T
```

Consequently:

- a safe method named `get_ref` must not return a raw pointer;
- the legacy `Vec::get_ref() -> *'T` surface is migration debt and must be
  removed rather than redefined silently;
- ordinary legacy `get_ref` callers migrate to `borrow`/`try_borrow`;
- callers that genuinely require a raw address migrate to `unsafe_get`;
- owned duplication currently spelled `get`/`get_opt` migrates to
  `dup`/`try_dup`;
- temporary compatibility aliases may exist during migration, but the final
  public surface contains only one spelling for each capability.

## 9. Open decisions

The following questions must be resolved before a new Phase 2 baseline tag:

1. What is the source spelling of `BorrowExtendable`, `RawExtendable`, and any
   other morphology-domain constraints?
2. Are constraints always explicit, or may private declarations infer them
   while public declarations must spell them?
3. Does the default morphic domain include arbitrary raw depth, and how are
   per-layer raw nullability constraints represented?
4. Which escape, storage, forwarding, and rebinding operations are admitted
   for a first-class local or returned `&^T` value? Formal parameters use the
   existing capture contract rather than a separate `&^x: T` root.
5. How are constrained members represented by reflection and method lookup
   without SFINAE disappearance?
6. Which existing standard-library signatures must be rewritten or moved to a
   constrained implementation?

## 10. Survival criteria for the hat surface

The current surface remains viable only if the completed design provides:

- one structural desugaring into `SoulType`, `HandleType`, `BindingMode`, and
  PAL effects;
- intrinsic direct move for unique value contexts, with `cede` reserved for
  explicit consuming parameter/capture contracts and other non-value transfer
  boundaries;
- generic substitution closure or explicit morphology-domain constraints;
- identical admission rules across source, TKI, reflection, and CodeGen;
- no independently maintained parameter, alias, generic, or lowering
  exceptions.

If these properties cannot be obtained without continuing context-specific
exceptions, the current hat surface should be declared unsuccessful even if
the internal handle semantics are retained under another syntax.
