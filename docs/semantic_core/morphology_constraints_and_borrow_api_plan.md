# Morphology Constraints and Borrow API Migration Plan

**Status:** Execution plan under review

**Prerequisites:**

- `3c6fd075` records the target-aware borrow model;
- `959486a3` implements payload versus handle-identity borrow selection;
- `4841a0c1` freezes the `borrow`/`dup`/`unsafe_get` naming families.

This plan intentionally separates two related workstreams. Morphology
constraints make generic signatures closed under their declared substitution
domain. Borrow API migration removes the legacy raw `get_ref` surface without
guessing which callers require safe loans, duplication, or raw addresses.

## 1. Non-goals

This phase does not:

- infer public morphology constraints from function bodies;
- model arbitrary raw-depth nullability in the first solver slice;
- change a legacy `get_ref` return type in place;
- preserve ordinary API disappearance as SFINAE;
- run a full cold audit after every migration cluster.

## 2. Morphology domain model

The initial managed domain is finite:

```text
Soul
Unique1        ^T
Shared1        ~T
Borrow1        &T
BorrowUnique2  &^T
BorrowShared2  &~T
BorrowBorrow2  &&T
```

Raw morphology is represented separately as a symbolic family. The first
constraint slice only needs to distinguish `NoRaw` from `RawChain`; it does not
need to solve every per-layer nullability sequence.

### 2.1 Built-in constraints

The compiler initially recognizes three non-trait constraint kinds:

```text
SoulOnly
BorrowExtendable = {Soul, Unique1, Shared1, Borrow1}
RawExtendable    = {Soul, RawChain}
```

`BorrowExtendable` is the preimage of globally legal morphology under an outer
borrow operation. `RawExtendable` is the corresponding preimage under an outer
raw-pointer operation.

These are compiler type-domain constraints, not user-implementable traits.

### 2.2 Candidate source surface

The preferred candidate extends the existing `where:` subject/relation shape
without using `@` trait syntax:

```toka
fn borrow_value<'T>() -> &'T
where:
    'T: morphology borrow_extendable
{
    ...
}
```

Alternative spellings must be compared before Parser work begins:

```text
'T: morphology borrow_extendable   preferred contextual form
morphology 'T: borrow_extendable   separate subject family
'T: @BorrowExtendable              rejected: falsely looks implementable
```

Public declarations must spell morphology constraints explicitly. Private
constraint inference is deferred; the first implementation rejects an
unproven composition at its definition site.

### 2.3 Semantic representation

`GenericParam` receives structured morphology bounds independent of
`TraitBounds`:

```cpp
enum class MorphologyConstraintKind {
  SoulOnly,
  BorrowExtendable,
  RawExtendable,
};

std::vector<MorphologyConstraintKind> MorphologyBounds;
```

Constraint intersection is a set operation. An empty domain is a declaration
error. A concrete substitution outside the resulting domain is a direct
constraint diagnostic, not candidate filtering.

### 2.4 Signature and interface requirements

Morphology bounds participate in:

- canonical function and method identity;
- TKI serialization and source-less replay;
- semantic index and public semantic evidence;
- generic instantiation and method lookup;
- cache invalidation and interface versioning;
- diagnostics at both definition and substitution sites.

The same-version TKI round trip must preserve the exact declared domain.

## 3. Generic definition closure

For every morphology composition in a generic signature or body, Sema computes
the domain required by that operation.

Examples:

```text
&'T  requires BorrowExtendable
*'T  requires RawExtendable
^'T  requires SoulOnly
~'T  requires SoulOnly
```

If the declared bounds do not imply the requirement, the generic definition is
rejected. A selected concrete call whose substitution violates a declared
bound reports a constraint mismatch with the composed illegal type.

Ordinary method lookup retains constrained members in the visible API. It may
rank or reject them by constraints, but must not erase them through implicit
SFINAE.

## 4. Current standard-library migration cases

The first known implicit morphology exclusions are:

```toka
Vec<'T>::get_ref()   -> *'T
Vec<'T>::unsafe_get() -> *'T
```

The quick audit currently observes four rejected candidates because two methods
are instantiated for two borrowed element types. They have different terminal
outcomes:

- `Vec::get_ref` is legacy raw API debt and is removed through the borrow API
  migration;
- `Vec::unsafe_get` remains raw and acquires an explicit `RawExtendable`
  constraint or moves to an appropriately constrained implementation.

The audit target `RejectedSFINAE >= 4` remains migration-only until these cases
are converted. It must not be copied into the final Phase 2 baseline.

## 5. Borrow API declaration migration

The final capability families are:

```toka
borrow(...)          -> &T
borrow_mut(...)      -> &T#
try_borrow(...)      -> Option<&T>
try_borrow_mut(...)  -> Option<&T#>

dup(...)             -> T
try_dup(...)         -> Option<T>

unsafe_get(...)      -> *T
```

### 5.1 Declaration targets

| Current API | Target API |
|---|---|
| `Vec::borrow` | retain `borrow` |
| `Vec::borrow_mut` | retain `borrow_mut` |
| `Vec::get_ref -> *T` | remove; classify callers |
| `Vec::get/get_opt` under `@Dup` | `dup/try_dup` |
| `HashMap::get_ref -> Option<&V>` | `try_borrow` |
| `HashMap::get` under `@Copy` | `try_dup` |
| `Slab::get/get_mut` | `try_borrow/try_borrow_mut` |
| `MutexLock::get_ref -> &T#` | `borrow_mut` |
| `RwReadLock::get_ref -> &T` | `borrow` |
| `RwWriteLock::get_ref -> &T#` | `borrow_mut` |

No `get_ref` remains in the terminal public surface. During migration, the
explicitly documented legacy `Vec::get_ref -> *T` is the only temporary raw
exception; no new raw `get_ref` surface may be introduced.

### 5.2 Repository inventory baseline

A textual inventory over `lib/`, `tests/`, and `examples/` currently finds
approximately:

```text
309 source lines containing `.get_ref(...)`
 96 zero-argument calls, predominantly guard projections
227 calls with an index or key argument
159 immediate payload/member uses
 70 borrow-then-duplicate uses (`.clone()`/`.dup()`)
 44 explicit `auto *... = ...get_ref(...)` bindings
```

The categories overlap and are not semantic proof. In particular, many
explicit `auto *` bindings exist only because legacy `Vec::get_ref` returned a
raw pointer; they may be safe-borrow callers rather than genuine raw consumers.

### 5.3 Required caller classification

Each call is classified using the resolved receiver declaration, not method
name text alone:

```text
Vec legacy raw get_ref
HashMap optional reference lookup
Mutex/RwLock guard projection
other same-named user or library method
```

Then it receives one migration decision:

```text
SafeRead       -> borrow / try_borrow
SafeWrite      -> borrow_mut / try_borrow_mut
Duplicate      -> dup / try_dup
RawAddress     -> unsafe_get, with the existing unsafe boundary
```

No global search-and-replace is permitted.

## 6. Migration sequence and commit boundaries

### B0: Declaration aliases

- add new safe and duplication names;
- retain old safe names only as temporary forwarding aliases;
- do not redefine raw `Vec::get_ref` as a safe reference;
- add focused tests for each new declaration.

Commit independently before caller migration.

### B1: Core library callers

Migrate `lib/core`, `lib/std`, and `lib/stdx` in bounded subsystem commits.
Every commit runs the affected package tests and the Handle Grammar quick gate.

### B2: Examples and tools

Migrate examples and build tools after the library surface is stable. Genuine
raw callers must be visibly routed through `unsafe_get`.

### B3: Tests and conformance

Update positive and negative fixtures only after production callers. Preserve
at least one diagnostic fixture proving that safe reference APIs cannot be used
as raw addresses.

### B4: Legacy removal

- remove `Vec::get_ref` and old duplication spellings;
- remove temporary safe forwarding aliases;
- verify zero source calls to removed declarations;
- update completion cards and public API matrices.

### B5: Morphology SFINAE closure

- constrain or relocate `unsafe_get`;
- replace implicit SFINAE events with explicit constraint decisions;
- set terminal `Implicit morphology SFINAE = 0`.

## 7. Qualification gates

Each implementation slice runs:

1. focused compiler/standard-library tests;
2. affected source-less TKI replay;
3. Handle Grammar Quick Audit;
4. full Fail and Conformance when diagnostics or public signatures change.

One final clean HEAD runs the cold full audit. Network tests that cannot bind
inside the sandbox are replayed outside the sandbox and recorded separately.

## 8. Stop conditions

Stop and return to design review if:

- public morphology constraints cannot be serialized without retaining bodies;
- constrained methods still disappear from semantic index or lookup;
- `borrow` of a morphic element cannot state its required domain independently
  of container instantiation;
- more than a small minority of ordinary container APIs require distinct
  morphology constraints;
- raw callers cannot be distinguished from safe-borrow callers using resolved
  semantic information.
