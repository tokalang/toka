# Async Runtime Phase 6 — Promise ABI Baseline

Status: initial vertical slice, runtime and CodeGen verified together.

Phase 6 begins by making the Promise boundary explicit. CodeGen may construct
the coroutine promise allocation needed by LLVM, but it must not read or write
private Promise header fields or assume the payload field's LLVM struct index.

The runtime owns Promise header initialization and exposes
`toka_task_result_value_ptr(promise)` as the stable accessor for a typed result
payload. The accessor returns the address immediately after the runtime-owned
`TokaPromiseHeader`; CodeGen casts that opaque pointer to the statically known
result type.

This slice preserves the existing source ABI and result-state protocol:
`Pending → ReadyLive → Taken`, with `Canceled` as the terminal cancellation
state. Cross-module `.tki` serialization is not claimed complete yet; the next
Phase 6 slice must add a producer/consumer fixture and verify identical async
task signatures and result-state behavior across module boundaries.
