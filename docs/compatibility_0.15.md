# Toka 0.15.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Synchronous Generic Free Function Recursion
- Free functions with generic type parameters now support direct homomorphic self-recursion (`walk<T>` calling `walk<T>`) and mutual recursion (`is_even<T>` <-> `is_odd<T>`).
- Mutual recursion cycles commit atomically via Tarjan SCC validation. If any function in a cycle contains a type or borrow error, all cycle instances are marked `Invalid`.
- Subsequent lookup of an `Invalid` instance fails closed, aborting compilation and preventing tainted cache reuse.

### 2. Prohibited Generic Recursion Forms
- **Async Generic Recursion**: `async fn f<T>` calling itself directly, mutually, or via a mixed sync/async cycle is rejected at compile time with diagnostic `error[E0406]` (or `error[E04661]` for unproven task result origins). Zero object or IR artifacts are generated.
- **Polymorphic / Type-Expanding Recursion**: Generic functions expanding their type parameter across calls (e.g., `f<T>` calling `f<Option<T>>` or `f<[T]>`) are rejected with diagnostic `error[E0406]`.

### 3. Preserved Contracts & Zero Regression
- Non-recursive generic async functions continue to compile and execute with full static dispatch support.
- All existing 0.14 features (unwrap operator `!`, static async traits, framed transport) remain fully backward-compatible.

### 4. Version Identity Architecture (Product Version vs Interface Version)
- **Product Release Version**: `0.15.0`
  - Emitted by CLI tools (`tokac --version`, `toka --version`, `tokafmt --version`, `tokalsp --version`).
  - Recorded in `CMakeLists.txt`, `sdk.json`, and official release tarballs (`toka-v0.15.0-<target>.tar.gz`).
- **Compiler Interface Key**: `0.9.9-38`
  - Defined in `include/toka/InterfaceVersion.h` (`TOKA_COMPILER_INTERFACE_VERSION`).
  - Emitted in `.tki` interface metadata headers (`// @meta compiler_version: 0.9.9-38`), semantic cache manifests, and diagnostic JSON records (`"compiler_version": "0.9.9-38"`).
  - Tracks binary/AST interface compatibility, intentionally decoupled from release tags to maintain `.tki` cache compatibility across non-breaking releases. Release verifiers must check the product version without conflating it with the compiler interface key.

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.15.0`
   - `toka --version` reports `0.15.0`
   - `tokafmt --version` reports `0.15.0`
   - `tokalsp --version` reports `0.15.0`
2. **SDK Identity Descriptor (`sdk.json`)**:
   - `schema`: `"toka.sdk-identity"`, `version`: `1`
   - `version_label`: `"v0.15.0"`
   - `build_testing`: `false`, `source_dirty`: `false`
   - Binary digests matching physical `bin/` files.
3. **Standalone Development Loop**:
   - Package manager `toka fetch`, `toka build`, `toka run`, `toka test` operate without `TOKA_LIB` or repository checkout.
   - Offline dependency resolution succeeds against `package.lock`.
4. **Candidate Feature Verification (0.15 Generic Recursion)**:
   - Direct and mutual generic recursion execute correctly with exit code 0.
   - Destructor tracking asserts exact single cede handoff drop (`drop == 1`) and exact per-frame local variable destruction (`drop == 6`).
   - Source-hidden TKI compilation, link, and execution succeed without provider source present.
   - Negative controls fail closed: async recursion rejected with `error[E0406]` across `--check-only`, `-c -o`, and `--emit-llvm` modes with zero artifacts; polymorphic recursion rejected with `error[E0406]` (<50ms).
