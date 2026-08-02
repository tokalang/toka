# RFC: Initialization Contracts

**Status:** The `uninit` spelling is implemented; the `init` contract remains
design-only.

**Scope:** This RFC records the implemented `uninit` source spelling and a
proposed, explicit initialization-obligation contract for `init`. It builds on
the existing local `InitMask` analysis; only the future `init` surface remains
outside current parser, Sema, CodeGen, TKI, and public syntax behavior.

## 1. Motivation

Toka already distinguishes an initialized slot from a slot that may not be
read, and conservatively merges that fact through branches, loops, and async
suspension. The implemented `uninit` spelling exposes that state without
providing a first-class way to transfer or discharge an initialization
obligation at a function boundary.

Initialization should be as explicit as ownership transfer:

```text
cede: initialized, caller-owned value  -> caller no longer owns the value
init:  caller-owned uninitialized place -> caller receives an initialized value
```

An `init` contract therefore describes a state transition on a storage place,
not a nullable value, default value, or ordinary output parameter.

## 2. Frozen decisions

### IC-01: `uninit` names the source state

The lower-case expression spelling is `uninit`, not `unset`.

```toka
auto value# = uninit:i32
```

`uninit` creates a known, typed, not-yet-initialized slot. It may be used only
where an initialization-state origin is meaningful; it is not a general
operation for clearing an already initialized value. A read, escape, transfer,
or ordinary borrow of an uninitialized part remains rejected by the existing
initialization and PAL rules.

The old `unset` keyword is removed rather than retained as a compatibility
alias. Toka has no released source-compatibility obligation for it.

### IC-02: `init` is a two-sided initialization contract

An `init` parameter receives an uninitialized writable place, not a value of
type `T`. Its callee owns the obligation to leave that place fully initialized
on every reachable non-diverging return path. The call site explicitly grants
the same contract, analogous to the two-sided `cede` spelling.

Illustrative grammar only:

```toka
fn parse(input: str, init out#: Message) -> bool {
    out = Message::decode(input)
    return true
}

auto message# = uninit:Message
auto ok = parse(input, init message)
```

The exact parameter morphology and call-site token arrangement remain a
grammar-design item, but the following semantic facts are frozen:

- the caller supplies a compatible uninitialized storage place with the
  required write authority;
- the callee cannot read, borrow, return, capture, or cede its uninitialized
  content as a `T`;
- initializing constituent fields is permitted when it establishes a complete
  initialized value by the required exit;
- the callee must discharge the obligation on every reachable ordinary return,
  including error-valued and propagation-driven returns; and
- a diverging path has no return obligation.

On successful contract checking, the caller's post-call analysis state records
the exact place as initialized. A callee that leaves any reachable return path
incomplete is rejected at the declaration boundary, just as a `cede` callee
that fails its transfer obligation is rejected.

### IC-03: An `init` block is a lexical proof scope

An `init` block is a future lexical scope in which one or more named
initialization obligations must be discharged before the block's ordinary
exit. It is a static proof boundary, not a runtime constructor call or a
default-value mechanism. The exact block spelling and whether it lists its
target places are intentionally not frozen by this RFC.

The block uses the same exact-path `InitMask` state and control-flow merge
rules as ordinary local initialization. It must not introduce a second,
weaker definite-assignment analysis.

### IC-04: `Uninit<T>` remains a separate raw-storage wrapper

`uninit:T` and `Uninit<T>` are related but distinct:

- `uninit:T` is a source-level state of a local or structural slot, checked by
  initialization facts and later discharged by initialization.
- `Uninit<T>` is the existing raw/container storage wrapper. It has `T`'s
  physical layout but bypasses construction and drop for slots such as
  `new [N]Uninit<T>()`.

Neither form is an ordinary readable `T`, and neither provides an implicit
conversion to the other.

### IC-05: Keyword policy

`uninit` becomes a reserved expression keyword. `init` is initially a
contextual keyword: it is special only in a contract parameter, a contract
argument, or the future init-block grammar. It remains usable as an ordinary
method name elsewhere, preserving the existing `@Default::init` protocol.

## 3. Relation to expression uniqueness

The frozen `EU-02` rule in `docs/expression_uniqueness_rfc.md` requires every
`auto` binding to have an initializer and to infer its type from the RHS.
`uninit:T` is the explicit initializer for a later-initialized local:

```toka
auto record# = uninit:Record
```

It is not an exception that reintroduces `auto record: Record`.

## 4. Open design items

The following require a separate decision before implementation:

1. The exact syntax and target-list form of an `init` block.
2. The exact parameter and call-site morphology syntax for `init` places.
3. Whether an `init` obligation may remain live across `.await`, and the
   corresponding PAL/source-less replay evidence.
4. Whether a callable may report a non-returning failure channel that does not
   require initialization, and, if so, the explicit signature contract for
   that exception.
5. Diagnostics, error codes, and the precise allowed structural-field
   initialization forms for a future `init` contract.

## 5. Implementation gate

Implementation must provide parser, Sema, PAL, CodeGen, TKI, and source-less
replay agreement for all of the following:

- a local `uninit:T` cannot be read before complete initialization;
- branches, loops, guards, matches, and suspension preserve or reject the
  exact initialization state conservatively;
- an `init` callee is rejected if any reachable ordinary return leaves its
  target incomplete;
- a valid `init` call updates the caller's exact-path state to initialized;
- aliasing, borrowing, cede, capture, escape, and raw-pointer boundaries
  cannot bypass the obligation; and
- `Uninit<T>` allocation/drop behavior remains distinct from local
  initialization-state tracking.
