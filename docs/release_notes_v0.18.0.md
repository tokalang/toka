# Toka 0.18.0 Release Notes

Toka 0.18.0 is the consuming static async trait method release following `0.17.0` Compositional Consistency (`受限组合收敛 v1`). This release admits consuming `cede self` static async trait methods (`trait @T { fn m(cede self) -> async R }`) and their implementations, enabling canonical lifecycle completion patterns such as `close(cede self) -> async Result<T, E>`. Receiver borrow dependency propagation and escape analysis established in v0.17.1 are fully preserved without regression. No explicit lifetime annotation syntax is introduced, existing `E0617` / `E0618` restrictions on unrelaxed receiver forms remain enforced, and external utilities (`trg` and `tokakv`) remain untouched. Source and interface migration guidance follows `docs/compatibility_0.18.md`.

（Toka 0.18.0 是继 0.17.0 受限组合收敛之后的消费型静态 async trait 方法版本。本版精确放开 `cede self` 静态 async trait 方法声明与实现（如 `close(cede self) -> async Result<T,E>` 经典消费形态），并在编译器语义分析（Sema）、代码生成（CodeGen）及无源码 `.tki` 接口跨模块重放中完成端到端本地功能闭环。完整复用并守恒 v0.17.1 借用依赖传播与逃逸检查；不引入显式生命周期语法；不放宽 `E0617` 等既有拒绝规则；不触碰 `trg` 与 `tokakv`。）

## Key Highlights

### 1. Consuming Static Async Trait Methods (`cede self` Async Trait)
- **Trait Declaration & Implementation**:
  - Traits may declare consuming asynchronous methods taking `cede self`:
    ```toka
    pub trait @AsyncClose {
        pub fn close(cede self) -> async Result<i32, CloseError>
    }
    ```
  - Types implement `@AsyncClose` with exact receiver and signature matching (`impl Carrier@AsyncClose { pub fn close(cede self) -> async Result<i32, CloseError> { ... } }`).
- **Direct & Generic Consumption**:
  - Direct consumption via `.close().await` consumes the receiver, moving ownership into the coroutine frame.
  - Generic dispatch functions (`fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError>`) monomorphize and invoke consuming methods with static dispatch, verifying receiver ownership transfer and frame destruction.
- **7-Lifecycle-Path Verified Robustness**:
  - Verified across seven complete execution and cleanup paths: normal completion, secondary concrete type generic dispatch, pre-suspend `!` early error return, post-suspend `!` error return, cold `TaskHandle` direct drop without outer `@Encap`, caught cancellation (`await?`), and uncaught cancellation (`race2`).
  - Native runtime tracking verifies zero leaks (`g_live == 0`) and deterministic destruction (`g_drops == 12`).

### 2. Upstream Borrow Dependency & Escape Conservation
- **Preserved v0.17.1 Invariants**: The compiler tracks field-level borrow provenance through receiver bindings and coroutine parameters. Consuming an instance containing a local borrow inside a coroutine frame propagates dependency obligations upstream and prevents stack frame escapes.
- **Fail-Closed Rejections (Zero-Artifact Guarantee)**:
  - **Forged Same-Name Wrappers**: Bypassing consumption through unverified top-level `block_on(h)` without `cede` or detached `.start` fails closed (`error[E0456]`). Trusted standard library execution (`std/task::block_on`) is verified via authenticated module identity (`isTrustedStdTaskBlockOn`).
  - **Untrusted Forwarding**: In-body `.start` detachment, synchronous forwarding without `<- x`, or asynchronous helper detachment fails closed (`error[E0456]` / `error[E0454]`).
  - **Local Borrow Escape**: Returning or escaping a local borrow through async trait return types or carrier projections fails closed (`error[E0455]` / `error[E04584]`).
  - **Composite Reference Carrier (`RefCarrier`) Escape**: Passing `RefCarrier(&view: i32, token: i32)` into detached async scopes fails closed (`error[E0455]` / `error[E04584]`).
  - **Cross-Module `.tki` Escape**: Bodyless or untrusted `.tki` declarations attempting to consume or forward borrowed receivers fail closed (`error[E0456]`).
  - **Zero Leftover Artifacts**: All negative probes emit exit code `1` with zero `.o`, `.ll`, or binary artifacts across `--check-only`, `-c`, and `--emit-llvm` modes.

### 3. Strict Boundary & Non-Relaxation Invariants
- **Unrelaxed Receiver Forms (`error[E0618]`)**:
  - Mutable borrow `cede self#` on async trait methods is rejected (`error[E0618]`).
  - Constructor-like `init cede self` on async trait methods is rejected (`error[E0618]`).
  - Value receiver `plain self` without `cede` is rejected (`error[E0618]`).
  - Shared borrow `self: &Self` and mutable borrow `self: ~Self` on async trait methods remain rejected (`error[E0618]`).
  - Static methods on async traits without receiver (`fn new() -> async R`) remain rejected (`error[E0618]`).
- **Dynamic Dispatch Rejection (`error[E0617]`)**:
  - `dyn @AsyncTrait` invocation remains rejected; trait object dispatch on async traits is not admitted.
- **No Lifetime Syntax**: Toka 0.18.0 introduces no explicit lifetime syntax (`'a`, etc.); lifetime safety continues to rely on lexical structured concurrency, explicit cede linear transfers, and compiler flow analysis.
- **Tooling Integrity**: No changes are introduced to `trg` or `tokakv`.

### 4. Cross-Platform Qualification
- Verified with native compilation, linkage, and execution (`rc == 0`) across all three core release platforms:
  - **`macos-arm64`** (Native Apple Silicon, Clang / LLVM 20.1.8)
  - **`linux-arm64`** (Docker `linux/arm64`, Clang 20.1.8)
  - **`linux-x64`** (Docker `linux/amd64`, Clang 20.1.8)
- Continuous validation wired into PR CI gate (`.github/workflows/ci.yml`) and Windows dogfood (`.github/workflows/windows-dogfood.yml`).

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Standard SDK Packaging**: Build tree packaging (`BUILD_TESTING=OFF`) produces standard archives (`toka-v0.18.0-<target>.tar.gz`) bound to candidate revision, version label, and clean git state.
- **Checkout-Free Qualification**: Packaging smoke and artifact replay verify the complete 0.18 cede self control suite directly on unpacked release archives.
