# RFC: `@Encap` Hybrid Policy and Lifecycle Separation

> **Archive — not normative.** This historical RFC describes a pre-2026-08
> design, including removed scoped and wildcard visibility forms. Do not use it
> to decide current compiler or library behaviour. The current semantic entry
> point is [`encap_current_contract.md`](encap_current_contract.md).
> This archive also describes the removed Safe nullable payload/drop model.
> Current Toka rejects that surface with E0484-E0487; only raw `nul *T`
> may-zero remains.

**Status:** Superseded design record.

**Historical implementation gate:** This pre-clean-break gate is retained only
to explain the audit sequence. It does not constrain the current v6 contract.

**Historical compatibility premise:** The proposal deliberately rejected a
compatibility shim for `clone`, `clone = delete`, callable `drop`, structural
`@Encap`, and wildcard access. The current contract records the adopted
clean-break result rather than this proposal's staged epoch plan.

**Rule IDs:** `ENCAP-POLICY-001`, `ENCAP-ACCESS-001`,
`ENCAP-COHERENCE-001`, `OWN-RESOURCE-001`, `OWN-COPY-001`,
`OWN-DUP-001`, `OWN-DROP-001`, `TKI-ENCAP-001`.

**Depends on:** declaration-backed H/P authority, PAL, explicit `cede`,
deterministic cleanup, and source/TKI semantic equivalence.

## 1. Decision

Toka keeps the distinctive two-mode access model:

```text
shape without explicit @Encap       explicit impl Type@Encap
-----------------------------       ------------------------
Transparent Shape                   Governed Capsule
fields visible with the type        fields closed by default
no access-policy declaration        exact grants form a policy
```

This is an **access-governance** distinction. It is not a complete ownership
classification.

A Transparent Shape may contain a move-only resource, require structural
drop, and be non-copyable. A Governed Capsule may be a small, compiler-verified
copyable value. The following semantic facts are therefore orthogonal:

```text
PolicyWitness(T)
AccessPolicy(T)
CopyWitness(T)
DupWitness(T)
DropPlan(T) = Trivial | Structural | Custom
```

One declaration may atomically elaborate into several facts, but no subsystem
may infer one of these facts merely from another.

Two additional compiler-internal facts support those type-level decisions:

```text
ModuleCoordinate(M) = (CrateId, LogicalModulePath)
ResourceContract(T) = None | Borrowed(ResourceId) | Owned(ResourceId)
```

They are resolver/type-contract provenance, not public traits and not additional
meanings of `@Encap`. In particular, a raw-pointer spelling never creates an
`Owned` fact.

In particular:

- `T: @Encap` proves only an explicit governance policy.
- `T: @Copy` proves implicit duplication is valid.
- `T: @Dup` proves an explicit `dup` operation is callable.
- `DropPlan(T)` is a compiler lifecycle fact, not a user-callable trait.

## 2. Motivation

The current model overloads one nominal `@Encap` implementation with four
unrelated responsibilities:

1. nominal trait satisfaction;
2. field-access policy;
3. `clone` method lookup or deletion;
4. destructor lowering.

That overloading admits contradictory states. A compiler-generated structural
destructor can create a public `@Encap` witness without any authored access
policy. A deleted `clone` can count as a required trait method. A private
`drop(self#)` hook remains callable from privileged source contexts.

This RFC replaces negative declarations and incidental registration effects
with positive, separately checkable facts.

## 3. Goals

1. Preserve Toka's zero-noise Transparent Shape syntax.
2. Make `T: @Encap` a sound proof of explicit policy authorship.
3. Make future fields of a Governed Capsule private by construction.
4. Make implicit copy, explicit duplication, and drop independently testable.
5. Remove every hidden non-trivial duplication path.
6. Keep structural cleanup automatic without forging a policy witness.
7. Preserve exact H/P and PAL authority; visibility must never amplify
   permissions.
8. Make source and source-less TKI compilation produce identical decisions.
9. Make custom-hook cleanup and subsequent field cleanup provably
   non-overlapping.
10. Make crate/path grants and external-resource contracts depend on stable
    resolver provenance rather than filesystem or morphology guesses.

## 4. Non-goals

- No garbage collection or unwinding model.
- No negative trait implementations.
- No general policy-directive language such as
  `policy dup = shared | custom`.
- No fallible duplication trait in the first revision.
- No runtime reflection or trait object for `@Encap` or `@Copy`.
- No automatic non-trivial fieldwise `@Dup` derivation.
- No cross-crate `pub(path)` friend grant in the first revision.
- No field-taking/disarming operation inside a custom drop hook.
- No compatibility with the previous language/TKI epoch.

## 5. Terms

### 5.1 Transparent Shape

A nominal shape with no explicit, source-level `impl Type@Encap` in its
defining module.

Its fields are accessible wherever the nominal type itself is accessible,
subject to field morphology, declaration visibility, H/P authority, unsafe
boundaries, and PAL.

“Transparent” does not mean POD, trivially droppable, or copyable.

### 5.2 Governed Capsule

A nominal shape whose defining module contains exactly one valid
`impl Type@Encap` policy declaration.

Its fields are inaccessible outside the defining module unless an exact
policy grant authorizes the access context.

“Governed” does not prove secrecy or non-leakage. Public methods may
intentionally expose values or views. It proves that direct field access is
subject to an authored policy.

### 5.3 Move-only

This RFC uses **move-only (affine)** rather than strictly linear. A move-only
value has at most one live owner, may be transferred with `cede`, and may be
dropped without first being otherwise consumed.

### 5.4 Copy and Dup

- **Copy** is implicit, compiler-verified duplication with no user code.
- **Dup** is an explicit, source-preserving duplication operation.

Every Copy value is also duplicable through a compiler intrinsic, but a
Dup-only value is never copied implicitly.

### 5.5 Resolver identity

Every resolved module has one opaque `ModuleCoordinate`:

```text
ModuleCoordinate = (CrateId, LogicalModulePath)
```

`CrateId` is assigned from the resolver's package/workspace graph.
`LogicalModulePath` is the normalized import path inside that crate. Neither
component is an absolute source path, current-working-directory-relative path,
basename, symlink spelling, or TKI cache location.

A resolver domain is one root package/workspace, one compiler-configured
toolchain/library domain, or one locked/package-map dependency node. If the
resolver cannot prove that two loose modules belong to the same domain, it
assigns distinct `CrateId` values. A nominal declaration identity is therefore
anchored as:

```text
TypeDefId = (ModuleCoordinate, NominalDeclarationId)
```

### 5.6 Resource contract provenance

`ResourceContract(T)` is an internal fact on a nominal safe wrapper, not on
the spelling of one of its raw fields. It may originate only from:

1. a compiler-defined nominal resource contract; or
2. a resolver-validated FFI resource record bound to one canonical wrapper
   `TypeDefId`, `ResourceId`, acquire operation, and release operation.

The existing package-level `ffi_resources` record is a valid source only after
the resolver validates it and binds it to that wrapper identity. A manifest
name, hook-body scan, field name, non-null promise, allocator return type, or
tooling report grants no language authority.

`*T`, `Addr`, and references remain raw/BorrowedView morphology regardless of
the wrapper contract: they gain no automatic drop, policy witness,
dereference authority, or public ownership trait. `unsafe alloc` likewise
returns an explicitly unsafe raw allocation; wrapping and releasing it is an
author-maintained capsule invariant unless a separate compiler-defined
resource contract covers the wrapper.

`Owned(ResourceId)` makes the wrapper type non-Copy and records the external
cleanup protocol for auditing, summaries, and TKI. Ordinary `cede` transfers
the wrapper's existing ownership obligation; it does not manufacture a
resource contract for a raw pointer.

An `Owned` wrapper contract requires either compiler-defined drop glue or an
explicit Governed Capsule custom hook. Conversely, a custom hook may manage
author-maintained unsafe raw state without claiming an `Owned` language fact.

## 6. Core facets

The new language epoch defines these semantic-core facets:

```toka
pub trait @Encap {}

pub trait @Copy {}

pub trait @Dup {
    pub fn dup(self) -> Self
}
```

They are re-exported by the standard prelude.

`@Encap` and `@Copy` are compiler-known marker facets:

- `@Encap` has special policy-declaration syntax and provenance rules.
- `@Copy` has no user method and every explicit implementation is structurally
  verified by the compiler.

`@Dup` is an ordinary behavioural facet with strict signature checking.

The old semantic-core `@Clone` and `@Drop` declarations are removed.

## 7. `@Encap` policy declaration

The body of an `@Encap` declaration accepts only:

1. exact field-access grants; and
2. at most one compiler-only `drop` hook.

```toka
pub shape Device(
    id: i32,
    secret: string,
    native#: usize
)

impl Device@Encap {
    pub id
    pub(crate) native

    fn drop(self#) {
        // release custom state
    }
}
```

Ordinary methods belong in `impl Device { ... }`. Duplication belongs in
`impl Device@Dup { ... }`.

### 7.1 Policy coherence

`ENCAP-COHERENCE-001` requires:

1. Only the nominal type's defining module may declare its `@Encap` policy.
2. A nominal type has at most one policy declaration.
3. A generic policy must apply uniformly to every instantiation.
4. A generic policy may repeat the shape's parameters but may not add trait
   bounds or `where:` constraints.
5. Policy specialization and overlapping `@Encap` implementations are
   rejected.
6. Type aliases do not acquire an independent policy; a new nominal type may.

Valid:

```toka
shape Box<T>(item: T)

impl<T> Box<T>@Encap {
}
```

Rejected:

```toka
impl<T: @Dup> Box<T>@Encap {
}
```

Access governance cannot vary with generic argument capabilities.

### 7.2 Policy satisfaction

`PolicyWitness(T)` exists if and only if the selected nominal definition has a
valid explicit policy declaration.

Compiler-generated structural lifecycle work never creates
`PolicyWitness(T)`.

Consequently:

```toka
fn governed<T: @Encap>(value: T) {}
```

accepts a Governed Capsule and rejects a Transparent Shape, including a
Transparent Shape that requires structural drop.

`T: @Encap` does not prove:

- that `T` has non-trivial drop;
- that `T` is move-only;
- that `T` is Copy or Dup;
- that no method can expose internal state;
- that `T` is Send or Sync.

Those claims require their own facts.

### 7.3 No dynamic `@Encap`

`dyn @Encap` is always rejected. Policy metadata is attached to the concrete
nominal type and has no runtime method dictionary. Removing methods from the
marker must not accidentally make it object-safe.

## 8. Exact member-access policy

### 8.1 Transparent default

Without `PolicyWitness(T)`, every field is accessible in every context that can
name and access `T`.

This is a visibility default only. It does not grant payload write authority,
handle rebinding, ownership transfer, or a PAL exemption.

### 8.2 Capsule default

With `PolicyWitness(T)`, every field is accessible inside the defining module
and closed elsewhere unless an exact grant applies.

Supported grants are:

```toka
pub field1, field2
pub(crate) field3
pub(os/driver/uart) field4
```

- `pub` grants access wherever the type is accessible.
- `pub(crate)` grants access only when the access-site and definition
  `ModuleCoordinate` values have equal `CrateId` components.
- `pub(path)` is resolved at the policy declaration, using the same resolver
  graph as an import, into a `ModuleTree(CrateId, LogicalModulePath)` grant.
  In this revision the target must have the owner's `CrateId`; cross-crate
  friend grants are rejected. It authorizes that exact logical module and its
  logical descendants by segment-prefix comparison.

A bare policy path is interpreted from the defining crate's logical namespace
root. Logical `./` and `../` are interpreted relative to the defining module's
logical directory and may not escape the crate root. Absolute filesystem paths
and `::` item-selection syntax are invalid policy targets.

The resolver must reject an unresolved or ambiguous `pub(path)` declaration.
It must not fall back to an absolute/relative filesystem path, basename,
substring or suffix match, current working directory, `/lib/` stripping,
symlink target, or TKI cache path.

The defining module is recognized by exact `ModuleCoordinate` equality, not
by “same physical source file”. Package aliases and source/TKI forms that
resolve to the same logical module receive the same coordinate; two packages
that happen to use the same physical file or trailing path do not.

The access context is the lexical `ModuleCoordinate` of the source operation.
Generic instantiation does not borrow the caller's authority or lose the
generic definition's authority.

An access grant applies uniformly to every direct field-access form:

- member projection used for reading or writing;
- named construction and update;
- pattern matching and destructuring;
- spread construction or transfer; and
- compiler-generated projections.

No access form may bypass `AccessPolicy` merely because it does not build an
ordinary member-expression AST node.

The decision is:

```text
CanNameField(field, use):
    requester = lexical ModuleCoordinate(use)
    owner     = lexical ModuleCoordinate(field.parent)

    if requester == owner:
        return Allow

    for grant in exact grants for field:
        if grant == Global:
            return Allow
        if grant == Crate
           and requester.CrateId == owner.CrateId:
            return Allow
        if grant == ModuleTree(target)
           and requester.CrateId == target.CrateId
           and segment_prefix(target.path, requester.path):
            return Allow

    return Deny
```

`segment_prefix(["build", "internal"], ["build", "internal", "codec"])` is
true; it is false for `["build", "internalized"]`. `Allow` only permits the
field name to be used. Morphology, H/P, PAL, ownership, and unsafe checks still
run independently.

Multiple grants for one field must be identical or form an unambiguous union
of contexts. Conflicting or malformed grants are diagnosed at the policy
declaration.

### 8.3 No wildcard grants

The following forms are removed:

```toka
pub *
pub * ! secret
```

A policy must enumerate every externally accessible field. Adding a new field
to a Governed Capsule therefore keeps it closed without any author action.

Wildcard tokens in an `@Encap` body are parse errors.

## 9. Copy semantics

### 9.1 Implicit Copy

An operation that would create a second live value without `cede` or an
explicit `.dup()` requires `CopyWitness(T)`.

This includes implicit duplication during:

- value initialization and assignment;
- by-value argument formation when a second value is created;
- return-value duplication;
- aggregate construction from an existing lvalue;
- closure `[copy ...]` capture.

The compiler must never call user code to satisfy an implicit Copy operation.

### 9.2 Automatic Copy

The compiler automatically derives `CopyWitness(T)` for:

- primitive and other language-defined Copy values;
- fixed arrays whose elements are Copy;
- Transparent Shapes whose fields and variant payloads are all Copy and whose
  `DropPlan` is `Trivial`.

A Transparent Shape containing a move-only or Dup-only field is not Copy.

Example:

```toka
shape Point(x: f64, y: f64) // automatically Copy

shape Pair(left: Device, right: Device)
// transparent fields, structural drop, not Copy
```

### 9.3 Explicit verified Copy for capsules

An `@Encap` policy suppresses automatic Copy derivation. This makes a capsule
move-only by default.

The defining module may explicitly request verified Copy:

```toka
shape NonZero(raw: u32)

impl NonZero@Encap {
}

impl NonZero@Copy {}
```

The compiler accepts the empty implementation only if:

1. `DropPlan(T) == Trivial`;
2. every field and variant payload is Copy;
3. no field owns a unique, shared, or otherwise non-Copy resource;
4. the type has no custom drop hook; and
5. copying preserves all PAL provenance and dependency facts.

User code cannot provide a method body or bypass this structural proof.

This explicit escape hatch keeps private-representation scalar values and
borrowed views ergonomic without binding access governance to resource
identity.

### 9.4 Copy proof algorithm

Copy validation is a monotone, fail-closed analysis over the resolved
**by-value ownership field graph**. Each concrete type/instantiation is in one
of three states:

```text
Unknown | ProvenCopy | ProvenNonCopy
```

An `Unknown` state never authorizes Copy. It is retained only to distinguish
an incomplete proof from a proved negative result and to produce an accurate
diagnostic. During one proof epoch, `Unknown` may transition once to either
terminal state; the two terminal states never overwrite one another.

Graph edges include every by-value component whose representation would be
duplicated: shape fields, every enum variant payload, tuple elements,
fixed-array elements, nullable payloads, and substituted generic arguments.
Every variant is checked even though only one is active at runtime. `[T; 0]`
still requires `T: @Copy`, so changing a const length cannot silently change
the capability domain.

Pointer/reference indirection is not recursively expanded. Such morphology is
classified by its own language fact: a raw pointer, ordinary function pointer,
or read-only borrowed reference/view may be a Copy leaf with its existing
provenance; a unique/shared cleanup owner or writable/exclusive borrowed view
is `ProvenNonCopy`.

The analysis is:

1. Resolve the complete concrete field graph and generic substitution
   environment.
2. Seed `ProvenNonCopy` for a structural- or custom-drop type, an
   `Owned(ResourceId)` wrapper contract, an ownership-bearing non-Copy handle,
   or a Governed Capsule without an explicit `@Copy` request. `@Dup` never
   satisfies a Copy edge.
3. Seed language-defined Copy leaves and generic parameters whose active
   environment proves `T: @Copy` as `ProvenCopy`.
4. Collapse the remaining graph into strongly connected components and
   evaluate the component DAG to a fixed point.
5. Mark a candidate `ProvenCopy` only when every by-value dependency is
   `ProvenCopy` and every local structural rule succeeds.
6. Propagate `ProvenNonCopy` from any negative dependency, retaining the first
   canonical field path that proves the blocker. Leave unresolved,
   unconstrained-generic, or otherwise incomplete dependencies as `Unknown`;
   they fail Copy authorization.

A residual by-value recursive SCC is a layout/type error, not an
optimistically Copy recursive type. Cycles through raw pointers or references
terminate at those leaf facts because indirection is not a by-value graph
edge.

For generic code, a parameter is Copy only when the active bound environment
entails `@Copy`. A transparent generic instantiation may therefore become
Copy when its concrete arguments or caller bounds complete the proof. An
explicit `impl T@Copy {}` is accepted only when its entire applicability
domain resolves to `ProvenCopy`; `Unknown` is an error, not a deferred
promise.

For example, a governed `Box<T>` that stores `T` may request
`impl<T: @Copy> Box<T>@Copy {}`. An unbounded `impl<T> ...` is rejected
because the universal domain contains unproved/non-Copy substitutions.

A generic definition is summarized without unbounded monomorphization:

```text
CopyRecipe = Always | Never(reason) | All(requirements) | Dependent(fact)
```

Concrete memoization uses `(TypeDefId, CanonicalArguments)`. A repeated key is
handled by the SCC worklist. Direct-storage recursion whose canonical
arguments grow on every step is a non-regular recursive-layout error, not a
reason to guess pointer size or Copy status.

An imported closed opaque type is `ProvenCopy` only through a valid TKI v2
`CopyWitness`; an authoritative v2 absence is `ProvenNonCopy`. Missing, stale,
old-epoch, or unverifiable structure/facts remain `Unknown` and fail closed.
The fixed-point result and diagnostic witness path must be independent of
declaration-registration order.

### 9.5 Copy implies Dup

Every `@Copy` type receives an intrinsic `DupWitness`:

```text
CopyWitness(T) => DupWitness(T, IntrinsicCopy)
```

The intrinsic copies the value and propagates its existing PAL provenance. It
does not introduce an artificial dependency on the receiver storage
location.

This implication is coherent and exclusive. A user `impl T@Dup` whose
applicability intersects any `CopyWitness(T)` domain is rejected. There is no
priority, override, specialization, or registration-order rule between an
intrinsic and user Dup witness. For a generic implementation, inability to
prove the domains disjoint is an overlap error.

## 10. Explicit duplication

### 10.1 Behavioural contract

A non-Copy type becomes explicitly duplicable through an ordinary
implementation:

```toka
impl Device@Dup {
    pub fn dup(self) -> Device {
        return Device(
            id = self.id,
            secret = self.secret.dup(),
            native = duplicate_native(self.native)
        )
    }
}
```

`DupWitness(T)` is one of:

```text
IntrinsicCopy
UserDup(function, generic bounds, dependency summary)
```

The compiler must strictly validate a user `@Dup` implementation:

1. the method is named `dup`;
2. it is public;
3. its receiver is non-consuming `self`;
4. its result soul type is exactly `Self`;
5. it has no extra value parameters;
6. it does not add a return dependency on the receiver storage place;
7. its generic bounds and PAL provenance summary are preserved in TKI;
8. it is declared in the nominal type's defining module; and
9. its applicability domain is provably disjoint from intrinsic Copy
   providers; and
10. no competing user `@Dup` implementation applies.

Provider collection is set-based and order-independent:

```text
DupProvider(T) =
    IntrinsicCopy(CopyProofId)
  | UserImpl(ImplDefId, substitution, normalized bounds)
```

Zero applicable providers means no `@Dup`; one is selected; more than one is
a hard coherence error. `.dup()` resolves through this provider set, not by
looking for an ordinary method with the same spelling.

### 10.2 Why `dup(self) -> Self`

The canonical signature is not `dup(self#) -> Self#`.

- `self#` requests exclusive payload mutation, but duplication normally
  observes the source and leaves it valid.
- `Self#` would attempt to grant payload authority from the callee. A fresh
  destination's H/P authority comes from its own declaration and flow
  ceilings.
- Internal reference counts or OS bookkeeping may mutate behind an
  abstraction boundary without making the logical receiver exclusively
  mutable.

An operation that genuinely mutates visible source state may be an ordinary
method, but it does not satisfy `@Dup`.

### 10.3 Explicitness

`.dup()` is never inserted implicitly.

For a Dup-only value:

```toka
auto moved = cede source // transfer; source invalidated
auto second = source.dup() // duplicate; source remains valid
auto invalid = source // rejected if this would create a second owner
```

Generic code may request:

```toka
fn duplicate<T: @Dup>(value: T) -> T {
    return value.dup()
}

fn duplicate_capsule<T: @{encap, Dup}>(value: T) -> T {
    return value.dup()
}
```

The first accepts both Copy values and explicit Dup implementations. The
second additionally requires an authored governance policy.

### 10.4 Closure capture

Capture modes are separated:

```toka
[copy value] // requires @Copy; compiler copy only
[dup value]  // requires @Dup; explicitly invokes duplication
[cede value] // transfers and invalidates the source
```

`[copy ...]` must not invoke a user `dup` implementation.

### 10.5 Lifetime dependencies

`@Dup` guarantees operation availability and source preservation. It does not
erase borrow dependencies, but it also must not make the result borrow the
receiver's stack/storage place merely because the operation was invoked.

The rule is:

```text
Deps(intrinsic_copy(source)) = Deps(source)
Deps(user_dup(source))       = Deps(source)
```

The selected witness's member/provenance summary is propagated through the
call and serialized in TKI. A spelling such as
`dup(self) -> Self <- self` is not a valid `@Dup` witness: it creates a new
receiver-place dependency instead of duplicating the input value's existing
provenance. A borrowed view should use verified `@Copy`, or expose an ordinary
view/to-owned conversion with the appropriate result type.

### 10.6 Fallible duplication

`@Dup` is infallible at the Toka type level. A resource whose duplication can
report failure exposes an ordinary operation such as:

```toka
pub fn try_dup(self) -> Result<Self, DupError>
```

`try_dup` does not produce `DupWitness(T)`. A generic fallible duplication
protocol is deferred.

## 11. Drop semantics

### 11.1 Drop plan

Every concrete type has one compiler lifecycle plan:

```text
DropPlan(T) =
    Trivial      no cleanup
    Structural   recursively clean live owned fields
    Custom       invoke an authored hook, then a compiler-owned field tail
```

`DropPlan` is not a nominal trait witness.

### 11.2 Structural drop

Shape analysis derives `Structural` when a field, element, or any enum variant
payload may require cleanup and no custom hook exists. Runtime cleanup visits
only the active variant.

Structural drop:

- does not synthesize `impl T@Encap`;
- does not enter trait or user method maps;
- does not create a vtable;
- does not change field visibility;
- does not by itself determine Copy or Dup beyond the independent Copy rules.

The backend may inline a recursive drop cascade. It need not emit a synthetic
named destructor or any TKI structural-drop marker when the complete field
graph is already available.

### 11.3 Custom drop hook

A Governed Capsule may declare:

```toka
fn drop(self#) {
    // custom cleanup
}
```

The only valid signature is `fn drop(self#)`: non-public, one
receiver, no additional parameters, no method-local generic parameters, and
no async/wait/suspending effect. Enclosing nominal generic parameters remain
available.

This spelling is a compiler hook, not a method:

- it is not registered in ordinary method lookup;
- it cannot be called explicitly;
- it is not inherited through `T: @Encap`;
- it is invoked at most once for each value and exactly once on every
  guaranteed normal cleanup path; and
- it cannot replace the compiler's structural cleanup of fields.

The exact normal-path sequence is:

```text
1. verify no active borrow conflicts with destruction
2. mark the whole value Dropping
3. snapshot the compiler live state
4. invoke the custom hook
5. on normal hook return, drop every compiler-live structural field
6. invalidate the whole storage
```

Normatively:

```text
DropPlan(T) =
    Trivial
  | Structural(FieldDropPlan)
  | Custom(HookSymbol, TailFieldDropPlan)

custom_drop_glue(place):
    if root_live(place) == false:
        return
    root_live(place) = false
    call HookSymbol(dropping_borrow place#)
    run TailFieldDropPlan(place)
```

The hook and glue are separate control-flow boundaries. Every explicit or
implicit normal `return` in the authored hook returns to the glue, never past
the tail plan. `self#` is a non-escaping `DroppingBorrow`, not an owned value
that user code can transfer.

For a struct/tuple, step 5 uses declaration order. A fixed array uses ascending
index order. An enum drops only its active variant payload, whose subfields use
declaration order. Nullable payload cleanup first tests presence. Toka's
non-unwinding panic boundary does not promise later hook/field cleanup after a
panic.

In this language epoch, a `Custom` value never supports partial `cede`.
Consequently the hook does not own or mutate a structural field live mask:

“Compiler-live” here means the complete initialized fields plus intrinsic
enum-variant, nullable-presence, array, and nested states. It does not mean an
arbitrary partial-move mask for the custom aggregate.

- `cede self.field`, `uninit`, explicit field drop/forget, a consuming field
  receiver/argument, replacement of a structural field, and whole-`self`
  transfer/return/capture are rejected;
- `self` or a reference/raw view derived from it cannot escape into a return,
  global, closure, async frame, or longer-lived storage, and the hook cannot
  suspend with `await` or `wait`;
- a structural field may be observed through shared access, but the hook may
  not obtain mutable or consuming authority over it;
- compiler-owned discriminants, nullable-presence bits, and liveness state
  used by the tail plan cannot be forged or mutated by the hook;
- these restrictions apply through call/operation summaries; an opaque call
  that receives `self`, `self#`, or a structural field and cannot prove the
  restriction is rejected; and
- raw/scalar/other `Trivial` fields may be inspected or mutated, but doing so
  never clears a structural live bit.

There is no general source-level “disarm this structural field” operation.
Assigning `null`, `ADDR0`, or a boolean state flag, calling an FFI release
operation, or executing `unsafe free` affects raw/custom state only; none of
those operations suppresses later structural field cleanup.

Raw morphology does not establish resource ownership. When a wrapper has an
`Owned(ResourceId)` contract, its matched acquire/release operations and
resource identity are retained in semantic summaries and TKI. A hook may also
release a manually maintained raw allocation inside `unsafe`, but that remains
an author obligation: the compiler makes no automatic ownership or release
claim merely because the hook contains `free`, `close`, or a sentinel write.

The compiler guarantees one hook invocation and non-overlapping structural
cleanup. It does not make arbitrary unsafe code correct: duplicated foreign
release calls inside the hook remain an author error.

### 11.4 No public `@Drop`

There is no public destructor facet and no direct `.drop()` operation.

If Toka later adds explicit early destruction, it must be a compiler operation
that consumes and invalidates the source, not a call to the hook.

### 11.5 Partial `cede`

Partial-move eligibility is decided from `DropPlan`, live-mask support, and
the concrete aggregate shape. It is not decided from `PolicyWitness`.

Sema and CodeGen consume one authoritative plan:

```text
PartialMovePlan(root, projection) exists iff:
    root is a compiler-tracked local or coroutine-frame place
    DropPlan(root.type) == Structural
    projection is a direct named field
        or a fixed-array constant index
    the initial bounded representation has at most 64 tracked slots
    every slot has a complete typed, mask-aware DropAction
```

Any unsupported projection, dynamic index, custom-drop root, untracked place,
or backend-incomplete field kind is rejected before lowering. Policy presence
does not affect the result. A value with `DropPlan == Custom` rejects every
partial field/element/payload `cede` in every method, including its hook.

The committed transition is:

```text
require live[i] == 1
verify PAL invalidation for the exact projection
take value from place[i]
set static and runtime live[i] = 0
publish the value to its new owner
```

If checking or handoff fails before commit, neither bit changes. Reading the
projection requires a live bit. Reinitialization evaluates the right-hand side
completely, stores it, then sets both bits to one. Control-flow joins use
bitwise intersection. A whole read or whole `cede` requires all declared
slots live; whole `cede` consumes the root flag rather than pretending to be a
series of partial moves.

Scope cleanup invokes the typed `DropAction` for every set bit, including
unique and shared handles; it must not skip a field merely because its
morphology is `^` or `~`. Lowering an accepted partial move without the exact
plan/runtime mask is a compiler invariant failure, never a silent no-op.

A future drop-aware take/disarm operation requires a separate RFC and proof
for all control-flow, call-summary, return, cancellation, and TKI paths.

## 12. Worked model

| Type | Policy | Fields | Copy | Dup | Drop |
|---|---|---|---|---|---|
| `Point(f64, f64)` | none | transparent | automatic | intrinsic | trivial |
| transparent `Pair<Device>` | none | transparent | no | explicit `@Dup` | structural |
| `Device@Encap` | explicit | grants | default no | explicit `@Dup` | structural/custom |
| `NonZero@Encap + @Copy` | explicit | exact grants | verified | intrinsic | trivial |

The access dichotomy remains simple:

```text
no @Encap  => transparent fields
@Encap     => closed fields plus exact grants
```

Ownership remains precise:

```text
@Copy      => implicit copy and intrinsic dup
@Dup       => explicit source-preserving duplication
otherwise  => move-only; use cede
DropPlan   => independent cleanup strategy
```

## 13. Generic policy and container example

Policy and duplication must not be expressed through overlapping policy
implementations.

```toka
shape Vec<T>(
    // representation fields
)

impl<T> Vec<T>@Encap {
    fn drop(self#) {
        // release custom storage; live elements remain compiler/accounting safe
    }
}

impl<T: @Dup> Vec<T>@Dup {
    pub fn dup(self) -> Vec<T> {
        // explicit element duplication
    }
}
```

All `Vec<T>` instantiations have one stable access policy. Only instantiations
whose element type is Dup obtain the operation witness.

Registration order must never select between overlapping policy
implementations.

## 14. Removed syntax and behaviour

The new epoch removes:

1. `clone = delete` and the general function-declaration `= delete` form;
2. `clone` as a compiler-recognized copy/lifecycle operation;
3. implicit insertion of a non-trivial `clone` or `dup` call;
4. `@Clone` and `@Drop` semantic-core traits;
5. ordinary method lookup for the `drop` hook;
6. `pub *` and `pub * ! ...` policy entries;
7. compiler-generated structural `impl T@Encap`;
8. structural `@Encap` vtables and trait witnesses;
9. conditional or overlapping policy implementations.

`clone` need not become a reserved identifier. A user may define an ordinary
method with that name, but it has no ownership, Copy, Dup, or lowering
meaning.

## 15. Sema elaboration model

The compiler must maintain separate authoritative facts:

```text
ModuleIdentityMap<M> -> (CrateId, LogicalModulePath)
PolicyMap<T>         -> PolicyWitness + exact resolved AccessPolicy
ResourceContractMap<T>
                     -> None | Borrowed(ResourceId) | Owned(ResourceId)
DropPlanMap<T>       -> Trivial | Structural(fields) | Custom(hook, tail)
PartialMovePlan<P>   -> typed slot/mask actions or no plan
CopyProofMap<T>      -> Unknown | ProvenCopy | ProvenNonCopy
CopyWitnessMap<T>    -> Builtin | AutoStructural | ExplicitVerified
DupProviderMap<T>    -> IntrinsicCopy | selected user @Dup implementation
MethodMap<T>         -> ordinary callable methods only
```

Here `M`, `T`, and `P` use resolver-backed module, canonical type/definition,
and exact access-path identities, not short names or filesystem strings.
Generic instantiation must cache `(ImplDefId, ConcreteTypeId)` directly; the
presence of a first method is not a valid proxy because `@Encap` and `@Copy`
are empty markers.

Elaboration is:

```text
resolver/package graph
    -> ModuleIdentityMap and canonical TypeDefId values
    -> validated FFI wrapper bindings in ResourceContractMap

compiler-defined nominal owners
    -> built-in ResourceContractMap entries

explicit impl T@Encap
    -> PolicyMap<T>
    -> optional custom-hook candidate

shape field analysis
    -> fixed-point DropPlanMap<T> = Trivial | Structural | Custom
    -> SCC/fixed-point CopyProofMap<T>
    -> CopyRecipe for generic definitions

eligible Transparent Shape with ProvenCopy
    -> CopyWitnessMap<T> = AutoStructural

explicit impl T@Copy
    -> require ProvenCopy across its applicability domain
    -> CopyWitnessMap<T> = ExplicitVerified

CopyWitnessMap<T>
    -> intrinsic Dup provider candidate

explicit impl T@Dup
    -> strict behavioural trait validation
    -> user Dup provider candidate

all Dup candidates
    -> overlap/coherence check
    -> exactly zero or one DupProviderMap<T> entry
```

Drop and Copy proof may be computed as one product fixed point, or Drop may
stabilize first; Copy must never query a partially initialized boolean
`HasDrop`. No synthetic lifecycle node may pass through the ordinary
trait-registration path.

## 16. TKI v2 contract

Adoption increments the TKI format version and invalidates all previous
interfaces and caches.

TKI v2 must preserve:

1. the defining module's logical path and resolver-binding fingerprint;
2. explicit `PolicyWitness` provenance and canonical owner `TypeDefId`;
3. the exact field grant list as global/crate/logical-module-prefix scopes;
4. resource identities and acquire/release provenance needed by exported
   ownership summaries;
5. explicit verified `@Copy` facts and generic `CopyRecipe` values;
6. `@Dup` signatures, provider identity, normalized generic bounds,
   dependency summaries, and generic bodies when instantiation requires them;
7. custom drop-hook presence and its exact ABI symbol;
8. the full private field/type/morphology graph required to recompute
   structural Copy, Drop, and partial-move decisions;
9. policy, intrinsic/user Dup, and operation coherence identity; and
10. enough lexical module identity to replay access from generic bodies.

Conceptually:

```text
ModuleIdentityRecord {
    identity_schema_version
    logical_module_path[]
    resolver_binding_digest
}

EncapPolicyRecord {
    owner_type_def_id
    owner_crate_ref = self
    owner_module_path[]
    grants[] = {
        field_name
        scope = global | crate | module_prefix
        target_crate_ref = self
        target_module_prefix[]
    }
}
```

`self` is rebound to the importing resolver's already-established crate node;
an interface cannot self-assert an authority-granting `CrateId`. Absolute
`source_path` may remain loading/diagnostic metadata, but never participates
in access authorization. Relocation, symlinks, cache locations, import aliases
and generated/source-backed TKI must not alter a decision.

The loader verifies that owner module, type, field list, normalized grant
targets, resource records, and resolver binding agree. Missing or forged
identity fails as an interface identity error rather than falling back to a
filesystem heuristic. A package-map, logical module coordinate, resource
contract, or normalized policy-target change invalidates the relevant cache.

TKI v2 must not encode structural lifecycle as:

- `impl T@Encap`;
- a deleted duplication method;
- a trait vtable; or
- a public policy witness.

For a structural type, an importer recomputes `DropPlan` from the transported
shape graph. For a custom-drop type, it calls the exported custom destructor
ABI recorded by the interface.

Source and source-less compilation must agree on:

- every trait-bound result;
- every field-access decision;
- Copy/Dup availability;
- partial-move eligibility; and
- exactly-once cleanup.

## 17. Diagnostics

The implementation must provide dedicated diagnostics for:

- wildcard policy entries;
- duplicate, external, conditional, or overlapping `@Encap` policies;
- malformed, unresolved, crate-escaping, ambiguous, or cross-crate
  `pub(path)` targets;
- unavailable/forged module or policy identity, with no physical-path
  fallback;
- direct access to a non-granted capsule field;
- implicit duplication of a non-Copy value;
- `.dup()` without `@Dup`;
- an invalid `@Dup` receiver, return type, visibility, or provider result;
- intrinsic/user or user/user `@Dup` domain overlap;
- a `ProvenNonCopy` blocker with its canonical field path;
- an `Unknown` Copy proof, unresolved generic requirement, or opaque fact;
- direct, mutual, or non-regular recursive value layout;
- direct invocation of the `drop` hook;
- a public, extra-parameter, method-generic, non-void, or suspending drop hook;
- `cede`, uninit/replacement, consuming access, or opaque whole-self calls
  forbidden inside a custom hook;
- a claimed managed wrapper without a compiler/validated-FFI resource
  contract;
- `dyn @Encap` and `dyn @Copy`;
- a TKI format from the previous semantic epoch.

Diagnostics should name the missing positive capability:

```text
value is move-only; use `cede value` to transfer it
type does not satisfy `@Dup`; no source-preserving duplicate is available
type does not satisfy `@Copy`; implicit duplication is forbidden
```

They should not suggest adding a negative declaration.
Identity diagnostics display logical crate/module coordinates, never absolute
host paths. Copy/Dup coherence diagnostics point to both provider origins.

## 18. Implementation slices

This is a clean break, but the implementation should still land in auditable
slices on one non-release branch.

### Slice 0: redlines and resolver identity closure

This is a blocking prerequisite. No policy, Copy/Dup, or custom-drop checker
may switch to the new semantics until its exit gate is complete.

- Record the current compiler/library/TKI behaviour and inventory every
  intentional epoch delta.
- Add redline fixtures for structural-drop witness rejection, custom-hook
  field-transfer rejection and cleanup order, wrapper-resource provenance,
  Copy/Dup provider overlap, Copy SCC termination, and source/TKI parity.
- Introduce resolver-owned `CrateId`, logical `ModulePath`, `ModuleId`, and
  `TypeDefId` in shadow mode for source, source-less/cached TKI,
  package-mapped, toolchain, overlay, and multi-root modules.
- Preserve lexical `ModuleId` on declarations, expressions, generic clones,
  and imported declarations.
- Resolve policy paths to logical segment prefixes in the owner's crate;
  remove physical paths, CWD, basenames, `/lib/`, `/tests/`, and `source_path`
  from authorization decisions.
- Define the canonical by-value field graph, three-state Copy transfer
  function, generic `CopyRecipe`, recursive-layout errors, and deterministic
  witness-path ordering.
- Define custom-hook compiler glue and wrapper `ResourceContract` provenance
  before lowering code depends on either.
- Compute old/new identity and access decisions in shadow mode and classify
  every disagreement.

Slice 0 exits only when:

1. module/type identity is deterministic under relocation and import aliases;
2. same-crate and cross-crate positive/negative matrices are complete;
3. source, generated-TKI, and cached-TKI identity decisions agree;
4. custom-drop order/mask and Copy-SCC redlines have reviewed expected results;
5. forged or missing metadata fails closed; and
6. every existing standard-library/build-system path grant resolves to one
   owner and target.

### Slice 1: semantic data model

- Add `PolicyMap`, `ResourceContractMap`, `DropPlanMap`, `PartialMovePlan`,
  `CopyProofMap`, `CopyWitnessMap`, and `DupProviderMap`.
- Key policy and operation facts by canonical type/definition identity rather
  than a base-name string.
- Key instantiated generic implementations by
  `(ImplDefId, ConcreteTypeId)`, including empty markers.
- Make `@Encap` and `@Copy` compiler-known empty markers.
- Add strict `@Dup`.
- Explicitly exclude `@Encap` and `@Copy` from dyn object safety.

### Slice 2: policy parser and access checking

- Restrict `@Encap` bodies to exact grants and one drop hook.
- Remove wildcard grammar.
- Enforce owner-module, uniqueness, and unconditional generic policy.
- Activate Slice 0 resolver identity for same-module, `pub(crate)`, and
  logical segment-prefix `pub(path)` checks.
- Route projection read/write, named initialization/update,
  pattern/destructure, spread, and generated projection through one
  `CanNameField` check.
- Reject cross-crate `pub(path)` and missing identity without a string
  fallback.

### Slice 3: lifecycle lowering

- Remove structural `ImplDecl` synthesis.
- Remove `IsStructuralDrop`, structural trait registration, and structural
  vtables.
- Derive `DropPlan` from the field graph.
- Lower each custom hook into user hook code plus compiler-owned cleanup glue;
  every normal hook return flows through the same field tail.
- Freeze struct/tuple declaration order, ascending array order, active enum
  payload order, and nullable-presence handling.
- Reject every partial `cede` for `DropPlan == Custom` and enforce the hook
  restrictions from Section 11.3.
- Inline structural cascades; emit an out-of-line symbol only for custom drop.
- Remove the drop hook from user method lookup.

### Slice 4: Copy and Dup

- Remove `IsDeleted`, declaration `= delete`, `@Clone`, `@Drop`, and automatic
  clone injection.
- Implement the three-state SCC/fixed-point Copy proof and generic
  `CopyRecipe`; reject unresolved concrete and recursive-layout cases.
- Implement intrinsic Dup for Copy types.
- Implement strict user `@Dup` and intrinsic/user domain-coherence checking.
- Add `[dup ...]`; restrict `[copy ...]` to Copy.

### Slice 5: TKI v2

- Increment interface/compiler/cache versions.
- Serialize resolver identity, normalized policy, wrapper resource contracts,
  Copy proof/recipe, Dup provider/coherence, custom-drop, and field-graph
  facts.
- Delete structural-drop marker replay.
- Reject all v1 interfaces and rebuild every dependency.

### Slice 6: library and specification rewrite

- Remove every deleted clone declaration.
- Reclassify value-like `@Encap` types as Transparent or explicit verified
  Copy capsules.
- Rename non-trivial clone operations and call sites to explicit Dup.
- Replace every wildcard with exact field grants.
- Collapse overlapping generic policy implementations.
- Rewrite syntax, ownership, iterator, TKI, and closure-capture documentation.

No intermediate slice is a publishable language state.

## 19. Acceptance evidence

Adoption requires source and source-less coverage for all rows below.

### 19.1 Policy

- A structural-drop Transparent Shape fails `T: @Encap`.
- An explicit policy succeeds even with no drop hook.
- External, duplicate, conditional, and overlapping policies fail.
- `dyn @Encap` fails.

### 19.2 Access

- Transparent fields are accessible wherever their type is accessible.
- An unlisted capsule field is denied outside the defining module.
- Each exact global/crate/path grant has positive and negative coverage.
- `pub(crate)` permits a different module in the same resolver crate and
  rejects an identically named/path-suffixed module in another crate.
- A module-prefix grant permits exact/descendant logical segments and rejects
  sibling, substring, basename, and cross-crate matches.
- Relocation, symlinks, import aliases, package maps, and source/TKI cache
  locations cannot change an authorization result.
- A friend generic uses its lexical module authority regardless of caller;
  caller authority cannot amplify a non-friend generic.
- Read, write, named init/update, pattern, destructure, spread, and generated
  projection all use the same grant decision.
- A newly added unlisted field remains inaccessible.
- Both wildcard spellings fail parsing.
- H/P and PAL still reject an operation when visibility alone permits it.

### 19.3 Copy

- Plain scalar aggregates copy implicitly.
- A transparent aggregate containing a move-only resource does not.
- A capsule is move-only by default.
- A valid explicit capsule `@Copy` succeeds.
- `@Copy` with custom drop or a non-Copy field fails.
- Raw pointers/read-only borrowed views terminate as Copy leaves while unique,
  shared cleanup-owning, and writable/exclusive views block Copy.
- Tuple, every enum variant, nullable payload, nested field, and `[T; 0]`
  contribute the required Copy proof.
- `Box<T>` is conditionally Copy only under a proven `T: @Copy` environment;
  `T: @Dup` is insufficient.
- Direct/mutual/non-regular by-value recursion produces a layout diagnostic;
  recursion through indirection terminates deterministically.
- Opaque v2 Copy/No-Copy facts replay; missing, corrupt, old, or unresolved
  facts fail closed as `Unknown`.
- Registration-order permutations produce the same proof and blocker path.
- `[copy ...]` never calls user code.

### 19.4 Dup

- Copy types satisfy `T: @Dup` through the intrinsic.
- A Copy type plus a user `impl T@Dup` is rejected with both provider origins.
- A generic user Dup domain that overlaps, or cannot be proved disjoint from,
  an intrinsic Copy domain is rejected.
- A user `@Dup` leaves its source valid and returns a second value.
- Dup-only assignment, argument formation, and capture never invoke `dup`
  implicitly.
- `[dup ...]` invokes duplication exactly once.
- `dup(self#)`, `dup(cede self)`, and `dup(self) -> Self#` fail the witness
  contract.
- `dup(self) -> Self <- self` fails because it adds a receiver-place
  dependency.
- `try_dup` does not satisfy `@Dup`.
- PAL dependencies survive source and TKI calls.

### 19.5 Drop

- Plain nested resource aggregates drop structurally exactly once.
- Structural drop never satisfies `@Encap`.
- A custom hook runs once, then each structural field drops once in the
  frozen struct/array/enum/nullable order, including every early return.
- Public, extra-parameter, method-generic, non-void, and suspending hook
  signatures fail.
- Custom-hook field `cede`, uninit/replacement, consuming receiver/argument,
  discriminant/presence forging, and opaque whole-self calls fail.
- A raw pointer, `Addr`, allocator result, field name, or release call never
  becomes an owning type fact. A validated wrapper `ResourceContract`
  survives TKI, and `cede` transfers the wrapper without changing that
  contract.
- Null/sentinel assignment and foreign release calls do not clear a
  structural live bit.
- Direct `.drop()` fails in every source context.
- Proven structural partial `cede` updates static/runtime masks consistently;
  custom partial `cede` always fails.

### 19.6 Generics and TKI

- One unconditional generic policy applies to all instantiations.
- A conditional `@Dup` implementation is selected only when its bounds hold.
- Registration order cannot change policy or operation selection.
- Resolver identity, normalized grants, generic Copy recipes, resource
  provenance, and Dup-provider domains replay identically.
- Forged `source_path`, crate/module identity, policy owner, or grant target
  cannot expand authority.
- Every policy/access/Copy/Dup/Drop decision is identical with source present
  and with only TKI v2 plus its backing object.

## 20. Rejected alternatives

### 20.1 “No `@Encap` means POD and Copy”

Rejected. A transparent aggregate may contain a move-only field and require
structural cleanup. Access transparency does not erase ownership.

### 20.2 “Every `@Encap` type is permanently non-Copy”

Rejected. Private-representation scalar values and borrowed views need an
explicit, verified way to retain value semantics. Default move-only plus
verified `@Copy` preserves both safety and usability.

### 20.3 Put `dup` inside the `@Encap` block and derive `@Dup`

Rejected for the first revision. It creates a second, implicit way to form a
nominal behavioural witness and complicates generic bounds, coherence, and TKI
replay. A separate `impl T@Dup` is explicit and works uniformly for generic
types.

### 20.4 `dup(self#) -> Self#`

Rejected. `#` is payload authority, not ownership identity. Duplication does
not consume the source, and a fresh destination obtains authority from its
own declaration.

### 20.5 `policy dup = shared | custom`

Deferred. Duplication mechanisms may allocate, share, duplicate kernel state,
or fail. An ordinary trait implementation exposes code, bounds, errors, PAL
dependencies, and testable behaviour without a second policy language.

### 20.6 Retain `clone = delete`

Rejected. Absence of positive Copy/Dup facts already means move-only. A
negative pseudo-method adds no information.

### 20.7 Retain wildcard grants

Rejected. They make a future field public by omission and defeat
closed-by-default API evolution.

### 20.8 Infer ownership from `*T`, `Addr`, or field names

Rejected. Raw morphology and naming do not distinguish owned, borrowed,
aliased, or sentinel handles. Managed ownership requires compiler/FFI
provenance; otherwise the operation remains explicitly unsafe and unmanaged.

### 20.9 Let a custom hook take or disarm structural fields

Deferred. Once user code can change the structural live mask while the object
is dropping, every control-flow edge, call summary, unwind/return edge, and
TKI body must preserve that mask. The first revision forbids the operation and
keeps compiler-owned field cleanup non-overlapping.

### 20.10 Cross-crate `pub(path)` friend grants

Deferred. A stable friend grant must bind a locked package identity rather
than a consumer-controlled alias or physical checkout path. The first revision
keeps module-prefix grants inside the defining crate.

## 21. Exit criterion

This RFC may be marked adopted only when:

1. the compiler has no path from structural drop to `PolicyWitness`;
2. no implicit operation invokes user duplication code;
3. `@Copy`, `@Dup`, and `DropPlan` have independent authoritative facts;
4. the drop hook is unreachable through ordinary method resolution;
5. every custom-hook normal return enters one compiler cleanup tail, custom
   field transfer is rejected, and field order is executable evidence;
6. no raw morphology creates managed ownership without a compiler/validated
   FFI wrapper contract;
7. resolver identity, not a physical path or TKI assertion, decides
   same-module/crate/path authority across every field-access form;
8. Copy proof terminates through the specified SCC/fixed-point algorithm and
   no concrete `Unknown` authorizes an operation;
9. intrinsic/user Dup provider overlap is rejected without priority;
10. wildcard access and declaration `= delete` are absent from the grammar;
11. generic policy coherence is order-independent;
12. TKI v2 source-less replay matches source compilation; and
13. the standard library and normative syntax documents contain no legacy
   ownership meaning for `clone`, `@Drop`, deleted methods, or structural
   `@Encap`.

The resulting language identity is:

```text
Transparent by default.
Governed only by explicit policy.
Copied only by proof.
Duplicated only by an explicit positive capability.
Moved only through ownership transfer.
Dropped only by the compiler's lifecycle plan.
```
