# Async Runtime Phase 6 — Promise ABI Baseline & Cross-Module Evidence Closure

Status: Phase 6 Promise ABI boundary was recorded at `40ed2d7b`, with the
cross-module source-less smoke fixture added at `238daf22` (2026-07-24). This
dated baseline is not current-HEAD TCB conformance or Level-B body-derived
semantic attestation.

## 1. Runtime Promise ABI Accessor
Phase 6 makes the Promise boundary explicit:
- CodeGen constructs the coroutine promise allocation required by LLVM, but does not read/write private `TokaPromiseHeader` fields or assume payload LLVM struct field indices.
- The runtime owns Promise header initialization and exposes `toka_task_result_value_ptr(promise)` as the stable accessor for typed result payloads.
- The accessor returns the memory address immediately following the runtime-owned `TokaPromiseHeader`; CodeGen casts this pointer to the statically known result type.

## 2. Result State Protocol Preservation
Preserves the source ABI and result-state protocol:
- `0 (PENDING)` → `1 (READYLIVE)` → `2 (TAKEN)` for normal task completion.
- `3 (CANCELED)` for canceled task completion (`toka_task_complete_canceled`).

The later TCB protocol's private `Unclaimed -> Claimed` arbitration word is an
internal refinement used while the public state remains `READYLIVE`; it is not
a fifth Promise ABI state. Only after the typed transfer/drop completes does
the public state change from `READYLIVE` to `TAKEN`. `READYLIVE` records the
payload ownership commit but is not independently claimable: a consumer must
first acquire-observe normal terminal completion, then acquire `READYLIVE`
before attempting the private claim.

The later TCB's `ConsumerOwned -> RuntimeOwned` detach handoff is likewise a
private ownership protocol, not a Promise ABI value. Detach and terminal
publication both invoke the same idempotent drain check so a normal detached
`READYLIVE` payload cannot be stranded by their interleaving.

An owning heterogeneous `TaskScope` may use the same private result-owner
transition only as part of the later TCB RFC's atomic consume-transfer-link-
activate enrollment. This Phase 6 ABI record neither defines nor proves that
scope handoff.

## 3. Cross-Module `.tki` Evidence Verification
Cross-module `.tki` producer/consumer serialization and execution are verified by `tests/semantics/tki_replay/cases/async_p6_abi_cross_module/`:
- **Interface Export**: Provider exports `produce_async_value`, `produce_async_pair`, and `produce_async_calc` across the `.tki` boundary.
- **Source-less Execution**: Consumer compiles against source-less `lib.tki` + `lib.o`, executes cross-module `.await` operations via `toka_task_result_value_ptr`, and verifies successful result publication and payload alignment (`i32`, shapes).

This case proves declaration serialization, accessor use, normal result
publication, and payload alignment for the tested objects. It does not prove
that an arbitrary bodyless provider's frame cleanup or cancellation unwind is
trusted, does not exercise adversarial object substitution, and does not close
the Semantic Manifest Level-B provenance/object-binding gate.
