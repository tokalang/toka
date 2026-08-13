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

### 2.1 Product-driver check: service shutdown cleanup

The independently maintained
[`toka-examples/service-kit` shutdown qualifier](https://github.com/tokalang/toka-examples/tree/main/service-kit)
is a retained end-to-end driver for this program. It cancels a worker suspended
in an HTTP read, waits for the structured scope to drain, and then observes the
SQLite bridge's live statement and handle counts. The qualifier exposed an old
SQLite wrapper spelling that placed native-resource `drop` methods in ordinary
`impl` blocks.
That spelling is outside the current lifecycle contract: the bridge now
declares its hooks in `impl T@Encap`, so compiler-generated frame cleanup and
normal lexical cleanup use the same declared resource fact.

This is a product regression gate, not an AS.3/AS.4 or AB completion claim:
it confirms the existing `@Encap` cleanup path in a cancellation scenario, but
does not establish the still-required TCB arbitration or async exact-place
matrices.

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
| AS.1 | Replace the bounded registration record with one installed WaitSet descriptor and explicit `Waiting -> WonPending -> WonCommitted -> Inactive` teardown/rollback transaction. This supplies, but does not close, the old-wait-uninstall prerequisite of structured join. | 8.1.1, 8.1.4, 8.1.7 | n-way winner, failed install, nested-suspend, terminal, and forced-publication tests |
| AS.2 | Complete terminal/result/cold finalization around the retained lifetime and re-entrant callback boundary. | 8.1.2, 8.1.3, 8.1.10 | claimant/drop, detach/complete, cold finalizer, and callback-arbiter probes |
| AS.3 | Implement completion subscription and joint await/cancellation resolution with one cleanup aggregate. | 8.1.8, 8.1.9 | child terminal/cancel permutations, single-worker cleanup and source-cancel/race tests |
| AS.4 | Make structured registration a helpable close/progress protocol whose result and reference authority joins the same aggregate. This is where structured join consumers become qualified. | 8.1.6, 8.1.11 | enrollment/close/cancel/stale-token and multi-scope aggregate tests |
| AS.5 | Run the complete 8.1 matrix at one revision and record the runtime-core closure. | all 8.1 | reproducible full AS qualification ledger |
| AB.0 | Lower frame-local cleanup plans from the Sema exact-place fact and wire return/terminal ordering through the AS contract. | 8.2.1, 8.2.2 | normal/caught/unhandled/cold cleanup probes for each admitted place kind |
| AB.1 | Lower cancellation CFG and post-resolution winner cleanup without duplicate result or place authority. | 8.2.3, 8.2.4 | cancellation-at-every-boundary and winner-suppression matrices |
| AB.2 | Qualify source and retained-body Level-A replay for every admitted async cleanup contract. | 8.2.5 | source/source-less parity plus bodyless-provider fail-closed cases |
| AB.3 | Run the complete 8.2 matrix at the AS-qualified revision and record the bridge closure. | all 8.2 | reproducible full AB qualification ledger |

## 5. Current qualification ledger

### 5.1 AS.1 descriptor-progress entry

The default runtime now retains one installed `WaitSet` descriptor through
`Waiting -> WonPending -> WonCommitted -> Inactive`. Each physical outcome slot
owns a descriptor reference; winner selection adds a private commit reference.
A preempted winner therefore cannot strand the selected parent: any inactive
member observation, winner query, or slot release can commit the same immutable
selection and publish its exact queue ticket. The last outcome-slot release is
the only physical descriptor-free transition; logical `Inactive` is published
before any worker or callback can observe the selected ticket.

`toka_async_queue_publication` forces a selected n-way source to stop at
`WonPending`, then proves a losing source completes the descriptor, publishes
one ticket, preserves the recorded winner, and leaves every slot stale after
release. The same forced state proves that a task-level cancellation request
helps the selected descriptor rather than publishing a descriptor-less wake.
Existing probe cases retain coverage for source/cancel selection before and
after suspension, forced queue-publication preemption, active-member teardown,
fresh suspension after an old outcome descriptor commits, and third-source
n-way selection. A selected source before `commit_suspend` is likewise forced
to complete through `PreparingWithPendingWake`, not a descriptor-less queue.
The rollback probe forces `abort_suspend` through that same preempted selected
state and proves local resume with the outcome intact; the terminal probe
proves terminal cleanup reaps such inactive outcome slots rather than leaving
them to unreachable user code. A test-only descriptor-creation failure also
proves that pair installation exposes neither member slot, wake, nor output
token before the caller rolls its already-prepared task back to `Running`; the
same check covers n-way registration, whose candidate slots remain private
until the descriptor has been installed. Terminal probes cover both normal and
canceled publication after a source has selected an uncommitted descriptor.
WaitSet-token exhaustion likewise fails before n-way installation and leaves
the caller's output buffers unchanged. A nested reinstallation probe retains
old physical outcomes across a new pair installation, then delivers their late
wake and release operations; they cannot alter the new group's authority or
queue ticket.

This advances AS.1 only as entry evidence. It does **not** close 8.1.1,
8.1.4, or 8.1.7: failed-install rollback, terminal cleanup, nested suspension,
full-token wait identity, and the complete qualification matrix remain required
at one revision. TCB 8.1.6 is deliberately not an AS.1 closure claim: its
`race2`/`select2`/`TaskScope` cancel-join-drain obligation is owned by AS.4,
which consumes AS.1's already-uninstalled-old-wait invariant.

### 5.2 AS.1 qualification matrix (working)

| TCB gate | Current forced evidence | Still required for closure |
|---|---|---|
| 8.1.1 — one cancellation/source winner | Source/cancel selection race; cancellation after `WonPending`; selected source before `commit_suspend`. | Every admitted ready/timer/reactor/completion source and the complete parent-cancellation aggregate. |
| 8.1.4 — failed suspend installation rolls back | Overlapping registration rejection; singleton/pair/n-way descriptor-creation failure; selected `WonPending` abort. | Timer/reactor/completion/progress registrations and the later aggregate cleanup kinds. |
| 8.1.7 — one helpable queue ticket | Preempted `WonPending` publisher; cancellation and `commit_suspend` helpers; stale old outcome after a new installation; terminal after queued-but-unpublished ticket; late helper after dequeue. | One reproducible matrix covering every queue publisher at the same revision. |

The table is a live qualification index, not a new public contract. It keeps
the default-runtime baseline bounded while making the remaining proof
obligations explicit.

### 5.3 AS.2 terminal/result entry evidence

`toka_async_terminal_publisher`, `toka_async_result_disposition`, and
`toka_async_cold_cancel_cleanup` jointly provide AS.2 entry evidence for the
current terminal/result/cold-finalization substrate. In particular, the result
probe forces a detach after `Pending -> ReadyLive` but before the terminal
publisher exposes `Completed`: detach transfers the result owner without an
early claim, then the publisher performs the one typed drop and releases the
detached owner after normal terminal publication. A detached canceled terminal
is separately shown to expose no payload while releasing that owner. The probe
also retains the existing re-entrant typed-drop, detached-before/after-
complete, scope-owner, canceled-no-payload, and frame-access-guard cases. A
2,000-round detach/normal-terminal race proves the terminal and detached paths
contend for the same private result claim without leaking the last TCB
reference. The cold probe retains the no-body cleanup-before-canceled-
publication cases, plus 2,000 start/cancel races over `Created`: either cold
cancellation wins and publishes canceled with no queue entry, or start wins
and supplies the sole ready task without a second cold finalization.

This is an evidence index, not closure of 8.1.2, 8.1.3, or 8.1.10. Full
terminal/join release-acquire matrices and the complete callback/finalizer
reclamation boundary remain required at one qualified revision.

### 5.4 AS.2 qualification matrix (working)

| TCB gate | Current forced evidence | Still required for closure |
|---|---|---|
| 8.1.2 — result claim/drop and publication | Premature/canceled consumer rejection; consumer transfer; re-entrant typed drop; scope drop; `ReadyLive`-before-`Completed` detach; 2,000-round detach/terminal race; frame access guard. | Complete terminal/join release-acquire matrix and callback/finalizer interaction with every retained authority. |
| 8.1.3 — cold cleanup before terminal | Explicit cold cancel and cold handle drop run no body, invoke cleanup once, and publish canceled only after callback return; 2,000-round start/cancel arbitration over `Created`; source-level cold-cancel redline. | Complete compiler-generated frame-cleanup matrix, which also feeds AB. |
| 8.1.10 — detach/complete handoff | Detach-before/after-complete, forced `ReadyLive`/`Completed` handoff, canceled terminal, and concurrent normal terminal drain. | Full framed-task reclamation and complete detached-result/release ordering with the remaining terminal/join authorities. |

The table is a live closure index, not a new runtime ABI or a claim that any
AS.2 gate is closed.

### 5.5 AS.3 completion-descriptor entry evidence

`toka_async_completion_subscription` now forces parent cancellation to race a
child terminal publication for 1,000 pair-WaitSet instances. Either contender
may remove the parent wait, but exactly one ready ticket is observable; the
completion descriptor releases its checked child retain and no active wait or
TCB remains after the modeled worker turn. This advances only the existing
descriptor substrate for 8.1.8.

The same native target also carries the narrower direct-await experiment: a
test-only terminal pause makes the child terminal state visible while its
continuation still owns the parent link. A parent cancellation request must not
queue the suspended parent in that window; releasing the continuation produces
the one ready ticket. The source-level P5 redline then enters an actual child
`.await`, cancels its parent, and verifies no child or parent await-successor
CFG is exposed. An adjacent `@Encap` resource probe keeps a parent frame local
live across the same await and observes its declared drop exactly once after
the canceled child reaches terminal, while proving that no child normal result
was constructed. The direct-await private word now gives that boundary one
atomic `Armed -> ChildNormal | ChildCanceled | CancelClaimed` choice, followed
by one `ChildNormal -> NormalClaimed` result authorization before CodeGen takes
the payload. Native regressions cover the forced continuation window, exactly-
once normal claim, post-normal non-reselection, and 1,000 direct
child-terminal/parent-cancel races. This is a service-shutdown-shaped safety
boundary, not a general await-resolution protocol.

It is deliberately not evidence for the 8.1.9 await-cleanup barrier: the
current runtime and CodeGen still lack post-normal-claim suppression and the
task-wide cleanup aggregate required to join every child result disposition
with parent cancellation. The remaining implementation stays within the
bounded single-child, service-shutdown-driven 0.x experiment until its benefit
is demonstrated, without freezing a new source spelling, TKI rule, or runtime
ABI.

## 6. Non-negotiable invariants

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

## 7. Completion boundary

Async/Place Contract Closure v1 is complete only when all Section 8.1 and 8.2
gates are evidenced at the same qualified revision, the bounded P5 document is
superseded as a baseline by that qualification record, and the existing
unsupported async/place cases still reject before lowering. Passing individual
runtime probes, exposing TaskScope helpers, or making the default queue more
featureful is not completion.

Only after this boundary may a separate RFC make lexical cancel-then-join
automatic or introduce Scoped Borrowed Tasks. Only after a separate provenance
design may a bodyless provider carry an async-cleanup obligation.
