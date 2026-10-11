# Toka 0.14.0 Release Notes

Toka 0.14.0 delivers static dispatch async traits, framed transport protocol lifecycle management, and verified ownership consumption and container layout bounds for the unwrap propagation operator (`!`).

## Key Highlights

### 1. Static Dispatch Async Traits (G1)
- **Static Monomorphization**: Supports generic async traits with static dispatch and concrete type monomorphization, enabling zero-cost abstraction for asynchronous interfaces.
- **Dynamic Dispatch Rejection (`E0617`)**: Dynamic async traits (`dyn @AsyncTrait`) are explicitly rejected at compile time with diagnostic code `error[E0617]`.
- **Isolated Lifecycle Observation**: Production runtime (`lib/sys/toka_rt.o`) maintains zero observer symbol contamination, while isolated test observation proves exact-once drop and zero buffer leaks across all 4 execution branches.

### 2. Framed Transport & TaskHandle Lifecycle (G2)
- **Framed Protocol Core**: Full protocol support for `FramedStream`, `FramedReader`, and `FramedWriter` with guaranteed safety under cancellation, I/O errors, zero-byte frames, and short buffer reads. Enforces a strict 16 MiB (`MAX_FRAME_SIZE = 16777216` bytes) payload limit on individual transport frames.
- **`.start` Ownership Inheritance**: Fixed task handle `.start` wrapper lowering so that `.start` inherits the ownership semantics of its base handle, guaranteeing timely resource cleanup and zero remaining Task Control Blocks (TCB = 0) upon cancellation or completion.
- **Permanent Lifecycle Assertions**: Lifecycle gates enforce exact-once buffer free and complete TCB reclamation.
- **Platform Matrix**: 18 directed G2 controls are verified across the three core blocking platforms (Linux x64, Linux ARM64, and macOS ARM64). Windows is continuously validated under the dedicated Windows Dogfood pipeline.

### 3. Complete Consumption & Layout Invariants for Unwrap Operator `!` (G3)
- **Complete Ownership Consumption**: The unwrap propagation operator (`!`) now fully transfers ownership of payload values across both success and failure paths. Error propagation correctly handles owned `Err` values and `@ErrorInto` conversion.
- **Precise Scoped Drop Suppression**: Variable drop suppression is scoped to the exact resolved storage/alloca identity, preventing outer shadowed variables of the same name from being mistakenly disarmed.
- **Authoritative Container Identity**: Replaced loose name prefix matching with authoritative declaration and template identity verification. Custom shapes spoofing internal names (e.g. `Option_M_Probe`) are strictly rejected.
- **Per-Variant Payload Cardinality**: Canonical `Option<T>` and `Result<T, E>` variant shapes are strictly enforced: `Some`, `Ok`, and `Err` must each carry exactly one complete payload value; `None` carries no payload. Multi-payload tuple variants are rejected in all modes (`--check-only`, `-c`, `--emit-llvm`, and binary linking) with `error[E04512]` and emit zero artifacts. Legal single payloads containing composite aggregates (e.g. `Option<Pair>`) are fully preserved and dropped cleanly.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Independent Dogfood**: Windows validation is maintained continuously and independently.
- **Installed Qualification**: The standard SDK packaging and checkout-free replay verify G1, G2, and G3 controls on standalone unpacked packages without ambient compiler source dependencies.
