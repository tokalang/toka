# Managed-slot replacement — thread/sync candidate, not Accepted

This closes the previously open full-element replacement row within the
existing native storage contract. It does not activate arbitrary reference
moves, add pointer syntax, or change capture/ABI/refcount protocols.

```toka
auto held = mutex.lock().unwrap()
auto &^#slot = held.borrow_mut() // borrow the whole unique handle, with H
^slot = new Token(value = 9)

auto &~#slot = held.borrow_mut() // corresponding shared-element case
~slot = cede ~incoming
```

These are separate examples. `&^#slot` differs from `&^slot#`: write permission
on the contained handle is not write permission on its pointee. Bare `slot`
continues to select payload and cannot substitute for a complete handle write.

## Qualification and execution

- The destination is a direct local reference from an already qualified live
  native guard. Its full referenced type must match that guard's initialized
  element. Only the matching `^` or `~` destination is admitted. Existing
  ClosedPayload, permissions/flow ceiling, PAL and overlap checks remain.
- The guard must authorize writing, and the actual reference declaration must
  preserve the contained handle's write capability. The reference binding
  itself does not gain ownership or reseating permission. The destination-only
  marker is recomputed on actual assignment checking and is not cloned into
  other expression contexts.
- RHS checking uses the full slot type as context, rather than an enclosing
  callable annotation. This prevents the old CedeExpr context rule from
  decaying a complete handle to payload. Normal type compatibility validates
  any permitted permission attenuation before the cleanup plan is sealed.
- Capture discovery's inferred type caches are not treated as fresh source
  annotations for nested managed references or explicit `new` bindings. They
  are re-elaborated from the unchanged source/initializer; no implicit
  unique-to-shared conversion was added.
- CodeGen requires the existing destination-matching validated assignment
  carrier plus the exact native replacement plan, reference binding/type and
  guard witness. It loads the local reference once to obtain the **handle slot**,
  never peels the contained handle to obtain a payload address for the store.
- RHS production completes first (including a shared copy's existing retain).
  Old full-element cleanup then runs once, followed by a full-type store. The
  borrowed reference does not receive a scope Drop liability for that element.
- Any rejection restores the assignment-entry state. Missing, wrong-role,
  wrong-reference, wrong-source or wrong-destination plans fail without an
  object/IR artifact. CodeGen does not reconstruct transfer/cleanup facts.

## Directed verification

`toka_native_sync_managed_slot` / `test_native_sync_managed_slot.py`:

- Five runtime/strict normal-shadow parity programs: complete unique/shared
  replacement and remaining shared owner, RHS-before-cleanup ordering,
  same-environment shared copy, and unique/shared replacement inside real
  workers followed by join/read/final cleanup.
- Seven strict parity rejection/rollback cases: read-only binding, read guard,
  live derived borrow (with its conflict origin), overlap, wrong morphology,
  read-only pointee, and invalid RHS. Subsequent source/slot reads must not
  acquire E0438/E0410. Each also rejects object and IR emission.
- Twelve missing/mismatched plan fault checks (six faults × object/IR).
- `sync_managed_slot_replace.tk` and `sync_managed_slot_thread.tk` are now
  mandatory positives in the public thread/sync closeout gate, not pending or
  skipped cases.

All directed checks passed before starting the single final full comparison.
The full comparison is recorded in [the thread/sync candidate report](thread_sync_candidate_report.md).
The containing commit is the candidate revision. This document is not an
acceptance declaration.
