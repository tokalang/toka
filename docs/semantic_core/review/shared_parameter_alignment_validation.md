# Shared parameter reception increment — not Accepted

Base: `85e5a6d350bfc3abbc4fd52da2bef5abd1be1fce`.

## Implemented authorization

CodeGen_Decl.cpp's body-side `needsCapture` now includes
`typeObj->isSharedPtr()`, exactly as authorized for generic/morphic shared
parameters. Signature generation already used this condition. No carrier layout,
reference-count algorithm, A/B protocol, raw_take guard, Parser, TKI or interface
key was changed. The native storage contract was edited only as a design draft;
no later sync/thread caller migration was made.

This condition alone is **insufficient**. The earlier diagnosis that only this
one decision was missing was incomplete; runtime verification, not the size of
the patch, determines completion.

## Measured matrix

The independent source fixture `shared_parameter_matrix.tk` does not use locks
or raw_take. It covers explicit shared formals, generic hatted formals and
morphic generic formals, both borrowing and cede, sync and async. Every execution
checks a remaining owner and final exact-once Drop. Async borrowing is tested
with direct await; `.start` on borrowed arguments remains rejected with E04583.
No std/task import is needed, so the known networking migration cannot mask the
parameter test.

| Runtime case | Result |
| --- | --- |
| Sync explicit borrow / cede | both pass |
| Sync generic hatted borrow / cede | both pass |
| Sync morphic borrow | fails: return 1, wrong value |
| Sync morphic cede | fails: return 4 |
| Sync morphic return | SIGSEGV (-11) |
| Async explicit borrow / cede | both pass |
| Async generic hatted borrow / cede | both pass |
| Async morphic borrow / cede | both pass |
| Original managed-storage shared positive | fails: return 5, Drop count |
| Actual shared guard/read/write positive | SIGABRT (-6) in this run |
| Source-hidden TKI + object, explicit shared return | passes actual execution |
| Same source-hidden path, morphic shared return | SIGBUS (-10) in this run |

The new `toka_shared_parameter_abi` target runs every case without xfail/skip:
**17 checks pass, 6 fail**. This includes two unchanged raw_take-isolation
no-artifact checks and four E04583 borrowed-start no-artifact checks. The 13
parameter cases, two sync cases and two source-hidden cases remain mandatory
positive goals, including the failures.

Directed CTest: public thread responsibility and existing sync storage pass;
the new ABI target fails (**2/3**, 56.33 seconds). No full suite was run and no
existing diagnostic oracle changed. Logs: `/private/tmp/toka-shared-abi-ctest.log`
and `/private/tmp/toka-shared-parameter-abi.log`.

## Remaining physical reception discrepancy

There are distinct existing body representations:

- Explicit shared sync parameters use the incoming carrier address directly.
- The morphic sync parameter now has an `alloca ptr` containing that address,
  but its value/handle paths can still use the wrapper as though it were the
  two-word carrier itself. The reproduced IR loads/stores `{ptr, ptr}` against
  that pointer-sized wrapper, and ordinary member reads also select the wrong
  level. Borrow, cede and return must agree on the actual carrier address.
- Async shared parameters already materialize a full carrier in the coroutine
  frame. They must not receive an extra wrapper dereference; all six async
  parameter combinations currently pass and are regression controls.

A supplemental attempt to extend `capturedHandleSlotNeedsLoad` was rejected by
automatic review as beyond the expressly approved one-line edit. It was **not
applied**, and no alternate tool or representation rewrite bypassed the refusal.
That single flag is not presented as a sufficient replacement patch: the failing
ordinary morphic read must be addressed too, while the direct async carrier must
stay direct.

Required follow-up scope for approval: finish **shared parameter body symbol and
addressing consistency** using the already selected pointer-to-carrier ABI,
covering ordinary read, cede and return. Reuse the existing explicit-shared
reception semantics where applicable; do not redesign the ABI or refcount, change
other type families, or remove the raw_take isolation gate. All mandatory cases
above must pass before this repair can be Accepted.

## Storage contract revision (design only)

The original `native_sync_storage_contract_proposal.md` now specifies:

1. exact private operations, trusted declaration/instance identity, compiler-side
   witness carrier and separate static/runtime publication points;
2. partial construction cleanup and checked native failure handling, preserving
   infallible factory API through cleanup-then-fatal policy;
3. first-batch `ClosedPayload<T>` proof stable under all admitted replacements,
   rather than shared copies of stale initializer dependencies. Unknown instances
   remain unqualified; source-hidden missing contracts reject without adding a
   serialized protocol.

Current sys/sync wrappers discard native status and cannot simply be labelled
verified. The explicit native failure policy and private adapters are design
decisions still awaiting acceptance, not code implemented by this increment.
