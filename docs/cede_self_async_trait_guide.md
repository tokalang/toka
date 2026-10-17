# Toka Consuming Async Trait Methods (`cede self` Async Trait) Guide

## 1. Overview & Motivation

In asynchronous systems, resources such as network sockets, database connections, and background task carriers require asynchronous cleanup. In previous Toka versions, traits could only declare synchronous methods or methods with borrow receivers. 

Toka 0.18.0 introduces **Consuming Static Async Trait Methods** (`cede self` async trait methods). This feature allows traits to define lifecycle-terminating methods that consume the receiver and execute asynchronous teardown:

```toka
pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}
```

Key characteristics:
- **Linear Ownership Transfer**: Passing `cede self` moves the receiver into the coroutine frame, preventing use-after-free or double-close bugs.
- **Static Dispatch**: Calls are statically dispatched and monomorphized without runtime trait object overhead.
- **Coroutines & Cleanup**: Coroutine frame fields track owned resources and ensure deterministic unwinding upon completion, error return (`!`), or task cancellation.
- **No Lifetime Annotations**: Memory safety is enforced through lexical structured concurrency and compiler dataflow analysis; no explicit lifetime syntax (`'a`) is introduced.

---

## 2. Core API Pattern

### 2.1 Trait Definition
An async trait declares a method taking `cede self`:
```toka
import core/result::{Result}

pub shape CloseError(code: i32)

pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}
```

### 2.2 Trait Implementation
Types implement the trait by accepting `cede self` and transferring ownership into the coroutine:
```toka
import core/task::{TaskHandle}

pub shape Resource(
    inner: TaskHandle<i32>,
    tag: i32
)

impl Resource@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        auto value = owned.inner.await
        return Result<i32, CloseError>::Ok(value + owned.tag)
    }
}
```

### 2.3 Consumption
Consuming async methods can be invoked directly, through generic helper functions, or within top-level synchronous wrappers:

#### Direct `.await`
```toka
auto r = Resource(inner=handle, tag=42)
auto res = r.close().await
```

#### Generic Monomorphized Helper
```toka
pub fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    return owned.close().await
}
```

#### Synchronous Block On
```toka
import std/task::{block_on}

fn main() -> i32 {
    auto r = Resource(inner=handle, tag=42)
    auto res = block_on(close_any(cede r))
    return 0
}
```

---

## 3. Lifecycle & Drop Semantics

Consuming static async trait methods are verified across 7 lifecycle and unwinding paths:

1. **Normal Completion**: Coroutine runs to end of execution, resolves result, and drops all internal frame fields in reverse declaration order.
2. **Early Error Return Before Suspend (`!`)**: If an early return occurs before the first suspension point, all fields initialized up to that point are dropped cleanly.
3. **Early Error Return After Suspend (`!`)**: If an error return occurs after resuming from suspension, suspended frame state is unwound deterministically.
4. **Cold Handle Direct Drop**: If a coroutine handle is created but never started or awaited, dropping the handle executes immediate synchronous frame disposal.
5. **Caught Cancellation (`await?`)**: When cancellation is injected and caught by an enclosing scope, the coroutine cleans up frame resources before yielding the cancellation signal.
6. **Uncaught Cancellation (`race2`)**: In concurrent races where one branch loses and is cancelled, unfinished child coroutines undergo uninterruptible drain and cleanup.
7. **Generic Polymorphic Dispatch**: Different concrete types implementing `@AsyncClose` monomorphize independently without cross-talk or runtime dispatch degradation.

---

## 4. Static Safety & Compiler Boundaries

The compiler enforces strict fail-closed boundaries:

- **No Dynamic Dispatch (`error[E0617]`)**:
  `dyn @AsyncClose` is rejected. Trait objects cannot represent consuming asynchronous state machines.
- **No Unrelaxed Receiver Variants (`error[E0618]`)**:
  Borrow receivers (`self`, `self#`, `self: &Self`, `self: ~Self`) and constructor receivers (`init cede self`) on async trait methods are rejected.
- **Trusted Wrapper Enforcement (`error[E0456]`)**:
  Attempting to bypass `cede` obligations by writing a user-defined function named `block_on` without `cede` triggers compile-time rejection (`error[E0456]`). The compiler authenticates `std/task::block_on` by module identity.
- **Escape Prevention (`error[E0455]` / `error[E04584]`)**:
  An async trait method cannot return local stack references or detach task handles borrowing local state.
