# Async/Place Contract Closure v1 Execution Plan

**Status:** Active execution plan. This document turns the already-approved
Async Runtime TCB RFC into a finite implementation and qualification program.
It changes no source spelling by itself.

**Depends on:** the frozen P0.4 exact-place fact/eligibility model, the
bounded default-runtime boundary, and the normative
[`async_runtime_tcb_rfc.md`](async_runtime_tcb_rfc.md). It is independent of
Semantic Manifest P2 except where a later source-less body-derived cleanup
claim needs its own explicit evidence.

## 1. Objective

Close the remaining semantic discontinuity in Toka's core model:

```text
PlaceState / authority / cleanup
      -- await, cancellation, finalization -->
Task / frame / result exactly-once protocol
```

The completed program has two ordered claims:

1. **AS — runtime-core closure:** the default runtime conforms to every
   Section 8.1 gate at one qualified revision. It has one terminal publisher,
   one result disposition, complete registration teardown, checked task/frame
   lifetime, and cancellation cleanup that cannot expose user CFG early.
2. **AB — async/place language bridge:** CodeGen frame cleanup and cancellation
   CFG consume the same admitted exact-place facts as Sema and synchronous
   cleanup, satisfying every Section 8.2 gate for the existing whole-place and
   bounded direct-projection capability matrix.

This is a semantic closure program, not an executor-plug-in project or a
general async feature expansion.

## 2. Entry evidence and current gap

The bounded P5 runtime baseline currently has focused passing evidence for:

- one terminal publisher;
- cold cleanup before canceled publication;
- private result disposition;
- pre-commit suspension rollback;
- basic completion subscription ordering; and
- queue-publication helping.

Those tests are useful regression gates, but they do **not** satisfy TCB RFC
8.1 as a whole. In particular, full task/wait identity, checked retained
lifetime, frame-access retirement, complete `WonPending -> WonCommitted`
arbitration, await-resolution cleanup, and helpable structured-scope close
remain unqualified. P0.4 likewise explicitly excludes exact-place state
across `.await` and terminal cancellation.

No AS or AB completion claim may be inferred from P5 test success alone.

## 3. Frozen scope

### Included

- the sole official default runtime and compiler-generated coroutine path;
- task/frame/result cancellation, cleanup, and reclamation operations required
  by TCB RFC 8.1;
- the current exact whole-place plus admitted direct-field/fixed-index
  PlaceState capability matrix in generated frame cleanup; and
- source and retained-body-rechecked source-less Level-A evidence for each
  admitted body-derived async cleanup claim.

### Excluded

- third-party runtime ABI, executor traits, or scheduler policy changes;
- new async surface syntax, algebraic effects, generalized async typestate,
  dynamic projection, custom-drop aggregates, and unbounded container paths;
- Semantic Manifest P3 distribution policy, and bodyless async-cleanup
  authority; and
- lexical automatic `TaskScope` exit or Scoped Borrowed Tasks before AS and AB
  are qualified. Existing TaskScope helpers remain internal/unqualified
  substrate until their corresponding TCB gates close.

Unsupported states must reject before lowering; they may not be represented as
best-effort runtime cleanup.

## 4. Ordered execution slices

Each slice lands only with its focused native race tests, source-level redline
tests where applicable, and an update to the current qualification ledger.
Later slices may rely only on closed earlier slices.

| Slice | Work | TCB gates advanced | Exit evidence |
|---|---|---|---|
| AS.0 | Establish full task-instance identity, checked retains, and frame-access pins/irreversible retirement. Remove bare-pointer resurrection paths from runtime-owned registries. | 8.1.5, 8.1.12, 8.1.13 | stale/overflowed token and retain tests; terminal/result/frame preemption tests |
| AS.1 | Replace the bounded registration record with one installed WaitSet descriptor and explicit `Waiting -> WonPending -> WonCommitted -> Inactive` teardown/rollback transaction. | 8.1.1, 8.1.4, 8.1.6, 8.1.7 | n-way winner, failed install, nested-suspend, terminal, and forced-publication tests |
| AS.2 | Complete terminal/result/cold finalization around the retained lifetime and re-entrant callback boundary. | 8.1.2, 8.1.3, 8.1.10 | claimant/drop, detach/complete, cold finalizer, and callback-arbiter probes |
| AS.3 | Implement completion subscription and joint await/cancellation resolution with one cleanup aggregate. | 8.1.8, 8.1.9 | child terminal/cancel permutations, single-worker cleanup and source-cancel/race tests |
| AS.4 | Make structured registration a helpable close/progress protocol whose result and reference authority joins the same aggregate. | 8.1.11 | enrollment/close/cancel/stale-token and multi-scope aggregate tests |
| AS.5 | Run the complete 8.1 matrix at one revision and record the runtime-core closure. | all 8.1 | reproducible full AS qualification ledger |
| AB.0 | Lower frame-local cleanup plans from the Sema exact-place fact and wire return/terminal ordering through the AS contract. | 8.2.1, 8.2.2 | normal/caught/unhandled/cold cleanup probes for each admitted place kind |
| AB.1 | Lower cancellation CFG and post-resolution winner cleanup without duplicate result or place authority. | 8.2.3, 8.2.4 | cancellation-at-every-boundary and winner-suppression matrices |
| AB.2 | Qualify source and retained-body Level-A replay for every admitted async cleanup contract. | 8.2.5 | source/source-less parity plus bodyless-provider fail-closed cases |
| AB.3 | Run the complete 8.2 matrix at the AS-qualified revision and record the bridge closure. | all 8.2 | reproducible full AB qualification ledger |

## 5. Non-negotiable invariants

Every implementation slice must preserve all of these:

1. User cleanup and typed result drop execute with no scheduler, wait,
   completion, scope, terminal, or cancellation arbiter held.
2. Frame retirement is irreversible and cannot race an owner that still has a
   checked frame-access pin.
3. A cancellation winner, result claimant, wait winner, cleanup component, and
   exact place fact are distinct authorities; no state bit substitutes for two
   of them.
4. A failed or stale registration changes neither parent cancellation epoch nor
   task-visible state, and releases every retained reference once.
5. A body-derived async cleanup fact is either rechecked from the provider body
   by the accepting compiler or rejected. An interface, hash, or comment alone
   grants no cleanup authority.

## 6. Completion boundary

Async/Place Contract Closure v1 is complete only when all Section 8.1 and 8.2
gates are evidenced at the same qualified revision, the bounded P5 document is
superseded as a baseline by that qualification record, and the existing
unsupported async/place cases still reject before lowering. Passing individual
runtime probes, exposing TaskScope helpers, or making the default queue more
featureful is not completion.

Only after this boundary may a separate RFC make lexical cancel-then-join
automatic or introduce Scoped Borrowed Tasks. Only after a separate provenance
design may a bodyless provider carry an async-cleanup obligation.
