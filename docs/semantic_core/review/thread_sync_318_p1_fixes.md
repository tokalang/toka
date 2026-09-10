# Incremental fixes after the 31843d5c review — candidate, not Accepted

Scope is only the two authorized P1s. No syntax, runtime ABI, capture layout,
reference-count algorithm, unlock protocol or unrelated binding work changed.
The previous full-run evidence remains unchanged; this revision uses directed
revalidation only.

## 1. A live guard retains unlock responsibility

The original `double_unlock_count.tk` was accepted by the frontend. The audit
counted two unlocks; its no-explicit-unlock control counted one.

The new rejection matches the **resolved existing Mutex unlock declaration**
and the **exact owner instance**, not just the method/type name or a common
factory parent. A visible live guard or a live Result that may contain a guard
blocks unlock. Shared aliases, moved guards and successful whole-guard
replacement retain their identity relation. Out-of-scope and definitely moved
bindings do not remain live guards. An earlier provenance exposure does not
pretend to have released an existing guard.

Rejection occurs before the old exposure-invalidation path. An entry checkpoint
is restored only when this exact native conflict is rejected; the surface name
filter merely selects potential checkpoints and grants no authority. Ordinary
unrelated unlock methods are not pulled into a new rollback policy.

Incremental testing also exposed the same P1 for an unnamed argument:

```toka
observe(mutex.lock().unwrap(), mutex.unlock())
```

Guard acquisition facts are therefore retained for the current full expression
as rejection-only facts, isolated by the function being analyzed and not
published from speculative/capture-discovery checks. A named binding takes over
its guard tracking; an expression's facts do not survive its end. This closes
the sibling-argument guard/Result case without creating a new unlock token/API.

The diagnostic is `ActiveGuardOwnsUnlock` with the acquisition origin. The
rejected call emits no object/IR and does not poison the guard/owner state.

## 2. Shared payload restrictions are not slot replacement restrictions

The initial replacement installs the incoming element's normal payload flow
ceiling. The previous target query also used that ceiling to revoke the guard's
handle-slot write capability, making a second identical replacement fail E04572.

The target query now derives slot H from the matched live writable guard and
the actual reference's contained-handle declaration. It does not derive H from
the stored element's P. Existing payload flow restrictions remain in place and
continue to reject pointee writes; read guards, read-only contained handles,
wrong morphology, overlap and borrowing checks remain unchanged.

This changes neither retain/transfer selection nor old-value cleanup. Tests
cover repeated copies, a branch followed by another replacement, and mixed
copy/transfer/copy with remaining shared owners and exact-once destruction.

## Verification

Original reviewer files were replayed without editing them:

- `shared_once.tk` and `shared_twice.tk`: normal/shadow parity and actual runtime
  pass, exit 0. External sources were given explicit workspace coordinates.
- `double_unlock_count.tk`: normal/shadow reject with `ActiveGuardOwnsUnlock`;
  object and IR outputs are absent.
- `single_unlock_count.tk`: actual counted result remains exactly one unlock.

Expanded existing gates:

- Native witness: ten unlock rejection/rollback cases (direct, qualified-call,
  shared alias, moved guard, pending Result, exposed owner, maybe-released guard,
  rebound guard, temporary guard and temporary Result); four check-only controls;
  three counted runtime cleanup controls.
- The no-guard/after-release unlock controls are **check-only**, not claims that
  unlocking an already-unlocked POSIX mutex is runtime-valid. Counted runtime
  controls use guard cleanup/reacquisition and preserve native preconditions.
- Managed-slot: nine runtime/parity programs, nine rejection/rollback cases and
  twelve no-artifact faults, including P restrictions after repeated replacement.
- All previous tests in these gates remain; no oracle was blessed or removed.

Logs are retained under `/private/tmp/toka-thread-sync-p1.7Ck53u`. The related
CTest set consists of native witness, managed-slot, composite, public owner,
shared parameter ABI, sync storage, and public thread/sync closeout. No full
PASS/FAIL/CTest suite or four-platform CI is part of this increment.

Final related CTest: **7/7 passed, 188.82 seconds**, recorded in
`related-final-ctest.log`. The incremental compiler, language server, directly
dependent native proof targets and tools build also passed (`final-build.log`).
The earlier seven-target run predates the additional temporary-guard regression
and is not substituted for this final result. `git diff --check` passed.

This is an implementation/revalidation record, not an acceptance declaration.
