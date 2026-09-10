# Binding continuation: Arena source migration

Base: `0038404be87977cbefc20df494569b644cbab605` (B1 candidate, not newly Accepted).
Branch: `impl/binding-b2-arena`.
This is a bounded continuation under the existing raw construction and binding
rules, not a new language or allocator contract.

## Implementation

1. `Arena.chunks` and `ArenaChunk.next` now declare both the existing handle
   rebind capability and the payload-write capability needed by traversal,
   offset updates and reset. Mutability is not inferred from an outer `self#`.
2. Descriptor construction uses the already accepted explicit
   `unsafe (address as nul *ArenaChunk#)` contract. It remains nullable until
   that same pointer is checked; only then is its non-null view obtained.
   Evidence must say `UnsafeCallerPrecondition`, not compiler-proven storage
   initialization or ownership.
3. Descriptor allocation uses `libc_malloc(sizeof(ArenaChunk))`. The former
   `unsafe alloc` panicked on failure before the existing buffer-release/null-
   return branch could execute. This is an intentional failure-path behavior
   repair, not merely facts recording: the failed descriptor allocation now
   releases the already allocated buffer and returns `ADDR0`, permitting retry.

Buffer allocation, chunk layout, allocation order, traversal, reset/reuse,
alignment arithmetic and release/drop implementation remain otherwise unchanged.
There is no compiler, CodeGen, Parser, runtime, interface-key, ABI or raw_take
change. This does not add tracking or destruction of arbitrary values placed
in arena storage, or a compiler-verified initialized-extent capability.

## Verification

`tools/scripts/test_binding_b2_arena.py` retains both original positive programs:

- `tests/semantics/stage1_return_matrix/arena_raw_handle_return.tk`
- `tests/pass/g07_arena_test.tk`

Both pass real execution and normal/non-call-shadow parity. The existing
mutable-argument warnings in the comprehensive original are not suppressed.

A dedicated generated-IR runtime test changes only the allocator/releaser
symbols inside the actual emitted `Arena_alloc_bytes` and `Arena_release`
functions. It asserts exactly two allocation instructions and one failure-path
free in the former, and two release instructions in the latter. No process-wide
interposition, production code rewrite, or fake pointer is used.

The test-only hooks count live allocation identities, flag duplicate/unowned
releases without causing undefined behavior, and can fail either allocation.
The matrix covers zero bytes, single/multiple chunks, reset without allocation
or release, explicit release twice followed by drop, automatic drop, buffer
allocation failure, descriptor allocation failure and successful retry.
An additional case fails descriptor allocation while extending a nonempty
arena, then proves its existing chunk is still live and reusable after reset.

Five negative parity cases retain their intended diagnostics and prohibit
both object and IR output: readonly Arena, readonly raw field, readonly source
through Addr/call mapping, missing nullable guard, and PAL conflict. The gate
also checks raw construction evidence has no Drop liability.

The dedicated gate passed: 3 source parity cases, 2 original runtime programs,
the scoped allocation/failure matrix, 5 negative parity cases and 10 no-artifact
checks. It is registered as `toka_binding_b2_arena`.

Incremental `toka-tools` build passes. Related CTest is **4/5**, 113.29 seconds:
the Arena gate, B1 scalar/view, B1 propagation and unsafe raw construction pass.
The return matrix now gets beyond its original Arena failure and stops at
`static_storage_chain.tk:11`, where binding `text()` reports E04661 /
`IncompleteFacts`. The independent baseline compiler at `72407112` with its own
library reproduces that exact failure. No compiler/return rule was changed,
and the positive remains a failing positive, not an updated negative oracle.

No new full PASS/FAIL run is requested for this local repair. The restored
`g07_arena_test.tk` runtime is not used to invent an updated global PASS total.
This file does not declare Arena, binding, B1, or the complete RFC Accepted.

## Scope boundary

The B1 branch and thread freeze refs remain unchanged. Networking/raw-container
extraction, source-hidden callable environment summaries, the separately
recorded local-view return defect, and receiver/protocol work are not folded
into this Arena patch. No push, PR or Actions run is requested.
