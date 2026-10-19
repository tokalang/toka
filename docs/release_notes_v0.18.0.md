# Toka 0.18.0 Release Notes

Toka 0.18.0 is an iterative development release on the `0.1x` branch following `0.17.0` Compositional Consistency (`受限组合收敛 v1`) and the `v0.17.1` patch. As a pre-1.0 development release, it does not claim `1.0` stability and does not guarantee blanket cross-version binary ABI compatibility. This release strictly admits consuming `cede self` static async trait methods (`trait @T { fn m(cede self) -> async R }`) and their monomorphized static implementations, enabling canonical lifecycle completion patterns such as `close(cede self) -> async Result<T, E>`. The strict mutable receiver `self#`, admitted by 0.14 G1, remains supported. Plain `self`, explicit handle receivers such as `self: &Self` and `self: ~Self`, the combined form `cede self#`, constructor variants (`init cede self`), and receiverless async trait methods remain rejected; dynamic async traits (`dyn @Trait`) also remain unsupported. Receiver borrow dependency propagation and escape analysis established in v0.17.1 are fully preserved without regression. No explicit lifetime annotation syntax is introduced, existing `E0617` / `E0618` restrictions on unrelaxed receiver forms remain strictly enforced, historical assets remain immutable, and external utilities (`trg` and `tokakv`) remain untouched. Upgrading from `0.17.x` requires full recompilation of `.o` and `.tki` artifacts with compiler cache clearing. Source and interface migration guidance follows `docs/compatibility_0.18.md`.

（Toka 0.18.0 是继 0.17.0 受限组合收敛及 v0.17.1 补丁之后的 `0.1x` 开发线迭代版本。作为 1.0 前的开发期版本，本版不作 1.0 级别稳定性承诺，不保证跨版本的通用二进制 ABI 兼容性。本版严格且精确地放开单所有权消费型 `cede self` 静态 async trait 方法声明与单态化静态实现（如 `close(cede self) -> async Result<T,E>` 经典生命周期终结形态），继续支持 0.14 G1 已放开的严格可变 receiver `self#`；普通 `self`、`self: &Self` / `self: ~Self` 等显式 handle receiver、组合形式 `cede self#`、构造器变体 `init cede self` 及无 receiver 的 async trait 方法仍被拒绝，动态 async trait 分发（`dyn @Trait`）也仍未开放。在编译器语义分析（Sema）、代码生成（CodeGen）及无源码 `.tki` 接口跨模块重放中完成端到端本地功能闭环。完整复用并守恒 v0.17.1 借用依赖传播与逃逸检查；不引入显式生命周期语法；严格维持 `E0617` / `E0618` 既有拒绝边界；历史发布资产保持不可变；不触碰 `trg` 与 `tokakv`。跨版本升级需全量重新编译 `.o` 与 `.tki` 并清理编译缓存。接口与源码迁移指引详见 `docs/compatibility_0.18.md`。）

## Key Highlights

### 1. Consuming Static Async Trait Methods (`cede self` Async Trait)

This feature is the **0.18 consuming static async trait slice**, historically
called “0.18 Stage 1” in verification identifiers. That label is local to this
release slice. The existing **0.14 G1 static mutable-receiver async trait** slice
remains supported; see the [complete receiver matrix](compatibility_0.18.md#2-admitted-language-forms-in-0180).

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
  - Generic dispatch functions (`fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError>`) monomorphize and invoke consuming methods with static dispatch, verifying receiver ownership transfer and frame destruction. Trait object dispatch (`dyn @AsyncClose`) remains strictly disallowed (`error[E0617]`).
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
  - The combined consuming-and-mutable form `cede self#` on async trait methods is rejected (`error[E0618]`).
  - Constructor-like `init cede self` on async trait methods is rejected (`error[E0618]`).
  - Value receiver `plain self` without `cede` is rejected (`error[E0618]`).
  - Explicit reference `self: &Self` and shared-owner handle `self: ~Self` on async trait methods remain rejected (`error[E0618]`).
  - Static methods on async traits without receiver (`fn new() -> async R`) remain rejected (`error[E0618]`).
- **Dynamic Dispatch Rejection (`error[E0617]`)**:
  - `dyn @AsyncTrait` invocation remains rejected; dynamic trait object dispatch on async traits is strictly disallowed.
- **No Lifetime Syntax**: Toka 0.18.0 introduces no explicit lifetime syntax (`'a`, etc.); lifetime safety continues to rely on lexical structured concurrency, explicit cede linear transfers, and compiler flow analysis.
- **Tooling Integrity & Asset Immutability**: No changes are introduced to `trg` or `tokakv`. All historical git tags, release archives, and qualification receipts for `v0.17.0` and earlier versions remain immutable.

### 4. Cross-Platform Qualification & Platform Tiering
- **Tier 1 (Core Blocking Release Targets)**: Verified with native compilation, linkage, and execution (`rc == 0`) across all three core release platforms:
  - **`macos-arm64`** (Native Apple Silicon, Clang / LLVM 20.1.8)
  - **`linux-arm64`** (Docker `linux/arm64`, Clang 20.1.8)
  - **`linux-x64`** (Docker `linux/amd64`, Clang 20.1.8)
- **Tier 2 (Continuous Dogfood & Compatibility Target)**:
  - **`windows-x64`** (MSYS2 / MinGW-w64 Clang 20.1.8 SDK path, verified via `.github/workflows/windows-dogfood.yml`; validated for ecosystem dogfooding, non-blocking for Tier 1 standard archive release).
- Continuous validation wired into PR CI gate (`.github/workflows/ci.yml`) and Native Windows dogfood (`.github/workflows/windows-dogfood.yml`).

## Release & Platform Policy
- **Core Blocking Platforms (Tier 1)**: Linux x64, Linux ARM64, and macOS ARM64. Standard release archives are published exclusively for Tier 1 platforms.
- **Dogfood Platform (Tier 2)**: Windows x64 is verified for toolchain correctness and dogfood stability but is not packaged as a primary release binary.
- **Standard SDK Packaging**: Build tree packaging (`BUILD_TESTING=OFF`) produces standard archives (`toka-v0.18.0-<target>.tar.gz`) bound to candidate revision, version label, and clean git state.
- **Checkout-Free Qualification**: Packaging smoke and artifact replay verify the complete 0.18 cede self control suite directly on unpacked release archives.
- **Full Test Suite Baseline**: All 141 full CTest suites pass 100% (141/141 PASS, 0 failed), 29 negative probes pass across 3 compilation modes (87 commands, zero artifacts), 75 contract mutation test cases pass fail-closed, 60 compositional convergence regression probes pass, and 110 command execution receipts validated with 0 errors.
