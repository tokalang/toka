# Toka 0.19.0 Release Notes

Toka 0.19.0 is a **breaking encapsulation contract migration release** on the `0.1x` development branch following `v0.18.0` and `v0.18.1`. Unlike a transparent patch release, `0.19.0` switches the default compiler encapsulation epoch (`TOKA_DEFAULT_ENCAP_EPOCH`) from `"0.18"` to `"0.19"`, making named `shape` fields **module-private (`private`) by default**, introducing the package-internal visibility modifier `pkg`, retiring `impl S@Encap { field_a, field_b }` field-visibility grant lists in favor of inline `shape` field modifiers (`pub` / `pkg`), and bumping the `.tki` interface format to **version 6** with strict epoch and cache isolation (`encap_epoch: 0.19`). As a pre-1.0 development release, Toka `0.19.0` does not claim `1.0` stability or cross-version binary ABI compatibility. Upgrading from `0.18.x` requires source migration for cross-module/cross-package shape fields and a full rebuild of all `.o`, `.tki`, and `.tke` artifacts after clearing compiler caches. Complete migration rules, diagnostic codes, and rollback instructions are documented in [`docs/compatibility_0.19.md`](compatibility_0.19.md) and [`docs/semantic_core/encap_current_contract.md`](semantic_core/encap_current_contract.md).

（Toka 0.19.0 是继 `v0.18.0` / `v0.18.1` 之后的**破坏性封装契约迁移版本**，并非对 `0.18` 无感的补丁更新。本版将编译器默认封装纪元 `TOKA_DEFAULT_ENCAP_EPOCH` 从 `"0.18"` 切换为 `"0.19"`：命名记录 `shape` 的字段在无显式修饰符时**默认为模块私有（`private`）**；新增包内可见性关键字 `pkg`；废除旧版 `impl S@Encap { a, b }` 字段授权列表语法，统一收敛至 `shape` 声明内联 `pub` / `pkg` 字段修饰符，仅保留 `impl S@Encap` 作为析构钩子 `drop` 载体；同时将 `.tki` 接口格式升级至 **Format Version 6** 并实行纪元与缓存强隔离（`encap_epoch: 0.19`）。从 `0.18.x` 升级需按契约迁移跨模块/跨包字段声明、清理全部编译缓存并重编所有依赖。详细迁移指引、独立 API 变更说明与 `v0.18.1` 回退办法见 [`docs/compatibility_0.19.md`](compatibility_0.19.md)。）

---

## Key Highlights & Breaking Changes

### 1. Inline `shape` Field Visibility & Default-Private Contract (`0.19` Encapsulation Epoch)

In `0.18.x`, fields of a `pub shape` without `@Encap` were publicly accessible by default, or granted via `pub <field>` inside an `impl S@Encap { ... }` block. Starting in `0.19.0`, **every named `shape` field defaults to module-private (`private`)** regardless of whether the enclosing `shape` is `pub`, `pkg`, or module-private.

#### Old (`0.18.x`) vs. New (`0.19.0`) Syntax

```toka
// ====================================================================
// Old (0.18.x): @Encap listed public/granted fields alongside drop
// ====================================================================
pub shape Account(
    id: i32,
    branch: i32,
    balance: i64
)

impl Account@Encap {
    pub id, branch    // REJECTED in 0.19.0 with error[E0406] / error[E0457]
    fn drop(self#) { /* cleanup */ }
}

// ====================================================================
// New (0.19.0): fields are module-private by default; annotate inline
// ====================================================================
pub shape Account(
    pub id: i32,      // accessible across all packages and modules
    pkg branch: i32,  // accessible only within the same authoritative package
    balance: i64      // no modifier => module-private (private)
)

impl Account@Encap {
    // @Encap now carries ONLY the private drop lifecycle hook (no field list, no pub/pkg on drop)
    fn drop(self#) { /* cleanup */ }
}
```

- **Modifier Selection Discipline (`pub` vs. `pkg` vs. Default Private)**:
  - Omit modifiers (default `private`) for internal invariants, raw pointers, handles, and representation details confined to the defining module.
  - Use **`pkg`** only for fields/declarations that genuinely participate in multi-module collaboration **within the same authoritative package**.
  - Use **`pub`** only for fields/declarations that belong to the external cross-package contract. Do **not** blindly convert all fields to `pub` during migration.
- **Preserved Non-Record Semantics (`R2` Boundary)**:
  - **`enum` variants and variant payloads** remain governed by the enclosing `enum`'s visibility (`pub enum` / `pkg enum` / private `enum`) and do **not** accept per-payload `pub`/`pkg` modifiers (`error[E0406]` / `error[E0457]`).
  - **Canonical fixed-size array shapes** (`shape Name([T; N])`) and **`enum` variants** are unchanged; legacy anonymous tuple-shape syntax (`shape Pair(i32, i32)`) remains removed and rejected with `error[E0472]`.
  - **`Copy` / `Dup` value semantics, receiver borrow dependency tracking (`0.17.1`), `cede self` static async traits (`0.18.0`), and `str`-valued `match` provenance (`0.18.1`)** remain intact and are not altered by the field visibility default switch.

### 2. Five-Path Access Enforcement & Authoritative Package Identity (`pkg`)

Field and declaration visibility (`pub`, `pkg`, `private`) is enforced uniformly across all access paths:

| Access Path | Example | Required Visibility | Diagnostic on Violation |
| :--- | :--- | :--- | :--- |
| **Field Read** | `auto v = s.x` | Accessed field visible in caller scope | `error[E0418]` (or `error[E0456]`) |
| **Inherent Method Call** | `s.helper()` | Called inherent method visible in caller scope | `error[E0437]` |
| **Unexported Symbol / Private Type Import** | `import m::{PrivateShape}` | Imported top-level symbol/type visible in caller scope | `error[E0405]` (or `error[E0419]`) |
| **Field Write & Mutable Borrow (`&` / `~` / `#`)** | `s#.x = 10` / `&s.x` / `~s#.x` | Field visible in caller scope (orthogonally combined with mutability) | `error[E0418]` (or `error[E0456]`) |
| **Direct Positional / Named Construction** | `S(1, 2)` / `S(x = 1, y = 2)` | **Every** field of `S` visible in caller scope | `error[E0418]` (or `error[E0456]`) |
| **Default-Value Field Elision (`..`)** | `S(x = 1, ..)` | Every **elided** default field visible in caller scope | `error[E0418]` (or `error[E0456]`) |
| **Pattern Destructuring (`let` / `match`)** | `auto S(a = .x, ..) = s` | Every destructured or wildcard-skipped field visible in caller scope | `error[E0418]` (or `error[E0456]`) |

- **Authoritative Package Boundary (`R1` / `PLAN-1`)**:
  - `pkg` access is granted **if and only if** the defining module and caller module resolve to the same authoritative package coordinate (`package.tk` root or `--workspace-node` + `--workspace-root` binding).
  - Sharing a parent filesystem directory without a package boundary, or declaring two separate packages with the same `name` in different directories, does **not** grant `pkg` access.
  - Invoking `tokac` on a module that exercises `pkg` access without an authoritative package coordinate fails closed (`error[E0418]` / `error[E0437]` / `error[E0405]`).
- **Effective Visibility Closure & `@Encap` Discipline (`PLAN-2`, `error[E0406]` / `error[E0457]`)**:
  - A `pub` shape field, `pub fn` parameter/return type, `pub const`, or `pub type` alias cannot reference a `pkg` or module-private type (`error[E0406]` / `error[E0457]`).
  - A `pkg` shape field, `pkg fn` signature, `pkg const`, or `pkg type` alias cannot reference a module-private type (`error[E0406]` / `error[E0457]`).
  - Legacy `@Encap(field)` grant lists and non-private `pub fn drop` / `pkg fn drop` inside `impl S@Encap` are rejected with `error[E0406]` / `error[E0457]`.
- **No `pub import` / `pkg import` Re-Exports (`error[E0406]` / `error[E0457]`)**:
  - Visibility modifiers on `import` statements are rejected. Cross-module APIs must be imported from their authoritative defining module.
- **Generic Specialization Visibility**:
  - Generic bodies are checked against the generic definition module's authority (`PG-01` / `PG-02`), while concrete shape method dispatch (`T::method`) instantiated on behalf of a caller validates visibility against the caller's instantiation scope. Whole-value passing, copying, moving, and dropping of a visible `shape` across modules/packages remain permitted even when the `shape` contains private fields (`PG-03` / `PG-04`).

### 3. Standalone Public API Surface Change (`stdx/text/template::TemplateListItem`)

`0.19.0` does **not** claim that every standard library symbol signature is 100% identical to `0.18.1`. To satisfy the `PLAN-2` effective visibility closure rule (`error[E0406]` / `error[E0457]`) for `pub shape TemplateContext` (which exposes `pub lists: Vec<TemplateListItem>`), the following symbol in `lib/stdx/text/template.tk:34` has been promoted from module-private to `pub`:

| Module | File | Symbol | `0.18.1` Declaration | `0.19.0` Declaration |
| :--- | :--- | :--- | :--- | :--- |
| `stdx/text/template` | `lib/stdx/text/template.tk:34` | `TemplateListItem` | `shape TemplateListItem(key: string, value: string, has_value: bool)` | `pub shape TemplateListItem(pub key: string, pub value: string, pub has_value: bool)` |

### 4. TKI Format Version 6, Epoch Isolation, and Cache Rebuild Requirements

- **`.tki` Format Version 6**:
  - `include/toka/InterfaceVersion.h` bumps `TOKA_INTERFACE_FORMAT_VERSION` from `5` to `6` and emits `@meta format_version: 6` and `@meta encap_epoch: 0.19` in every generated `.tki` file.
  - Note: the low-level compiler interface key `TOKA_COMPILER_INTERFACE_VERSION` (`0.9.9-38`) remains unchanged; format and epoch isolation are enforced directly via `format_version: 6` and `encap_epoch: 0.19`.
- **Fail-Closed Interface & Cache Isolation (`error[E0901]`)**:
  - Consuming a `v0.18.x` Format 5 `.tki` file, a `.tki` file missing `encap_epoch`, or a Format 6 `.tki` emitted with `encap_epoch: 0.18` under the default `0.19.0` compiler is rejected with `error[E0901]` in sourceless builds, or triggers automatic cache invalidation and source recompilation when `.tk` sources are present (`TOKA_BUILD_DIR` / `TOKA_USE_LIB_CACHE=1`).
- **Actionable Upgrade & Cache Rebuild Steps**:
  1. Ensure all `tokac`, `toka`, `tokafmt`, and `tokalsp` binaries on `PATH` report `0.19.0`.
  2. Delete all existing build/cache directories (`rm -rf build/ .toka-cache/ "$TOKA_BUILD_DIR"` and any prebuilt `.tki`, `.o`, `.tke` artifacts from `0.18.x`).
  3. Migrate cross-module/cross-package `shape` field declarations to inline `pub` or `pkg` modifiers and remove field lists from `impl S@Encap` blocks.
  4. Recompile upstream dependencies with `tokac -c --emit-interface` before building downstream consumers.
- **Status of Temporary `--encap-epoch=0.18` Switch**:
  - `tokac` retains `--encap-epoch=0.18` solely as a temporary compiler-level transition switch for isolated legacy syntax/diagnostic regression tests during the `0.19.x` cycle.
  - **Scope Limitation**: `--encap-epoch=0.18` is **not** a full `0.18` SDK compatibility mode—the `0.19.0` standard library (`lib/std`, `lib/stdx`) and prebuilt `.tki` interfaces are migrated to `0.19` (`encap_epoch: 0.19`) and cannot be mixed with `--encap-epoch=0.18` compilation units (`error[E0901]`). `--encap-epoch=0.18` is scheduled for removal in `0.20+`.

---

## Platform Policy, `0.18.1` Maintenance Baseline & Rollback Guidance

- **Tier 1 (Core Blocking Release Platforms)**:
  - **`linux-x64`** (Ubuntu 22.04, Clang / LLVM 20.1.8)
  - **`linux-arm64`** (Ubuntu 24.04 ARM64, Clang / LLVM 20.1.8)
  - **`macos-arm64`** (macOS 15 Apple Silicon, Clang / LLVM 20.1.8)
  - Official release archives (`toka-v0.19.0-<target>.tar.gz`) and `SHA256SUMS` are published exclusively for Tier 1 targets under policy `toka.release-platforms.0.19.v1`.
- **Tier 2 (Continuous Dogfood Target)**:
  - **`windows-x64`** (MSYS2 / MinGW-w64 Clang 20.1.8 SDK path, validated via `.github/workflows/windows-dogfood.yml`; non-blocking for Tier 1 release archive publication).
- **`0.18` Line Maintenance & Rollback Policy**:
  - Published `v0.18.1` Git tags, release tarballs (`toka-v0.18.1-<target>.tar.gz`), `SHA256SUMS`, and qualification receipts remain **immutable** and serve as the official rollback baseline for `0.18` projects.
  - The `0.18` line is **feature-frozen** and receives **on-demand maintenance only** (restricted to critical correctness/security defects or severe installation regressions upon explicit approval; no indefinite dual-line feature development and no permanent LTS guarantee).
  - **Pinned Installation & Rollback Warning**: Projects that must stay on or roll back to `v0.18.1` must pin the `v0.18.1` release archive (`toka-v0.18.1-<target>.tar.gz`) **together with `0.18.1`-compatible source code, dependencies, and freshly rebuilt `0.18.1` caches**. Swapping a `v0.18.1` binary onto source files already migrated to `0.19` (`pkg` modifiers or inline `pub` field syntax) or onto `0.19` Format 6 `.tki` caches will fail to parse/compile.
