# Toka 0.19.0 Compatibility Notes, Migration Guide & SDK Acceptance Checklist

## 1. Release Positioning & Breaking Contract Overview

- **Breaking Contract Migration (`0.18` -> `0.19` Default Epoch)**:
  - Toka `0.19.0` switches `TOKA_DEFAULT_ENCAP_EPOCH` from `"0.18"` to `"0.19"` (`include/toka/InterfaceVersion.h`).
  - Named `shape` fields without an explicit visibility modifier now default to **module-private (`private`)** instead of inheriting the enclosing `shape`'s visibility.
  - Inline field visibility modifiers (`pub` and `pkg`) on `shape` declarations replace the legacy `impl S@Encap { field_a, field_b }` field-list syntax.
  - `.tki` interface artifacts are bumped to **Format Version 6** (`TOKA_INTERFACE_FORMAT_VERSION = 6`) and carry `@meta encap_epoch: 0.19`.
- **No `1.0` Stability or Cross-Version ABI Claim**:
  - Toka `0.19.0` is an iterative pre-1.0 development release on the `0.1x` line and does not provide cross-version binary (`.o`) or interface (`.tki`) compatibility with `0.18.x` or earlier releases.
- **Preserved Language & Runtime Invariants**:
  - **Non-Record Types (`R2`)**: `enum` variants and variant payloads, and canonical fixed-size array shapes (`shape Name([T; N])`) are unchanged; legacy anonymous tuple-shape syntax (`shape Pair(i32, i32)`) remains removed and rejected with `error[E0472]`. `enum` variant payloads do not take `pub`/`pkg` modifiers (`error[E0406]` / `error[E0457]`).
  - **Value & Ownership Semantics**: `Copy` / `Dup` derivation, explicit `cede` ownership transfers, receiver borrow dependency propagation (`0.17.1`), `cede self` consuming static async traits (`0.18.0`), and `str`-valued `match` expression provenance (`0.18.1`) are preserved without regression.
  - **External Tooling**: No changes are made to `trg` or `tokakv`.

---

## 2. Source Migration Guide (`0.18.x` -> `0.19.0`)

### 2.1 Migrating `shape` Field Declarations & `@Encap` Blocks

In `0.18.x`, an `impl S@Encap { ... }` block listed the granted/public fields alongside `drop`. In `0.19.0`, field visibility is declared **inline on the `shape`** and field lists (or non-private `pub`/`pkg` `drop` modifiers) inside `impl S@Encap` are rejected (`error[E0406]` / `error[E0457]`):

```toka
// BEFORE (0.18.x)
pub shape ServerConfig(
    host: string,
    port: i32 = 8080,
    internal_token: i64 = 0
)

impl ServerConfig@Encap {
    pub host, port
    fn drop(self#) {
        // custom destructor logic
    }
}

// AFTER (0.19.0)
pub shape ServerConfig(
    pub host: string,         // cross-package public field
    pkg port: i32 = 8080,     // package-internal field (only same package)
    internal_token: i64 = 0   // module-private field (no modifier)
)

impl ServerConfig@Encap {
    // Only a private drop lifecycle hook is permitted inside @Encap in 0.19.0
    fn drop(self#) {
        // custom destructor logic
    }
}
```

> **Migration Best Practice (`pub` vs. `pkg` vs. Default `private`)**:
> - Use **`pub`** only for fields that external packages are meant to read, write, construct, or destructure directly.
> - Use **`pkg`** for fields shared across modules inside the **same authoritative package** but hidden from external consumers.
> - Leave internal representation fields **unannotated (`private`)** and provide `pub fn` / `pkg fn` constructors or accessor methods where needed. Do not blindly mark every field `pub`.

### 2.2 Field, Method & Type Visibility Rules & Diagnostics

Under the default `0.19.0` compiler entry, any cross-module or cross-package operation checks visibility:

1. **Field Read (`error[E0418]` / `error[E0456]`) & Inherent Method Call (`error[E0437]`)**:
   - Reading `s.field` requires `field` to be visible (`pub` across packages; `pub` or `pkg` within the same authoritative package), otherwise rejected with `error[E0418]` (or `error[E0456]`).
   - Calling an inherent method `s.method()` or `S::method(...)` requires the method to be visible in the caller's scope, otherwise rejected with `error[E0437]`.
   - Importing an unexported top-level symbol or accessing a private / cross-package `pkg` type is rejected with `error[E0405]` (or `error[E0419]`).
2. **Field Write & Mutable/Shared Borrows (`error[E0418]` / `error[E0456]`)**:
   - Mutating `s#.field = val` or borrowing `&s.field` / `~s#.field` requires both field visibility and the appropriate receiver/binding mutability (`error[E0418]` / `error[E0456]`).
3. **Direct Construction (`S(...)`, `error[E0418]` / `error[E0456]`)**:
   - Constructing `S(...)` directly (positionally or by named fields) outside `S`'s defining module is permitted **only if every field of `S` is visible** to the caller. If `S` has any private (or cross-package `pkg`) field, direct construction is rejected with `error[E0418]` (or `error[E0456]`) and external callers must use a visible constructor function such as `S::new(...)`.
4. **Default-Value Field Elision (`S(a = 1, ..)`, `error[E0418]` / `error[E0456]`)**:
   - Omitting defaulted fields via `..` requires **every elided field** to be visible to the caller. Eliding a private or inaccessible `pkg` field with `..` is rejected with `error[E0418]` (or `error[E0456]`).
5. **Pattern Destructuring (`let S(...) = ...` / `match`, `error[E0418]` / `error[E0456]`)**:
   - Destructuring a `shape` pattern requires all bound fields—and any fields skipped via `..`—to be visible to the caller (`error[E0418]` / `error[E0456]`).

### 2.3 Authoritative Package Identity (`R1` / `PLAN-1`) & Effective Visibility Closure (`PLAN-2`)

- **Authoritative Package Root Required for `pkg`**:
  - `pkg` visibility is determined strictly by the module's authoritative package identity (`package.tk` or `--workspace-node <id> --workspace-root <dir>`).
  - Files in the same directory or sibling subdirectories without an authoritative package root do **not** share `pkg` access (fails closed with `error[E0418]` / `error[E0437]` / `error[E0405]`).
  - Two distinct package roots that happen to declare the same package `name` in `package.tk` have distinct package identities and cannot access each other's `pkg` symbols (`error[E0418]` / `error[E0437]` / `error[E0405]`).
- **Effective Visibility Closure & `@Encap` Discipline (`PLAN-2`, `error[E0406]` / `error[E0457]`)**:
  - Public APIs (`pub shape` fields, `pub fn` parameters/returns, `pub const`, `pub type`) cannot expose `pkg` or module-private types (`error[E0406]` / `error[E0457]`).
  - Package-internal APIs (`pkg shape` fields, `pkg fn` parameters/returns, `pkg const`, `pkg type`) cannot expose module-private types (`error[E0406]` / `error[E0457]`).
  - Legacy `impl S@Encap(field)` grant lists and non-private `pub fn drop` / `pkg fn drop` inside `impl S@Encap` are rejected with `error[E0406]` / `error[E0457]`.
  - Outer type visibility caps member effective visibility: a `pub` field inside a `pkg shape` has effective visibility at most `pkg`.
- **No Re-Export Modifiers on `import` (`error[E0406]` / `error[E0457]`)**:
  - `pub import` and `pkg import` are rejected with `error[E0406]` / `error[E0457]`.

---

## 3. Standalone Public API Surface Change in `0.19.0`

To satisfy `PLAN-2` effective visibility closure (`error[E0406]` / `error[E0457]`) for `pub shape TemplateContext` (`pub lists: Vec<TemplateListItem>`), `stdx/text/template::TemplateListItem` (`lib/stdx/text/template.tk:34`) is promoted from a module-private `shape` to a `pub shape` with `pub` fields:

```toka
// lib/stdx/text/template.tk:34
// 0.18.1: shape TemplateListItem(key: string, value: string, has_value: bool)
// 0.19.0:
pub shape TemplateListItem(
    pub key: string,
    pub value: string,
    pub has_value: bool
)
```

All other standard library (`lib/std`, `lib/stdx`) public contracts preserve their intended public vs. internal boundaries under inline `0.19` field visibility.

---

## 4. `.tki` Format 6, Cache Isolation & Dependency Rebuild Procedure

### 4.1 `.tki` Metadata Contract

| Header Key | `0.18.1` Value | `0.19.0` Value | Enforcement |
| :--- | :--- | :--- | :--- |
| `compiler_version` | `0.9.9-38` | `0.9.9-38` | Low-level compiler interface key (`TOKA_COMPILER_INTERFACE_VERSION`, unchanged) |
| `format_version` | `5` | `6` | Hard rejection (`error[E0901]: Interface format version mismatch (expected 6, got 5)`) |
| `encap_epoch` | *(absent)* | `0.19` | Hard rejection on missing or mismatched epoch (`error[E0901]: Interface encap_epoch mismatch`) |

### 4.2 Dependency & Cache Rebuild Procedure

When upgrading a project or workspace from `0.18.x` to `0.19.0`:

1. **Verify Tool Identities**:
   ```bash
   tokac --version    # must report 0.19.0
   toka --version     # must report 0.19.0
   tokafmt --version  # must report 0.19.0
   tokalsp --version  # must report 0.19.0
   ```
2. **Purge Stale Caches & Prebuilt Artifacts**:
   - Remove all `.tki`, `.o`, and `.tke` files generated by `0.18.x` (`rm -rf build/ .toka-cache/` and any custom `TOKA_BUILD_DIR`).
   - Do **not** mix `0.18.x` `.tki` or `.o` artifacts with `0.19.0` compilation units.
3. **Rebuild Upstream Modules First**:
   - Recompile source-hidden or prebuilt library modules with `tokac -c --emit-interface` so their `.tki` files record `format_version: 6` and `encap_epoch: 0.19` along with `pub` / `pkg` / private field declarations.
4. **Temporary `--encap-epoch=0.18` Switch Scope**:
   - `--encap-epoch=0.18` is retained in `0.19.0` **only** for isolated compiler-level migration diagnostics and legacy syntax regression tests.
   - It does **not** allow linking or importing the `0.19.0` standard library `.tki` cache under `0.18` rules (`error[E0901]`), and is scheduled for removal in `0.20+`.

---

## 5. Platform Tiering, `0.18.1` Maintenance Policy & Rollback Procedure

### 5.1 Supported Platforms (`toka.release-platforms.0.19.v1`)

| Tier | Platform Label | Environment | Release Status |
| :--- | :--- | :--- | :--- |
| **Tier 1 (Core Blocking)** | `linux-x64` | Ubuntu 22.04 x86_64, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.19.0-linux-x64.tar.gz`) |
| **Tier 1 (Core Blocking)** | `linux-arm64` | Ubuntu 24.04 ARM64, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.19.0-linux-arm64.tar.gz`) |
| **Tier 1 (Core Blocking)** | `macos-arm64` | macOS 15 Apple Silicon, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.19.0-macos-arm64.tar.gz`) |
| **Tier 2 (Dogfood)** | `windows-x64` | Windows x64 MSYS2/MinGW-w64 Clang 20.1.8 | Continuous CI dogfood gate; no standalone release tarball |

### 5.2 `0.18.1` On-Demand Maintenance & Pinned Rollback Procedure

- **`0.18.1` Maintenance Policy**:
  - The published `v0.18.1` release (`toka-v0.18.1-<target>.tar.gz`, `SHA256SUMS`, Git tag `v0.18.1`, and qualification receipts) remains **immutable** as the rollback baseline for `0.18` users.
  - The `0.18` series is feature-frozen and maintained **strictly on demand** for critical correctness, security, or installation regressions; it does not carry an open-ended dual-line development or permanent LTS commitment.
- **How to Pin or Roll Back to `v0.18.1`**:
  1. Download the pinned `v0.18.1` archive for your platform (`toka-v0.18.1-linux-x64.tar.gz`, `toka-v0.18.1-linux-arm64.tar.gz`, or `toka-v0.18.1-macos-arm64.tar.gz`) and verify its SHA-256 against the `v0.18.1` `SHA256SUMS`.
  2. Check out the `0.18.1`-compatible revision of your project source and dependencies (before `pkg` or inline `pub` shape field migration). **A `v0.18.1` compiler binary cannot parse `0.19` inline field visibility syntax (`pub`/`pkg` on shape fields) or `Format Version 6` `.tki` files.**
  3. Clear any `0.19.0` build/cache directories (`build/`, `TOKA_BUILD_DIR`) and rebuild cleanly with the `v0.18.1` toolchain.

---

## 6. SDK Acceptance Checklist (`v0.19.0`)

1. **Tool Version Identities**:
   - `tokac --version` reports `0.19.0`
   - `toka --version` reports `0.19.0`
   - `tokafmt --version` reports `0.19.0`
   - `tokalsp --version` reports `0.19.0`
2. **Standard SDK Archive (`BUILD_TESTING=OFF`)**:
   - Archive name `toka-v0.19.0-<target>.tar.gz` contains `bin/`, `lib/`, `include/`, `docs/`, and `sdk.json`.
   - `sdk.json` binds `candidate_revision`, `version_label: "v0.19.0"`, `source_dirty: false`, `build_testing: false`, and all four tool SHA-256 digests.
   - `docs/` includes `release_notes_v0.19.0.md` and `compatibility_0.19.md`.
3. **Installed Candidate & Replay Controls (`toka.0.19-candidate-controls`)**:
   - Default compiler entry (`TOKA_DEFAULT_ENCAP_EPOCH = "0.19"`, without passing `--encap-epoch=0.19`) enforces all 27 four-path encapsulation & visibility items (`SP-01`..`SP-08`, `XP-01`..`XP-09`, `PG-01`..`PG-04`, `TKI-01`..`TKI-06`).
   - Emitted `.tki` interfaces record `format_version: 6` and `encap_epoch: 0.19`; missing, corrupt, Format 5, and `encap_epoch: 0.18` interfaces are rejected fail-closed (`error[E0901]`).
   - All negative probes reject with exit code `1`, expected diagnostic code, and zero residual `.o`/`.ll` artifacts across `--check-only`, `-c`, and `--emit-llvm`.
   - All positive probes compile, link, and execute natively with `rc == 0`.
   - All 60 compositional convergence regression probes pass (`rc == 0`).
   - All 143 CTest suites pass (`0` failed).
