# Toka 0.13.1 release notes — patch release

Toka 0.13.1 is a patch release addressing semantic analysis correctness and safety fixes in the 0.13 development line.

## Key Changes

### 1. Async Task Return Lifetime Dependency Enforcement (R3)
- Semantic analysis now tracks `TaskHandle` as a lifetime-carrying type along function return paths.
- Distinguishes task execution and capture dependencies from completed task result dependencies.
- Discards task execution frame dependencies upon `await` or `wait`, retaining only completed task result dependencies.
- Local-source async task return escapes that were previously mis-admitted are now strictly rejected at compile-time with semantic diagnostics (`error[E0455]` or `error[E0454]`).
- Check-only, object compilation, and LLVM IR emission modes all consistently reject escaping tasks with zero intermediate artifacts emitted.
- Preserves legal caller-owned tasks, immediate awaits, and scalar return paths.
- **Important Note**: This fix enforces that local-source async task return escapes are rejected during semantic analysis; it does not claim a complete formal borrow safety proof for all concurrency contexts.

### 2. Trait Method Parameter Resolution under Module Context (R5)
- Resolves trait method parameter types under full module context rather than isolated lexical scopes.
- Correctly resolves shapes, types, and generic parameters in trait method signatures across module boundaries.

## Release Policy
- In accordance with the 0.13 three-core release policy, blocking qualification targets are Linux x64, Linux ARM64, and macOS ARM64.
- Windows is maintained under the continuous Dogfood build and validation pipeline.
