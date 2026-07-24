# Async Runtime Phase 6 — Promise ABI Baseline & Cross-Module Evidence Closure

Status: Phase 6 Promise ABI boundary and cross-module `.tki` baseline verified.

## 1. Runtime Promise ABI Accessor
Phase 6 makes the Promise boundary explicit:
- CodeGen constructs the coroutine promise allocation required by LLVM, but does not read/write private `TokaPromiseHeader` fields or assume payload LLVM struct field indices.
- The runtime owns Promise header initialization and exposes `toka_task_result_value_ptr(promise)` as the stable accessor for typed result payloads.
- The accessor returns the memory address immediately following the runtime-owned `TokaPromiseHeader`; CodeGen casts this pointer to the statically known result type.

## 2. Result State Protocol Preservation
Preserves the source ABI and result-state protocol:
- `0 (PENDING)` → `1 (READYLIVE)` → `2 (TAKEN)` for normal task completion.
- `3 (CANCELED)` for canceled task completion (`toka_task_complete_canceled`).

## 3. Cross-Module `.tki` Evidence Verification
Cross-module `.tki` producer/consumer serialization and execution are verified by `tests/semantics/tki_replay/cases/async_p6_abi_cross_module/`:
- **Interface Export**: Provider exports `produce_async_value`, `produce_async_pair`, and `produce_async_calc` across the `.tki` boundary.
- **Source-less Execution**: Consumer compiles against source-less `lib.tki` + `lib.o`, executes cross-module `.await` operations via `toka_task_result_value_ptr`, and verifies successful result publication and payload alignment (`i32`, shapes).
