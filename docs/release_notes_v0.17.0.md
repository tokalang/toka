# Toka 0.17.0 Release Notes

Toka 0.17.0 is the compositional-consistency improvement release following `0.16.0` Scoped Tasks (`显式汇聚受限 v1`). This release fixes false-positive acceptances (unsound escape loopholes) and false-negative rejections of valid programs when borrowed or owned `TaskHandle<T>` values pass through wrapper helpers, aggregate carrier fields, ordinary method receivers (`self#` and `cede self#`), and `.tki` interface calls; programs that previously relied on erroneously accepted escape routes are now rejected at compile time. No new lifetime annotation syntax is introduced, and the verified `0.16.0` usage patterns remain the regression baseline. Source and interface cache migration requirements follow `docs/compatibility_0.17.md`.

（Toka 0.17.0 是继 0.16.0 显式汇聚受限 v1 之后的组合一致性改进版本。本版修复借用任务在包装、聚合字段、方法接收者及 TKI 调用中的误放行与合法程序误拒；部分过去错误接受的逃逸程序将被编译期拒绝。不新增生命周期标注，0.16 的已验证使用方式继续作为回归基线。源码和接口缓存的迁移要求以本版兼容说明为准。）

## Key Highlights

### 1. Restricted Compositional Convergence (`受限组合收敛 v1`)
- **Task-Handle Carrier Tracking**: Shapes and structs carrying `TaskHandle<T>` fields (directly or through nested carrier fields) preserve field-sensitive borrow and scope dependency metadata (`FieldDependencySet`) across initialization, projection, variable binding, reassignment, and helper calls within the verified subset.
- **Multi-Task Carrier Independence**: Carriers holding multiple `TaskHandle<T>` fields track field-level lifetime dependencies independently. Starting an owned task field (`h.t_owned.start`) on a carrier whose sibling field (`h.t_borrowed`) borrows a local buffer is permitted without false-positive rejection, while starting the borrowed field or a consuming expression that touches any borrowed field fails closed (`error[E04584]`).
- **Ordinary Method Receiver Ownership (`self#` vs `cede self#`)**:
  - Consuming ordinary `impl` methods declared with `cede self#` that start an owned internal task handle are statically verified against the receiver's field-level borrow provenance (`r5_pos1_cede_self_receiver_start`). Note: this fixes `cede self#` consumption on ordinary receiver methods and does **not** add consuming `cede self` async trait methods.
  - Borrowed receiver calls (`self#`) attempting to start an internal task field (`self.t.start` or `(self.t).start`) or `cede self#` calls on carriers holding any borrowed task field are rejected with `error[E04584]`.
  - Receiver methods with `effects: self <- t` cannot launder unjoined scope obligations across scope boundaries or bypass `join_scoped` / `cancel_and_join` convergence (`error[E0456]`).

### 2. Fail-Closed Escape & Scope Bypass Prevention
- **Detached Start on Borrowed Task & Carrier Paths (`error[E04584]`)**: Starting a task handle (or consuming carrier expression) that borrows local stack state without structured scope attachment is rejected across direct variables, split variables, unboxed field projections, nested carriers, field reassignments, and external `.tki` forwarders.
- **Carrier & Wrapper Scope Escape Prevention (`error[E0456]`)**:
  - Returning or escaping a carrier/wrapper containing a local-borrowing `TaskHandle<T>` outside its lexical scope is rejected (`error[E0455]` / `error[E0456]`).
  - Spawning a task into `scope1#` while joining only an unrelated `scope2#` (dual-scope laundering, including via carrier methods or `effects: self <- ...` wrappers) is rejected with `error[E0456]`.
  - Whole-carrier or field-level reassignment that overwrites an active unjoined scoped task handle is rejected with `error[E0456]`.
- **Bodyless `.tki` Fail-Closed Boundary (`error[E0456]`)**: Passing a local-borrowing `TaskHandle<T>` to a bodyless `.tki` function declaration (where the compiler cannot inspect the callee body to verify non-escape or scope convergence) fails closed at compile time (`error[E0456]`), whereas retained-body `.tki` functions (`checked-local-v1`) are verified and supported within the tested subset.
- **Zero-Artifact Multi-Mode Guarantee**: Tested negative escape and scope violations fail closed with exit code `1` and emit zero `.o`, `.ll`, or executable artifacts across `--check-only`, `-c`, `--emit-llvm`, and full link invocations.

### 3. Relationship & Compatibility with `v0.16.0`
- **Subsequent `0.1x` Development Release**: `v0.17.0` builds directly on `v0.16.0` Scoped Tasks (`显式汇聚受限 v1`) without introducing new syntax or claiming `1.0` stability. Published `v0.16.0` tags, original release archives, and historical qualification receipts remain immutable.
- **Verified `0.16.0` Baseline**: Verified `0.16.0` `TaskScope`, `spawn_scoped`, `join_scoped`, `cancel_and_join`, uninterruptible cancel drain barriers, cold forwarder functions (`fn forward(cede t: TaskHandle<T>) -> TaskHandle<T> <- t`), and standalone owned `TaskHandle<T>` workflows are retained as the regression baseline.
- **False-Rejection Fixes**: Valid programs that were previously over-rejected on multi-task carriers, `cede self#` ordinary receiver methods, or scalar/non-task struct dependency propagation now compile and execute within the verified subset.
- **Intentional Soundness Tightening & Recompilation Guidance**: Programs that relied on previously unguarded composite-carrier escape, nested field reassignment, `effects: self <- t` scope laundering, or bodyless `.tki` borrowed-task sinks are now rejected at compile time. Upgrading from `0.16.0` requires recompiling `.o` objects and `.tki` interfaces with `0.17.0` `tokac` and clearing stale build caches; blanket cross-version binary ABI compatibility is not claimed.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Continuous Dogfood**: Windows validation is maintained continuously and independently.
- **Installed Qualification**: Standard SDK packaging (`BUILD_TESTING=OFF`) and checkout-free artifact replay verify `0.17.0` compositional convergence and sourceless `.tki` controls directly on unpacked release archives.
