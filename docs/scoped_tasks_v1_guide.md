# Toka Scoped Tasks ("显式汇聚受限 v1" Explicit Convergence Restricted v1) Guide

## 1. Overview & Structured Concurrency Model

Toka 0.16 introduces **Scoped Tasks (Explicit Convergence Restricted v1)**, establishing structured concurrency primitives with compile-time lifetime and escape safety guarantees.

In asynchronous systems, uncontrolled background tasks frequently outlive parent stack frames, triggering use-after-free, memory corruption, and unpredictable resource leaks. Toka's Scoped Tasks solve this fundamentally:

- **Stack-Bound Concurrency**: Concurrently spawned tasks are bound to an explicit `TaskScope`.
- **Guaranteed Convergence**: The parent task scope must explicitly join or cancel all spawned child tasks before leaving scope. Stack-allocated buffers captured by reference or value are guaranteed to remain valid throughout the execution of all child tasks.
- **Escape Prevention**: Task handles and unjoined execution contexts cannot escape the scope boundary through returns, container embedding, or forged routing.
- **Uninterruptible Drain Barriers**: Scope cleanup phases (`cancel_and_join`) run under an uninterruptible cancel barrier, ensuring deterministic resource disposal even if cancellation is re-injected mid-drain.

---

## 2. Core API Reference

The standard scoped concurrency primitives are provided in `std/async` and `std/task`:

```toka
import std/task::{block_on, sleep_async}
import std/async::{TaskScope, spawn_scoped, join_scoped, cancel_and_join}
```

### 2.1 `TaskScope`
Represents an active concurrency scope:
```toka
auto scope# = TaskScope::new()
```
`TaskScope` tracks active spawned tasks within its lexical block. It is a linear resource that must be converged before scope termination.

### 2.2 `spawn_scoped`
Launches an asynchronous child task attached to the enclosing scope:
```toka
auto handle = spawn_scoped<T>(scope#, cede async_expression())
```
- Returns a scoped `TaskHandle<T>`.
- The child task begins execution concurrently on the runtime scheduler.
- The parent scope retains ownership and tracking of the child task lifetime.

### 2.3 `join_scoped`
Explicitly converges all tasks in the scope:
```toka
join_scoped(scope#).await
```
- Suspends the parent until all child tasks spawned under `scope#` have completed.
- Releases the scope, allowing the enclosing block to exit safely.

### 2.4 `cancel_and_join`
Cancels all active child tasks and awaits their cleanup:
```toka
cancel_and_join(scope#).await
```
- Injects cancellation into all active child tasks in the scope.
- Enters an uninterruptible drain barrier: awaits termination and resource cleanup of every child before returning.

---

## 3. Explicit Convergence Contract

In Explicit Convergence Restricted v1, convergence is **mandatory and explicit**. A scope cannot simply fall out of lexical scope while tasks remain in flight.

### 3.1 Normal Convergence Pattern

```toka
import std/task::{block_on, sleep_async}
import std/async::{TaskScope, spawn_scoped, join_scoped, cancel_and_join}

shape Buffer(size: i32, tag: i32)

fn worker(buf: Buffer, multiplier: i32) -> async i32 {
    sleep_async(10:i32).await
    return buf.size * multiplier
}

fn coordinator() -> async i32 {
    auto buf = Buffer(size = 100:i32, tag = 1:i32)
    auto scope# = TaskScope::new()

    // Spawn concurrent children accessing parent stack data
    auto t1 = spawn_scoped<i32>(scope#, cede worker(buf, 2:i32))
    auto t2 = spawn_scoped<i32>(scope#, cede worker(buf, 3:i32))

    // Await task outputs with cancellation propagation (.await?)
    auto r1 = t1.await?
    auto r2 = t2.await?
    if r1.is_none() || r2.is_none() {
        cancel_and_join(scope#).await
        return -1:i32
    }

    auto sum = r1.unwrap() + r2.unwrap()

    // Explicit convergence: guarantee all children are terminal
    join_scoped(scope#).await

    // buf is safely dropped only after scope convergence
    return sum
}
```

### 3.2 Cancellation & Drain Pattern

```toka
fn coordinator_with_cancel() -> async i32 {
    auto scope# = TaskScope::new()
    auto t1 = spawn_scoped<i32>(scope#, cede worker_a())
    auto t2 = spawn_scoped<i32>(scope#, cede worker_b())

    auto r1 = t1.await?
    if r1.is_none() {
        // Child 1 was canceled or failed; cancel remaining children and drain
        cancel_and_join(scope#).await
        return -1:i32
    }

    join_scoped(scope#).await
    return r1.unwrap()
}
```

---

## 4. Compile-Time Safety & Escape Prevention

Toka 0.16 enforces strict compile-time boundaries to eliminate lifetime escapes and data races:

| Violation Category | Diagnostic Code | Behavior |
| :--- | :--- | :--- |
| **Return Escape** | `error[E0455]` | Returning `TaskHandle` outside the spawning function or scope body is strictly rejected. |
| **Container Escape** | `error[E0456]` | Storing `TaskHandle` inside heap containers, shapes, structs, tuples, or arrays is rejected. |
| **Unjoined Scope Exit** | `error[E0456]` | Exiting scope with unjoined or un-drained child tasks is rejected. |
| **Detached Task Start** | `error[E04583]` | Detached starting of task handles without scope attachment is prohibited. |
| **Concurrent Mutation Conflict** | `error[E0440]` | Spawning tasks with conflicting mutable borrows of shared captures is rejected. |
| **Borrowed Child Result** | `error[E04658]` | Borrowing internal child results across scope boundaries is prohibited. |
| **Forged Scope Bypass** | `error[E0456]` | Fabricating pseudo-scopes or routing spawn through unverified wrappers is rejected. |

---

## 5. Forwarder Wrappers

Cold (unstarted) task handles can be forwarded across helper functions provided their lifetime connection is explicitly declared using parameter borrow annotations:

```toka
import core/task::{TaskHandle}

fn forward_cold(cede t: TaskHandle<i32>) -> TaskHandle<i32> <- t {
    return cede t
}
```
This preserves the static borrow link to the original execution context and prevents detached escape.

---

## 6. Uninterruptible Cancel Drain Barrier

When `cancel_and_join(scope#).await` executes:
1. Cancellation signals are dispatched hierarchically to all active child tasks in the scope.
2. The runtime establishes an **uninterruptible drain barrier** for the scope.
3. If an external cancellation signal reaches the parent coordinator while it is already inside `cancel_and_join`, the parent **will not abort mid-drain**.
4. All child tasks complete their cleanup, final yields, and encapsulation drop hooks (`impl T@Encap { fn drop(self#) }`).
5. Only after all children have reached a terminal state and all child resources are destroyed does the parent exit the barrier.
6. This guarantees that parent stack variables are dropped strictly after child resource destruction.

---

## 7. Source-Hidden TKI Compilation

Scoped task libraries can be compiled into `.tki` interface files and `.o` object files:

```bash
# 1. Compile provider into object and interface
tokac -c scoped_provider.tk --emit-interface -o provider.o

# 2. Downstream consumer compiles against interface without provider source
tokac -I ./interface_dir consumer.tk provider.o -o application_bin
```

Downstream consumers can instantiate and coordinate scoped tasks with complete type checking and ABI fidelity without access to the provider's source code.

---

## 8. Restricted Compositional Consistency (New in Toka 0.17.0)

Toka `0.17.0` upgrades Scoped Tasks from single-variable tracking (`v0.16.0`) to **Restricted Compositional Consistency ("受限组合收敛" v1)** across composite carriers, multi-task shapes, method receivers, and `.tki` interfaces.

### 8.1 Field-Sensitive Task-Handle Carriers
Shapes and structs may hold one or more `TaskHandle<T>` fields alongside data fields. The compiler tracks local borrow dependencies (`FieldDependencySet`) per field:

```toka
import core/task::{TaskHandle}
import std/task::{block_on}

shape Buffer(size: i32, tag: i32)
shape TwoTasks(t1: TaskHandle<i32>, t2: TaskHandle<i32>)

fn borrow_worker(b: Buffer) -> async i32 <- b { return b.size }
fn owned_worker() -> async i32 { return 42:i32 }

pub fn run_independent_owned_field() -> i32 {
    auto buf = Buffer(size = 100:i32, tag = 1:i32)
    auto holder = TwoTasks(
        t1 = borrow_worker(buf), // t1 borrows local `buf`
        t2 = owned_worker()      // t2 is dependency-free (owned)
    )
    // Allowed in 0.17: starting `holder.t2` is safe because `t2` has no local borrow
    holder.t2.start
    return block_on<i32>(holder.t2)
    // Note: `holder.t1.start` would be rejected with error[E04584]
}
```

### 8.2 Method Receivers: `cede self#` vs Borrowed `self#`
- **Owned Consuming Receiver (`cede self#`)**: A method taking `cede self#` may start an internal `TaskHandle<T>` field (`self.t.start`) provided that the carrier instance passed by the caller holds no local-borrowing task fields:
  ```toka
  shape Runner(t: TaskHandle<i32>)
  impl Runner {
      pub fn start_owned(cede self#) -> i32 {
          self.t.start
          return 0:i32
      }
  }
  ```
- **Borrowed Receiver (`self#`) Restriction**: A method taking a borrowed receiver (`self#`, without `cede`) cannot detached-start `self.t.start` or `(self.t).start` (`error[E04584]`), nor can an `effects: self <- t` method launder a scoped task obligation outside its originating `TaskScope` (`error[E0456]`).

### 8.3 Retained-Body vs Bodyless `.tki` Interfaces
- **Retained-Body `.tki` (`// @meta local_body_policy: checked-local-v1`)**: Cross-module helper functions that retain their body in `.tki` (such as cold forwarders `fwd_external`, `consume_external`, or `spawn_scoped_external`) participate in full compositional borrow and scope verification.
- **Bodyless `.tki` Declarations**: Passing a local-borrowing `TaskHandle<T>` to a bodyless `.tki` function declaration is rejected with `error[E0456]` because the compiler cannot verify that the opaque external callee does not detached-start or escape the borrowed handle.

### 8.4 Diagnostic Summary (`v0.16.0` → `v0.17.0` Relationship)

| Pattern | `v0.16.0` Behavior | `v0.17.0` Behavior | Diagnostic |
| :--- | :--- | :--- | :--- |
| Direct `TaskScope` + `spawn_scoped` + `join_scoped` / `cancel_and_join` | Supported | Supported (100% compatible) | — |
| Multi-task carrier starting owned field (`holder.t_owned.start`) | Over-rejected when sibling borrowed | **Supported** (field-sensitive) | — |
| Consuming `cede self#` method starting owned task field | Over-rejected | **Supported** | — |
| Carrier / split-var / nested field / postfix `.start` on borrowed task | Partial check on direct handle | **Fail-closed across all carrier/projection paths** | `error[E04584]` |
| Carrier wrapper escape / dual-scope (`spawn` `s1#`, `join` `s2#`) | Checked on direct scope | **Fail-closed across carriers & receiver `effects`** | `error[E0456]` |
| Passing local-borrowing `TaskHandle` to bodyless `.tki` declaration | Unchecked bodyless sink | **Fail-closed** | `error[E0456]` |

