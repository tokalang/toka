# Toka 1.0 Scope & Qualification Layer Definition

**Status**: Qualification Layer Specification  
**Authority Hierarchy**:
1. [`docs/1_0_freeze_decision_list.md`](1_0_freeze_decision_list.md) and [`docs/1_0_closure_plan.md`](1_0_closure_plan.md) constitute the **Normative Language & Compiler Semantic Authority** for Toka 1.0.
2. This document ([`1_0_scope.md`](1_0_scope.md)), alongside [`1_0_api_matrix.md`](1_0_api_matrix.md) and [`1_0_gap_ledger.md`](1_0_gap_ledger.md), defines the **Production Application Integration & Qualification Evidence Layer**.

---

## 1. Platform Support & Qualification Tiers

### Tier 1: Primary Supported Release Platforms
Full 1.0 release guarantee. Every PR must pass the PR Gate checks; every release candidate must pass the defined Release Candidate Gate.
- **Linux `x86_64`** (glibc / POSIX)
- **Linux `aarch64`** (ARM64 glibc / POSIX)
- **macOS `aarch64`** (Apple Silicon ARM64 macOS 12+)

### Tier 2: Secondary Qualification Platforms
Supported with restrictions. Failures in Tier 2 platforms provide qualification feedback but do NOT block core 1.0 language/compiler freeze.
- **Windows Native `x86_64`** (MSYS2 / MinGW toolchain)

### Development Profiles (Non-Platform Tiers)
- **WSL2 (Windows Subsystem for Linux)**: Recommended development and execution environment for Windows users seeking native POSIX compatibility.
- **WASI (WebAssembly System Interface)**: Single-threaded WASM runtime profile.

---

## 2. CI Gate & Qualification Tiering

- **Pull Request Gate (`.github/workflows/ci.yml`)**: Automated on every PR and
  commit on `main`. It builds the SDK on the supported CI hosts, runs the
  current focused compiler/tooling/conformance regressions, and exercises the
  Linux service-kit and macOS GUI-settings dogfood paths. Exact test counts are
  intentionally not this document's contract; `ci.yml` is authoritative for
  the current runnable set. The gate must remain green, but it is not the
  multi-platform release qualification.
- **Release Candidate Gate (`.github/workflows/release.yml`)**: Automated on release candidate triggers via `tools/scripts/release_gate.py` (13-stage release qualification).
- **Manual / Scheduled Sanitizer Gate (`tools/build_sanitized.sh`)**: `runtime-tsan` and `compiler-asan` are available for manual developer validation or dedicated scheduled builds.
- **Windows/MSYS2 Dogfood (`.github/workflows/windows-dogfood.yml`)**: A
  scheduled/manual Tier-2 product-feedback build of the installed SDK path; it
  does not alter the Tier-1 release promise.

The operational decision for whether a 0.x experiment needs these checks, an
interface qualification, or an RC gate is
[`0_x_exploration_qualification_policy.md`](0_x_exploration_qualification_policy.md).

---

## 3. Standard Layer Hierarchy

1. **Language 1.0 Surface (Normative)**: Value/Handle sigils (`&`, `*`, `^`, `~`, `#`, `$`), explicit transfer contract (`cede`), postfix `!`, trait dispatch (`@Trait`), associated types, and async function mechanics (`fn -> async T`, `.await`, `.wait`, `.start`).
2. **Core / Std 1.0 Library (Normative)**: Owner-carrying containers (`Vec`, `Bytes`), string slices (`string`, `str`, `bytes`), non-blocking TCP sockets, `File` I/O, `Option`/`Result`, and `Task`/`Context`.
3. **Stdx Networking Profile (Qualification Layer)**: High-level owner-carrying HTTP/1.1 client/server streaming (`stdx/net/http.tk`), TLS stream adapter (`stdx/net/tls.tk`), and WebSocket framing (`stdx/net/websocket.tk`).
4. **Experimental / Post-1.0 Surface**: Structured async blocks, parameterized `.start`, async traits, and structured task scopes.

---

## 4. Capability & Dependency Boundaries

### OpenSSL Dependency Policy
- **OpenSSL is optional for the core/plaintext profile**: Plaintext HTTP/1.1, TCP socket I/O, file I/O, and JSON Serde MUST compile and execute cleanly without OpenSSL enabled (`-UTOKA_HAS_OPENSSL`). Verified via `tools/test_no_openssl.sh`.
- **TLS / HTTPS Profile**: TLS/HTTPS capabilities require an available TLS backend (OpenSSL 1.1.1+ or 3.0+).

### Non-Blocking Exclusions
- Native Win32 API rewrites for standard library POSIX subsystems.
- Legacy C-style pointer APIs in `stdx` (retained as compatibility shims; new code prohibited from introducing them).
