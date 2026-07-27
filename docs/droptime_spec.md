# Toka `droptime` Specification

**Status**: Post-1.0 syntax frozen; semantic implementation baseline

**Target**: Additive Toka 1.x feature after implementation qualification

**Normative scope**: This document does not amend the frozen Toka 1.0 surface.

## 1. Purpose

`droptime` installs one deterministic local cleanup obligation anchored to one
or more raw-handle storage paths.

```toka
droptime *fp {
    unsafe fclose(*fp)
}
```

Unlike a general `defer` statement, `droptime` is not an arbitrary
scope-exit callback. Its target paths are part of the source contract and are
tracked by PAL for the lifetime of the cleanup obligation.

The feature is intended for local unmanaged FFI handles that do not justify a
named `@encap` wrapper. Movable, returnable, container-stored, shared, or
cross-task resources continue to use explicit resource types.

## 2. Frozen Surface Syntax

The initial public spelling is block-only:

```text
droptime-statement :=
    "droptime" raw-handle-path ("," raw-handle-path)*
    droptime-captures?
    block

droptime-captures :=
    "[" copy-capture ("," copy-capture)* "]"

copy-capture :=
    "copy" identifier
```

Examples:

```toka
droptime *fp {
    unsafe fclose(*fp)
}

droptime *read_end, *write_end {
    unsafe close(*write_end)
    unsafe close(*read_end)
}

droptime *mapping [copy size] {
    unsafe msync(*mapping, size)
    unsafe munmap(*mapping, size)
}
```

The following are not alternate spellings:

```toka
defer unsafe fclose(*fp)       // not Toka syntax
drop *fp { unsafe fclose(*fp) } // not a registration form
on_drop *fp { }                // no alias
droptime *fp => unsafe fclose(*fp) // no expression-body form
```

`droptime` is the only keyword for this contract. The dormant, unimplemented
`defer` lexer reservation is not a compatibility alias and should be removed
when `droptime` is implemented.

## 3. Anchor Eligibility

The initial version accepts only initialized local raw-handle paths:

```toka
droptime *fp { } // accepted anchor form
```

The initial version rejects other morphology:

```toka
droptime ^owned { }   // owning handles already have ownership/drop semantics
droptime ~shared { }  // the local scope may not own the last shared identity
droptime &borrowed { } // a borrower has no cleanup ownership
droptime payload { }  // payload anchoring is outside the initial surface
```

Hat morphology selects the path being anchored; it does not grant ownership
that the binding did not already possess. In particular, `droptime` does not
bring raw-pointer provenance, aliasing, nullability, or pointee validity into
PAL's safe-borrow guarantee.

Every anchor must:

1. name stable local storage rather than a temporary expression;
2. be initialized at the `droptime` statement;
3. remain in scope until the obligation executes;
4. be distinct from, and path-disjoint with, every other anchor in the same
   obligation; and
5. have no other active `droptime` obligation.

Examples of rejected anchors:

```toka
droptime make_file() { } // temporary expression
droptime *p, *p { }      // duplicate path
droptime obj, obj.*field { } // overlapping path prefixes
```

## 4. Obligation Lifetime

Executing a `droptime` statement arms one non-escaping cleanup obligation in
the current lexical scope. The obligation becomes active only after all
anchors and copy captures have passed semantic checking.

While it is active:

- no anchor may be rebound;
- no anchor may be moved or passed with `cede`;
- no anchor may be explicitly freed or dropped outside the cleanup body; and
- an operation that may invalidate an anchor conflicts with the obligation.

The initial version has no disarm, release, transfer, or early-fire operation.
Code that needs to move or hand off cleanup ownership must use a named
`@encap` resource instead.

A `droptime` statement in a branch is armed only on the executed branch. A
statement inside a loop body creates one obligation for that iteration's
lexical scope and executes it when that iteration leaves the scope.

## 5. Cleanup Execution

An armed cleanup body executes exactly once on every normal exit from its
lexical scope, including:

- fallthrough;
- `return`;
- `break`;
- `continue`; and
- early return through postfix `!`.

Multiple obligations in one scope execute in reverse registration order.
One multi-anchor statement remains one obligation: its body executes once, and
the order of operations inside that body is exactly the written source order.
The anchor list does not imply an additional cleanup order.

Toka's existing panic boundary remains unchanged. `panic` is non-returning
process termination and does not promise stack unwinding, so `droptime`
cleanup is not guaranteed after a panic point.

## 6. Cleanup Body Restrictions

A cleanup body is synchronous, non-escaping, and `void`. It may inspect its
anchor identities and use explicitly copied captures.

The initial version rejects:

- `await`, suspension, or task handoff of an anchor;
- `return`, `break`, `continue`, `pass`, or `yield`;
- moving, rebinding, or `cede`-ing an anchor;
- implicit capture of another local binding; and
- a cleanup result or propagated cleanup error.

Cleanup APIs that can fail should normally have a named `@encap` wrapper with
an explicit consuming `close` operation. `droptime` is a best-effort local
cleanup boundary and cannot propagate an error from scope exit.

Only `copy` captures are part of the initial syntax:

```toka
droptime *mapping [copy size] {
    unsafe munmap(*mapping, size)
}
```

Captured resource ownership and borrowed cleanup contexts are deferred. They
should be modeled with a named resource type rather than widening the first
`droptime` contract.

## 7. Async and Execution Boundaries

An active `droptime` obligation may not cross `.await` or another suspension
point in the initial version. This avoids silently extending a raw handle into
a coroutine frame before cancellation, frame destruction, and reactor
deregistration semantics are specified for the feature.

An anchor may not be captured by a closure passed across `.start`,
`thread_spawn`, or another execution boundary while its obligation is active.
Unrelated task creation remains governed by the existing execution-boundary
rules.

## 8. Multi-Anchor Meaning

Multiple anchors declare one inseparable cleanup contract:

```toka
droptime *first, *second {
    unsafe close_pair(*first, *second)
}
```

They are not shorthand for two independent obligations. If two resources have
independent cleanup, prefer two statements:

```toka
droptime *first {
    unsafe close(*first)
}

droptime *second {
    unsafe close(*second)
}
```

Metadata needed by cleanup is a capture, not an anchor:

```toka
droptime *mapping [copy size] {
    unsafe munmap(*mapping, size)
}
```

## 9. Lowering Contract

`droptime` must lower without heap allocation, reference counting, or a
runtime callback registry. The compiler may represent it as a hidden
non-escaping cleanup guard or as structured cleanup edges, provided that PAL
and exactly-once behavior are preserved.

The lowering must:

1. record the anchor set and copied captures at the registration point;
2. emit the cleanup body on every normal exit edge from the active scope;
3. preserve reverse registration order;
4. avoid duplicate execution at merged control-flow exits; and
5. preserve ordinary cleanup ordering with surrounding `@encap` values.

`droptime` does not change the source type of an anchor and does not turn a raw
pointer into a safe or movable owner.

Because `droptime` is local syntax, no obligation is exported through `.tki`.
The lexer, parser, formatter, semantic index, diagnostics, and source cache
must nevertheless agree on the syntax and its active source semantics.

## 10. Relationship to Existing Resource Mechanisms

Use `droptime` for a local unmanaged FFI handle that remains in one lexical
scope:

```toka
droptime *fp {
    unsafe fclose(*fp)
}
```

Use a named `@encap` type when a resource must be moved, returned, stored,
shared, explicitly closed with error handling, or transferred across an
execution boundary:

```toka
auto file = CFile::open(path)!
consume(cede file)
```

Use a library `ScopeGuard` when an exit action has no honest resource-path
anchor. Toka does not add a general `defer` statement as part of this feature.

## 11. Qualification Gate

Promotion into the normative 1.x syntax documents requires:

1. parser, formatter, semantic-index, and diagnostic coverage for the frozen
   syntax;
2. positive execution cases for fallthrough, nested scopes, branches, loops,
   `return`, `break`, `continue`, postfix `!`, LIFO order, and multi-anchor
   cleanup;
3. negative cases for invalid morphology, temporaries, duplicate and
   overlapping anchors, rebind, move, `cede`, explicit free, implicit capture,
   forbidden cleanup control flow, and suspension;
4. exact-once resource tests under the normal and sanitizer gates;
5. proof that lowering adds no heap allocation or runtime callback registry;
   and
6. synchronized updates to `docs/syntax.md`, `docs/syntax_zh.md`, the semantic
   rule matrix, and the 1.x compatibility record.

Until those gates pass, this file freezes the Post-1.0 source spelling and
serves as the implementation baseline; it does not advertise `droptime` as an
implemented public feature.

## 12. Deferred Extensions

The following are deliberately outside the initial contract:

- expression bodies using `=>`;
- payload, `^`, `~`, or `&` anchors;
- obligation disarm, transfer, or early execution;
- resource-owning or borrowed captures;
- active obligations across `.await`;
- cleanup error propagation;
- inferred cleanup from arbitrary foreign functions; and
- general scope-exit callbacks without a resource anchor.

Any later extension must preserve the frozen block syntax and must be additive
under the Toka 1.x compatibility policy.
