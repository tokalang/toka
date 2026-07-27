# Dynamic Borrowing Exploration

**Status:** Exploratory design note.  It changes no 1.0 rule and is not an
implementation RFC.

## Question

Toka already has static, field-level interior mutability (`field#`) and
synchronization primitives such as `Mutex` and `RwMutex`.  The open question is
narrower: should the standard library offer a single-thread dynamic-borrowing
container analogous to Rust's `RefCell<T>`?

The comparison question is not whether Toka has *any* interior mutability.  It
does.  The question is whether a runtime-checked shared/exclusive borrowing
policy is useful where static field authority is intentionally insufficient and
a lock would be the wrong abstraction.

## Minimum semantic contract to investigate

An exploratory `DynamicCell<T>` would need all of the following before it can
be judged useful:

1. `borrow` obtains a shared runtime guard; multiple shared guards may coexist.
2. `borrow_mut` obtains an exclusive runtime guard only when no shared or
   exclusive guard exists.
3. A conflicting request has one explicit policy: return `Result`, return
   `Option`, or terminate.  The policy must not be implicit.
4. Guards keep the cell alive and release their runtime borrow state exactly
   once on normal cleanup, `cede`, and early return.
5. Guards cannot escape in a way that invalidates the cell or smuggles a
   payload-write capability through an unrelated shared view.
6. The API states whether it is single-thread-only, and does not imply
   `@Send`/`@Sync` merely because it detects conflicts dynamically.

## Relation to Toka authority rules

`field#` remains a declaration-backed static capability.  A dynamic guard is
not permission elevation: the cell declaration would explicitly expose the
operation, and a successful exclusive guard would be the direct source of the
guard's payload-write authority.  Ordinary shared views must not acquire that
authority by adding `#` at a use site.

This means the feature can be evaluated using the same local rule as other
shared flows:

```text
effective operation = direct-source capability ∩ requested operation ∩ PAL
```

The dynamic state checks complement, rather than replace, declaration/parameter
authority and PAL interference checks.

## Required evidence before an RFC

- a real Toka API sketch that compiles without new compiler privileges;
- a use case that is materially clearer than `field#`, `Mutex`, or `RwMutex`;
- conflicting shared/exclusive tests, nested guards, early return, `cede`, and
  drop-order tests;
- a decision on failure reporting and diagnostics;
- an audit of async suspension and OS-thread crossing while a guard is live.

Until that evidence exists, this is an investigation target, not a Toka gap,
release blocker, or commitment to emulate Rust's `RefCell` API.
