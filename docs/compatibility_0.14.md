# Toka 0.14.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Unwrap Operator `!` Container & Layout Requirements
- The unwrap propagation operator (`!`) strictly requires authoritative instances of `core/option::Option<T>` or `core/result::Result<T, E>`.
- Any custom shape declaration attempting to spoof compiler-internal mangled prefixes (e.g. `Option_M_...` or `Result_M_...`) is rejected with `error[E04512]`.
- Variant payloads for `Some`, `Ok`, and `Err` must represent exactly one payload type. Multi-element tuple syntax (such as `Some(A, B)`) is rejected at compile time. Applications requiring multiple fields in a payload should use a named struct (e.g. `shape Pair(a: A, b: B)` inside `Option<Pair>`).
- The `None` variant must carry no payload fields.

### 2. Async Trait Bounds & Coroutine Frames
- Dynamic dispatch over async traits (`dyn @AsyncTrait`) is not supported and is rejected with diagnostic `error[E0617]`. Use static generic dispatch instead (`fn serve<T: AsyncTrait>(svc: T)`).
- Coroutine frame allocations are bounded by a 16 MiB ceiling.

### 3. TaskHandle Lifecycle
- Lowering of `.start.await` guarantees that canceling or completing the task cleanly releases buffers and reclaims the Task Control Block (TCB = 0).

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.14.0`
   - `toka --version` reports `0.14.0`
   - `tokafmt --version` reports `0.14.0`
   - `tokalsp --version` reports `0.14.0`
2. **SDK Identity Descriptor (`sdk.json`)**:
   - `schema`: `"toka.sdk-identity"`, `version`: `1`
   - `version_label`: `"v0.14.0"`
   - `build_testing`: `false`, `source_dirty`: `false`
   - Binary digests matching physical `bin/` files.
3. **Standalone Development Loop**:
   - Package manager `toka fetch`, `toka build`, `toka run`, `toka test` operate without `TOKA_LIB` or repository checkout.
   - Offline dependency resolution succeeds against `package.lock`.
4. **Candidate Feature Verification (G1 / G2 / G3)**:
   - G1: Static dispatch async trait lifecycle passes with 0 leaks and exact-once drop.
   - G2: Framed transport (18 directed controls across 3 core platforms, plus Windows Dogfood) passes without frame corruption or descriptor leaks.
   - G3: Unwrap consumption (21 positive controls pass, 5 rejection controls fail closed with `E04512`).
