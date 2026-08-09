# Toka Async Runtime P5 Specification: Task Cancellation, Join Substrate & Structured Concurrency

**Document Status**: Bounded runtime-baseline record; full Phase 5
conformance and current-HEAD requalification remain pending. Sections 1.1--1.6
record discrete implementation evidence; they are not an open-ended mandate to
implement the whole TCB RFC one interleaving at a time.

**Historical Target**: Toka v0.9.5-P5

**Authority:** This document is subordinate to
[`async_runtime_tcb_rfc.md`](async_runtime_tcb_rfc.md), which is the normative
design contract. If this record conflicts with that RFC, the TCB RFC wins. This
record does not by itself claim that cancellation, result reclamation, cold
cancel, or suspension rollback is implemented conformantly.

---

## 1. Executive Summary

This record tracks the intended Phase 5 implementation and closure evidence for:
1. **Linearized Task Cancellation**: Atomic cancellation of active tasks competing through `WaitRegistry` winner CAS.
2. **Completion Subscription & Join**: Subscription mechanism waking waiters upon task completion or cancellation.
3. **N-Way WaitSet Allocation**: Atomic allocation of $N$-slot wait sets for multi-way race combinators.
4. **Loser Cleanup Guarantee**: Absolute invariant that `race2` / `select2` combinators do not return to caller until loser tasks are fully terminated and disarmed.
5. **Structured Concurrency (`TaskScope`)**: Retained task lifetime management
   intended to guarantee zero orphan tasks and zero UAF (Use-After-Free).

### 1.1 Current narrow implementation evidence: terminal publisher arbitration

The current runtime has one deliberately limited, independently qualified
improvement toward the TCB RFC's runtime-core gates. `Pending -> ReadyLive` and
`Pending -> Canceled` now compete through one terminal-publication CAS in
`toka_task_publish_terminal`. Only that CAS winner may publish the matching TCB
terminal state, drain completion subscriptions, hand off a continuation, or run
detached-result/owner cleanup. A normal result therefore cannot later be
relabeled canceled merely because a cancellation request arrived late, and a
losing terminal path is a no-op.

New CodeGen emits only `toka_task_complete` after it stores a normal result.
The former raw `toka_task_publish_result_state` C ABI symbol remains solely as
a compatibility wrapper for older objects; it invokes the same terminal path
instead of directly exposing `ReadyLive`. Cold task cancellation likewise lets
the canceled terminal CAS decide the state before publishing
`CompletedCanceled`.

`toka_async_terminal_publisher` is a CTest runtime probe for normal-first and
cancel-first sequential interleavings, the legacy compatibility entry point,
cold cancellation, and 4,000 concurrent normal/canceled publications. This is
evidence only for terminal-publisher arbitration. It does **not** close Phase 5
or Section 8.1 of the TCB RFC: cancellation epochs, queue-publication helping,
subscription arbitration, cleanup aggregates, frame-access retirement, and
the async/PlaceState bridge remain separately unqualified.

### 1.2 Current narrow implementation evidence: cold cleanup before terminal

New compiler output opts into a cold-finalizer handshake when it creates a
task. A cold request first claims `Created -> ColdFinalizing`, invokes the
coroutine destroy/cleanup callback without a runtime arbiter held, and only
after that callback returns publishes `Pending -> Canceled` and
`CompletedCanceled`. CodeGen defers the physical frame free on that destroy
path so the existing promise/result ABI remains readable through terminal
observation; the final TCB release then frees the already-cleaned frame without
running its destroy entry twice.

`TaskHandle` destruction has a distinct cold-drop entry and therefore takes
that same no-body cancellation path. Explicit `detach` remains an activation
operation for a cold task. Existing three-argument runtime-create ABI users
remain compatible but do not claim this handshake until recompiled.

`toka_async_cold_cancel_cleanup` proves both explicit cold cancellation and
last-handle drop: the installed cleanup callback runs exactly once, observes a
nonterminal task while it runs, and terminal cancellation appears only after
it returns. It also runs 2,000 start/cancel races: exactly one side claims the
`Created` transition, so a cold-cancel winner publishes canceled without a
ready entry, while a start winner supplies one ready task and cannot also take
the no-body cold-finalization path. The
`async_cede_unique_receiver_cold_drop` and
`async_cede_unique_field_receiver_lifecycle` language fixtures additionally
cover compiler-generated frame ownership and exact-once resource cleanup. This
is still a bounded evidence slice, not closure of the RFC's full cold-finalizer
or frame-retirement gates.

### 1.3 Current narrow implementation evidence: result-disposition claim ordering

For TCB-backed promises, `ReadyLive` alone is no longer enough to transfer or
drop a result. A consumer first acquire-observes normal `Completed` and moves
the private disposition from `Unclaimed` to `ClaimedByConsumer`, then
release-publishes public `ReadyLive -> Taken`. Detached and scope owners first
observe the same normal terminal state, claim `Unclaimed -> Dropping`, invoke
the typed drop hook without a runtime arbiter held, and only then publish
`Taken` and `Dropped`. Thus the public state records discharge only after the
unique private transfer/drop claim; canceled completion has no payload claim.

`toka_async_result_disposition` is a CTest runtime probe for rejecting a
premature `ReadyLive` consumer take, canceled no-payload observation, consumer
transfer, both detach/complete orderings, the forced `ReadyLive`-before-
`Completed` detach handoff, a detached canceled terminal, and scope-owned
drain. In that forced handoff the detach moves result ownership but cannot
claim/drop before terminal publication; the terminal publisher later performs
the sole detached typed drop and owner release. A detached canceled terminal
instead exposes no payload and still releases the detached owner. Its typed
drop hook re-enters `toka_task_take_result` while it runs
and observes `ReadyLive`; the re-entry is rejected and cannot deadlock or steal
the result. A 2,000-round concurrent detach/normal-terminal probe covers both
owners racing the same private claim and proves every final TCB reference is
released. This is bounded evidence for result-claim ordering only. The
compiler's async-main,
`.await`, and synchronous `wait` lowerings additionally use an internal result
access guard: the successful private claim transfers a checked TCB retain and,
when a frame exists, one frame pin through the typed payload load. The CTest
probe releases the external owner during that interval and verifies that the
guard keeps the payload readable until it is released. This is not a
source-level API or a public runtime ABI commitment. The older
`toka_task_take_result` compatibility entry also validates a non-null promise
header `self_tcb` through the task registry; a stale frame-less promise header
fails rather than falling back to the promise-only claim path. It does **not**
qualify the TCB RFC's broader frame-retirement protocol, full-token lifetime
validation, aggregate cleanup, or await-resolution/cancellation arbitration.

### 1.4 Current narrow implementation evidence: pre-commit wait rollback

`toka_task_abort_suspend` now invalidates the TCB's active singleton or
wait-set registrations before it restores `Preparing` or
`PreparingWithPendingWake` to `Running`. This removes their retained TCB
references and makes late slot tokens stale; it creates no ready-queue entry
for an attempt that never committed. Timer registration and `sleep_async` call
the abort helper when singleton allocation fails. `race2` likewise aborts after
pair allocation, completion-subscription, or child-cancellation-registration
failure, after it has released the partial resources it acquired.

`toka_async_suspend_rollback` covers singleton rollback and a pair that already
has a pending wake: both leave zero live wait registrations, reject stale
tokens, remain absent from the ready queue, and permit a new suspension
attempt. This is not full RFC conformance for `WonPending` completion,
reactor/timer physical unregistration, completion-progress descriptors, or the
four cleanup-only suspension kinds.

### 1.5 Current narrow implementation evidence: basic completion subscription ordering

The current mutex-protected callback-list substrate has a bounded arm/terminal
ordering: a subscription installed before terminal publication is moved into
the publisher's snapshot and woken outside the runtime arbiter; a subscription
that observes an already normal or canceled terminal task routes an immediate
wake instead. An unsubscribe that acquires the list before the publisher's
snapshot removes its entry and sends no wake. These paths reuse the parent wait
registration's existing schedule claim, so a child completion while the parent
is preparing becomes its one pending wake and commits through the ordinary
`PreparingWithPendingWake -> Queued` path.

`toka_async_completion_subscription` covers subscribe-before-terminal,
normal/canceled terminal-before-subscribe, unsubscribe-before-terminal, and
1,000 concurrent subscribe/terminal races. Every arm/terminal permutation in
this probe yields either one pending wake or the deliberate prior unsubscribe;
none leaves the parent wait slot live or creates a duplicate ready-queue entry.
This is evidence for the old callback-list substrate only, **not** TCB RFC
completion-registry conformance: it lacks full task/wait tokens, `Active ->
Selected -> CommitClaimed -> Inactive` descriptors, independent retained
references, group arbitration, and helpable unsubscribe progress.

### 1.6 Current narrow implementation evidence: queue-publication helping

Each current `Queued` epoch carries a runtime-local schedule-generation ticket
and `unpublished`/`published` bit. The ticket is prepared before the transition
into `Queued`; its first insertion into the ready queue and transition to
`published` occur while holding the runtime arbiter. A later
`toka_task_try_schedule` that observes the same task already `Queued` repeats
the publish attempt: it inserts an unpublished matching ticket exactly once,
or treats an already published ticket as success. Worker dequeue claims
`Queued -> Running` under that same arbiter and clears the ticket, so a late
helper cannot republish the claimed epoch.

`toka_async_queue_publication` deliberately pauses the original publisher
after each real `Created -> Queued`, `Suspended -> Queued`, and
`PreparingWithPendingWake -> Queued` claim, but before physical insertion. It
also pauses publication after a two-slot WaitSet has been logically unlinked
by task cancellation and after a natural source winner has selected a two-slot
WaitSet, both while the parent is preparing and after it has suspended. The
probe runs eight concurrent helpers for 100 rounds on each path. It proves that
forced preemption leaves one ready entry, one worker claim, no late reinsertion
after dequeue, and no active WaitSet slot that can wake the selected epoch
again.

For the pair/n-way API, source selection leaves each physical slot reserved as
an inactive outcome record: the resumed consumer can still query which token
won and explicitly release its retained TCB and descriptor reference, but
neither the winner nor loser is event-eligible and the active registry count is
already zero. The selected source unlinks the complete group before queue
publication. The descriptor remains alive after its committed logical
uninstall until the last outcome slot releases it; that is distinct from
scheduler handoff.

This is a narrow queue-publication substrate only, **not** Section 8.1 queue
publication conformance. It uses a schedule generation rather than the full
normative scheduler token, and it does not implement queue-allocation failure,
the complete cancellation arbitration, or the task-wide cleanup aggregate.

### 1.7 Execution boundary: freeze the runtime baseline

For the P0 language roadmap, Sections 1.1--1.6 form the current async runtime
baseline. They establish only the following bounded properties at their
respective test gates:

- one terminal publisher and cold cleanup before canceled publication;
- one result-disposition claimant before payload transfer or typed dropping;
- no live registration or ready-queue publication after an aborted suspend;
- bounded completion-subscription arm/terminal ordering; and
- one queue insertion for a claimed scheduled epoch, including the covered
  cancellation and natural WaitSet-winner paths.

The default next implementation step is **not** another Phase-5 slice merely
because a normative TCB interleaving remains unimplemented. A new runtime
change belongs on this baseline only when it fixes a reproduced violation of
one property above, or when a separately accepted feature supplies its own
narrow invariant, failure matrix, and acceptance gate. Otherwise the work
belongs to the long-horizon TCB closure track.

In particular, full tokens and checked retains, cancellation epochs and
aggregates, `WonCommitted`/completion descriptors, frame-access retirement,
and helpable scope close remain coordinated Section 8.1/8.2 closure work. They
are neither implied by this baseline nor a blocker for the synchronous P0
PlaceState-core work. This boundary does not qualify `TaskScope` cleanup or
Scoped Borrowed Tasks; their stated prerequisites remain unchanged.

The provisional division between this semantic baseline and the official
default executor is recorded in
[`async_runtime_contract_boundary.md`](async_runtime_contract_boundary.md).
It deliberately freezes no external runtime ABI.

---

### 1.8 Closure-track entry evidence: identity exhaustion

The separately accepted Async/Place Contract Closure program begins by making
the current task allocator and suspension generation fail closed at `u64` task
or schedule-epoch exhaustion. A failed allocation installs no TCB or frame-map
entry; a failed suspend preparation leaves the task runnable and installs no
wait registration or queue ticket. `toka_async_identity_exhaustion` forces
both boundaries through test-only hooks.

This is deliberately only an AS.0 entry fact. Exhausted wait slots are retired
instead of returning to generation `1`; the separate full task-token and
checked-retain substrate is recorded below. Cancellation-epoch exhaustion and
complete frame retirement remain unimplemented. It is therefore not a claim
that TCB RFC 8.1.5—or any other 8.1 gate—is complete.

### 1.9 Closure-track entry evidence: checked registry retains

Runtime-owned lookup now enters through a private task registry protected by
the runtime arbiter. The registry, ready item, and wait registration carry a
full internal `TaskToken(task_id, task_instance_generation)`; the current
standard-library suspension, timer, reactor, and `race2` paths call token-bound
entry points. Reusing a numeric task slot advances its instance generation, so
an old task token cannot schedule or register a wait for the new TCB. A
successful lookup takes one checked TCB reference; zero cannot be resurrected
and `u32` overflow returns failure without adding a queue or wait registration.
Final release first removes that registry entry, so a later raw pointer
comparison is stale without dereferencing freed memory. `toka_task_try_retain`
is the failure-reporting API used by the standard library; the older void
retain and `(task_id, generation)` entries remain fail-stop compatibility only.
Task-facing start, cancellation, promise observation, token observation, and
terminal-state inspectors likewise first convert their input through that
checked registry path. Await preparation validates both its promise-associated
child and parent before any state/link mutation; cancellation-child enrollment
does the same before transferring its retained child authority. A stale pointer
is a failed operation, never a fallback TCB read.
`toka_async_identity_exhaustion` forces overflow, zero, stale pointers, and
numeric-slot reuse through these paths.

Workers, terminal publishers, typed result disposition, and cold finalizers
now acquire a frame pin before frame access; nested default-executor turns
restore the enclosing worker context before its pin is released. Retirement is
still only the final-reference fail-closed substrate, not the required
reclaimer transaction that revalidates completion, cancellation, subscription,
scope, and cleanup guards. This is not a claim that TCB RFC 8.1.5, 8.1.12, or
8.1.13 is complete.

### 1.10 Closure-track entry evidence: installed WaitSet descriptor

Pair and n-way registration now allocate one internal `WaitSet` carrying a
nonwrapping descriptor token, the parent `TaskToken`, and the prepared schedule
generation. Installation accepts only that exact parent in
`Preparing|PreparingWithPendingWake` with no existing active registration; a
second singleton or group attempt therefore fails before writing a slot. A
group installation records the descriptor token in the parent TCB, and source
selection or teardown clears only that exact token match.

Each installed outcome slot owns one descriptor reference. A natural source
first moves the descriptor from `Waiting` to `WonPending`, records the winner,
and logically unlinks every member. The parent retains a progress-only set link
that blocks nested installation until the commit point clears it. The winner
then holds one private commit reference. The original source, a losing event that
observes its inactive slot, a winner query, or a slot release can use that
retained descriptor to complete the single `WonPending -> WonCommitted`
transition. The committing helper transfers parent-bound completion teardown
outside the runtime arbiter and only then permits the matching queue ticket to
be published. It then release-publishes `Inactive` before any worker or
callback visibility. The immutable winner record remains readable through the
inactive physical outcome slots; their last release frees the descriptor.
Cancellation may only change `Waiting -> Inactive`; it cannot overwrite an
already-selected source winner.

Releasing any still-active member also takes that same group-wide inactive
transition: it clears every sibling before the suspended parent can be queued,
so public member handles cannot leave a partial live WaitSet behind. Once a
source has committed, the slots remain only as inactive outcome records until
the consumer releases them individually.

`toka_async_suspend_rollback` exercises rejected overlapping registrations,
and the queue-publication tests cover source/cancel selection before and after
suspension commit, active-member group teardown, a third-source n-way winner,
and a forced `WonPending` publisher preemption: a losing source helps the same
descriptor to `WonCommitted` and publishes exactly one parent ticket. The same
probe verifies that a task-level cancellation request helps an already selected
descriptor rather than bypassing it with a second wake, and that a selected
source before `commit_suspend` is committed through the pending-wake ticket.
`toka_async_suspend_rollback` additionally forces `abort_suspend` to encounter
a preempted selected descriptor: it commits the existing winner, returns to
`Running` without a ready ticket, and preserves the outcome until it is
released. The same rollback test injects descriptor-creation failure and proves
that pair installation exposes neither member slot, wake, nor output token
before the caller rolls its already-prepared task back to `Running`; n-way
candidate slots likewise remain private until descriptor installation.
`toka_async_terminal_publisher` proves terminal publication both
uninstalls a live group and reaps a preempted selected descriptor's inactive
outcome slots, so a terminal task cannot retain them waiting for nonexistent
user code; both normal and canceled terminal publication exercise that path.
`toka_async_identity_exhaustion` also exhausts the nonwrapping WaitSet token
and proves the rejected n-way installation exposes no slot or output token.
`toka_async_queue_publication` retains old physical outcome slots while the
resumed parent installs a new pair, then proves their late wake/release calls
cannot alter that new descriptor or publish a second ticket.
`toka_async_terminal_publisher` additionally preempts a selected ticket after
the `Queued` claim but before physical insertion; normal and canceled terminal
publication both make every late helper fail closed.
This is an AS.1 entry fact only: complete completion-subscription,
rollback-descriptor, terminal teardown, and cancellation-aggregation protocols
remain unqualified; no TCB RFC 8.1.1, 8.1.4, or 8.1.7 closure is claimed.
TCB RFC 8.1.6 is not owned by this descriptor substrate: it requires the
structured `race2`/`select2`/`TaskScope` cancel-join-drain protocol tracked by
AS.4.

### 1.11 Closure-track entry evidence: completion subscription descriptors

Completion notification no longer records only a reusable `(wait_id,
slot_generation)` pair. Each active internal descriptor carries the checked
child `TaskToken`, the parent's full token, exact wait-slot identity, and (for
a grouped wait) its `WaitSet` token. The descriptor owns one checked child
retain. Terminal publication and explicit unsubscription select the same node
through `Active -> Selected(Publisher|Unsubscriber) -> CommitClaimed ->
Inactive`; only that selected side releases the retain. A repeated arm for the
same child and exact parent wait is idempotent, rather than adding a second
publisher attempt. A terminal-before-arm path validates the same stored parent
identity before routing the immediate wake.

`toka_async_completion_subscription` covers normal and canceled
terminal-before/after-arm paths, duplicate arming, subscribe-versus-terminal,
terminal-versus-unsubscribe races, terminal publication after the external
child-handle reference has been released, and a child terminal publisher
racing a different member of the parent WaitSet. Selecting any other group
member—or canceling the parent group—logically unlinks every descriptor bound
to that exact parent WaitSet and releases its checked child retain before the
parent ticket is published. A further 1,000-round parent-cancel versus child-
terminal race proves those two contenders produce one parent ticket and leave
no descriptor-held child reference. Thus an inactive parent wait cannot keep a
nonterminal child alive as an orphan subscription.

This is only an AS.3 entry fact. It has no general descriptor-helper/hazard
protocol, no task-wide cancellation/await cleanup aggregate, and no
source-cancellation or winner-suppression proof; therefore it does not qualify
TCB RFC 8.1.8 or 8.1.9.

### 1.12 0.x direct-await cancellation barrier experiment

The current single-child await continuation now supplies one additional private
barrier and resolution word:

```text
Idle -> Armed -> ChildNormal -> NormalClaimed -> Idle
             -> ChildCanceled -> Idle
             -> CancelClaimed -> Idle
```

Parent cancellation claims `CancelClaimed` while the await remains armed or
child-terminal-but-unclaimed. A normal child terminal records `ChildNormal`,
and compiler-generated `.await` atomically promotes it to `NormalClaimed`
before taking the typed result; the cancellation and normal claims cannot both
win. A canceled child yields `ChildCanceled`. The CodeGen lowering discharges
the private word before it exposes either successor CFG. When parent
cancellation observes an active direct-await child while the parent is
suspended without a `WaitSet`, it requests child cancellation but does not
separately queue the parent. The child terminal continuation remains the sole
parent-resumption edge, after it has published its terminal/result state and
cleared the await link.

`toka_async_completion_subscription` pauses that terminal path after terminal
publication and before the continuation, requests parent cancellation, and
observes no ready ticket. Releasing the continuation produces exactly one
ticket. The same C probe proves that a normal child terminal acquires exactly
one normal claim, a later parent request cannot reselect it, and 1,000 direct
child-terminal/parent-cancel races select cancellation before any normal claim.
The source-level P5 redline also cancels a parent only after it has entered a
direct child `.await`; neither the child nor parent await-successor side effect
may execute. Its adjacent `@Encap` resource probe keeps one parent frame local
live across that await and observes its declared drop exactly once only after
the child cancellation reaches terminal; the child never constructs a normal
result. The existing `.await?` conformance fixture continues to capture both
child and current-task cancellation as `Option::None`. This is a
deliberately narrow 0.x experiment for the service-shutdown shape; it
introduces no public runtime ABI, source spelling, or TKI rule. It does not
provide post-`NormalClaimed` result suppression, a cleanup aggregate, source
cleanup, or multi-child resolution, and therefore does not qualify the full
8.1.9 await-cleanup barrier.

---

## 2. Cancellation Linearization Architecture

### 2.1 TCB State Machine & Active Registration Link
The Phase 5 model expects each `TokaTCB` to track the following fields; their
current implementation and atomic ordering remain subject to Section 6:
- a full `TaskToken(task_id, task_instance_generation)` used by ready, wait,
  join, and cancellation entries; a bare reusable numeric ID is insufficient;
- `_Atomic state`: the complete normative state set, including `Created`,
  `Queued(gen)`, `Running(gen)`, `Preparing(gen)`,
  `PreparingWithPendingWake(gen)`, `Suspended(gen)`, `FinalizingNormal`,
  `FinalizingCanceled`, `Completed`, and `CompletedCanceled`; numeric ABI values
  are not frozen by this subordinate record.
- preparing/suspended epochs carry `Ordinary` or one of four compiler/runtime-
  only cleanup kinds:

  ```text
  AwaitCleanup(cancel_epoch, await_obligation_id)
  ResolutionCleanup(cancel_epoch, await_obligation_id)
  ScopeCleanup(task_cleanup_obligation_id)
  SourceCleanup(source_obligation_id)
  ```

  `AwaitCleanup` requires `Handling` plus a matching `CancelOwned` aggregate and
  `CancelClaimed` await component. `ResolutionCleanup` requires `Requested`
  plus `ResolutionOwned` and `NormalClaimed`. `ScopeCleanup` encodes only the
  stable task-cleanup aggregate id, waits only for validated progress from its
  retained closing descriptors, and revalidates one of three exact current
  key/mode pairs: `ParentEpoch(e) + CancelOwned + Handling(e)`,
  `ParentEpoch(e) + ResolutionOwned + Requested(e)`, or
  `SourceOutcome(source_token, disposition) + SourceOwned + Open|Consumed`.
  `SourceCleanup` drains a canceled source and mandatory losers under
  `SourceOwned` while the
  parent remains `Open|Consumed`; a later parent request may convert that same
  aggregate to `CancelOwned + Handling` without changing the stable aggregate
  id or winning either source- or scope-cleanup WaitSet. Await,
  resolution, and source cleanup admit only their retained child-terminal
  sources; scope cleanup admits only `ScopeProgress`.
- `_Atomic cancel_state`: the normative
  `Open(e) | Requested(e) | Handling(e) | Consumed(e) | Closed` cancellation-
  admission state, or an equivalent combined arbitration word; an
  independently writable boolean is insufficient.
- `lifetime_ref_count` (or an equivalent checked hazard/ownership scheme):
  every `TaskHandle`, `TaskRef`, queue entry, wait/subscription, registry node,
  and helper that may dereference the TCB owns a reference. Retain validates a
  full token under registry protection, never resurrects zero, and fails closed
  on overflow. Frame eligibility does not by itself permit TCB/slot reuse.
- `frame_access_state = Open(pin_count) | Retired` (or an equivalent checked
  guard): every worker resume, final-suspend/terminal publisher, cold finalizer,
  typed claimant, and cleanup callback retains a pin through its last frame
  access. `Open(0)` remains acquirable by a legitimate claimant; after
  revalidating all other frame guards, only the reclaimer may CAS
  `Open(0) -> Retired`. Acquisition from `Retired` fails closed.
- `queue_publication`: the normative `NoTicket | Unpublished(full ticket) |
  Published(full ticket)` state paired with each `Queued(gen)` epoch; scheduler
  insertion uses the TCB RFC's linearizable `publish_once` primitive.
- `WaitSetToken? active_wait_set`: links a preparing or suspended task to the
  one group that arbitrates all of that suspension's registrations. The token
  carries `wait_set_id` and `wait_set_generation`; each member registration
  separately carries its full `WaitToken`. Bare reusable numeric IDs and a
  single-slot active-wait link are forbidden.
- `completion_registry`: the child-terminal subscription arbiter. Every node
  binds the parent full `WaitToken`, child full `TaskToken`, and exactly-once
  retained-reference ownership; a check-then-append callback list is not
  conforming.
- `result_owner`: tagged private authority: `ConsumerOwned(disposition)` or
  `RuntimeOwned(Detached | ScopeNode(full_node_token))`. Activated handle drop/detach uses
  `RuntimeOwned(Detached)` and the two-sided detached drain.
  `RuntimeOwned(ScopeNode(...))` reserves the private claim for that exact
  structured node and routes result readiness through its helpable progress
  protocol; the generic detached helper must reject it. An independently
  sampled detached boolean or untagged runtime owner is insufficient.
- each armed await has an internal
  `AwaitResolution(oid) = Armed | NormalClaimed |
  SourceCanceledClaimed(source) | CancelClaimed(e) | Discharged` word. Its
  joint claim against child terminal kind and parent cancellation, not a sampled
  cancel-state load followed later by result handoff, is the result-disposition
  linearization point.
- `cleanup_obligation`: at most one task-wide aggregate for the current cleanup
  origin:

  ```text
  CleanupKey = ParentEpoch(e) | SourceOutcome(source_token, disposition)
  disposition = Capture | Propagate

  TaskCleanupObligation(CleanupKey, oid) =
    Armed(CancelOwned | ResolutionOwned | SourceOwned, components)
    | Discharged
  ```

  `CancelOwned` and `ResolutionOwned` require `ParentEpoch(e)` and pair with
  `Handling(e)` and `Requested(e)`, respectively. `SourceOwned` requires a
  fixed `SourceOutcome(source_token, disposition)` and pairs with
  `Open|Consumed`. `Capture` owns only the current await/combinator cleanup and
  does not disturb unrelated scopes. `Propagate` also closes structured
  registration and canonicalizes every active scope descriptor into the same
  aggregate before parent finalization. Components are
  optional `AwaitDrain`/`CanceledSourceDrain`, zero or more mandatory losers and
  scope descriptors, and zero or more suppressed frame-value drops. Each names
  exact, non-overlapping result, await, registry, reference-release, and callback
  authority ids. Overlap between a race operand, its structured node, and a
  scope descriptor must reference the existing descriptor or atomically
  transfer-and-tombstone authority; it may not duplicate a component. Finishing
  one component never consumes/closes the parent state. Only the aggregate-empty
  commit may publish aggregate `Discharged` and expose CFG or finalization.

`.await?` produces an explicit continuing control-flow outcome; it does not set
a persistent `cancel_handled` bit. A current-task request consumes its exact
epoch directly only when no cleanup aggregate is armed. Otherwise the task
builds or extends the canonical aggregate, quarantines CFG under the mode above,
and consumes/closes only after every component is discharged. A post-
`NormalClaimed` request stays `Requested(e)` through `ResolutionCleanup` and
any required `ScopeCleanup`; it cannot claim the child result again and may
typed-drop an already transferred winner through one keyed frame-value
component. `SourceCanceledClaimed` creates no parent epoch: its fixed
disposition drains remaining operands under `SourceOwned`; `Capture` reaches an
explicit no-value boundary without disturbing unrelated scopes, while
`Propagate` first folds every active scope descriptor into that aggregate and
then propagates source cancellation. The eventual body return or unhandled
cancellation/source outcome chooses the normative finalization transition.

### 2.2 Linearization Invariant
When `toka_task_request_cancel(tcb)` competes with an active wait:

1. It acquire-validates and retains the full `active_wait_set` token and the
   matching group, then checks that the TCB still links that same group.
2. It first checks cancellation state, aggregate mode, and suspension kind.
   An existing `Requested(e)` or `Handling(e)` is a duplicate: it may help
   already selected cleanup work but cannot win a still-waiting
   `AwaitCleanup`, `ResolutionCleanup`, `ScopeCleanup`, or `SourceCleanup`
   group, consume the epoch, or create another wake. A request encountering
   `Armed(SourceOutcome(source_token, disposition), SourceOwned)` with
   `Open|Consumed` is the other cleanup-set exception: one
   cancellation/cleanup-arbiter transaction admits the next checked epoch,
   re-keys and converts the same aggregate to
   `ParentEpoch(e) + CancelOwned + Handling(e)`, and adds any newly required
   scope/value components without winning, uninstalling, or changing the kind
   of an active `SourceCleanup` or source-owned `ScopeCleanup` set. Only an
   `Ordinary` `Waiting` group with parent `Open|Consumed` attempts the shared
   `Waiting -> WonPending(TaskCanceled)` CAS. Per-slot winner CAS is forbidden.
3. Only the chosen descriptor, or a bounded helper completing that same
   descriptor, may admit the next `Requested(e)` epoch and claim the matching
   TCB schedule action. It then publishes `WonCommitted`, invalidates every
   group slot, clears the matching TCB link, and calls `publish_once` for the
   claimed full queue ticket. A matching unpublished ticket inserts once, a
   matching published ticket is a no-op, and `NoTicket`/mismatch rejects.
4. If ready, timeout, or completion already selected the group, a wait-local
   cancellation event returns without side effects. A task-level cancellation
   request instead bounded-helps committed logical uninstall and retries the
   TCB cancellation/finalization arbiter; it cannot be lost merely because the
   old wait already has a winner. Stale full tokens still fail without a TCB
   dereference.
5. An unhandled cancellation runs coroutine cleanup before it publishes
   `TOKA_TCB_COMPLETED_CANCELED`. Explicit `.await?` capture may continue to a
   later normal domain result, but cannot fabricate a `T` or bypass the same
   one-shot terminal/result protocol.

Each cleanup completion commits one component under the aggregate arbiter,
then performs physical reference release with no runtime arbiter held. If components
remain, the commit selects the next permitted cleanup kind while preserving
`Requested|Handling` (or `Open|Consumed` for unconverted `SourceOwned`). If the
component set becomes empty, one non-suspending final commit discharges the aggregate and
either consumes/closes the task-cancellation epoch or propagates/captures the
source-only outcome. No component may expose user CFG, finalization, or a
second result claim on its own.

Tasks without an active registration use the TCB cancellation/terminal
arbitration defined by the normative RFC. In particular, cold `Created`
cancellation must claim the task, destroy frame-owned obligations without
running the body, and only then publish terminal cancellation. The exact current
implementation remains subject to the closure gates in Section 6.

Completion subscription uses the child TCB RFC's terminal/registry arbiter.
After the fully initialized parent set is linked/`Waiting`, the subscriber
holds the parent install/uninstall arbiter and then the child registry arbiter,
revalidates that same full parent token/link/state, and either installs an
`Active` full-token node before terminal or acquire-observes terminal and
immediately routes the same `ChildTerminal` event without installing. A group
winner serializes through the parent arbiter, so a node cannot be linked after
the set has stopped waiting. Terminal publisher and logical unsubscribe compete
`Active -> Selected(Publisher|Unsubscriber)`; any owner/helper must then win the
single `Selected -> CommitClaimed` transition before it may attempt the
publisher event (if selected), remove the node, release retained references,
and publish `Inactive`. The child registry is closed and terminal/result state
is release-published before `Selected(Publisher)` becomes visible; a committer
acquires that terminal state before sending `ChildTerminal`. If a publisher
event wins the parent group, its source
node reaches `Inactive` before whole-group teardown. A waiter retains the node
and releases both arbiters before waiting; waiting while holding the parent
arbiter is forbidden. Selector/helper descriptor refs keep node storage and its
TCB/WaitSet refs alive across logical unlink; physical free waits for
`Inactive` and every such ref release. A helper obtains its first descriptor
reference only under the child registry arbiter or from an already retained
hazard—never by loading a bare pointer before ref-increment. The unique
`CommitClaimed` owner action is inline, bounded, non-suspending, invokes no user
cleanup, and cannot enqueue work to the same executor and wait for it.

Any result/frame cleanup ownership claimed while a runtime arbiter is held
retains the TCB/frame, releases every scheduler, wait, completion, scope,
terminal, and cancellation arbiter, and only then invokes the compiler-installed
typed callback. `Taken` or terminal state is release-published after the
callback returns; cleanup code is never invoked under those arbiters.

---

## 3. Loser Cleanup Guarantee (`race2`)

The intended conforming sequence for
`race2(cede first, cede second) -> async RaceWinner` is:

1. **Preparation**: Allocate a 2-slot `WaitSet` bound to the parent
   `(TaskToken, task_schedule_generation)` and subscribe both children before
   activating either cold handle.
2. **Logical uninstall**: Acquire-capture the immutable winner descriptor and
   retained authorities, complete/observe `WonCommitted`, invalidate both
   registrations, clear the matching `active_wait_set`, and disarm the old
   teardown obligation before any callback, return, unwind, or nested wait.
3. **Joint resolution**: Under the cancellation/cleanup arbiter, resolve the
   selected child's terminal kind, parent state, and canonical task-cleanup
   aggregate in one transaction:

   - selected `Completed` plus parent `Open|Consumed` claims
     `NormalClaimed`, authorizes exactly one typed winner disposition, and
     records the mandatory loser component;
   - selected `CompletedCanceled` plus parent `Open|Consumed` claims
     `SourceCanceledClaimed(source)`, fixes the boundary's `Capture|Propagate`
     disposition, creates
     `TaskCleanupObligation(SourceOutcome(source_token, disposition), oid)` in
     `SourceOwned` mode for the canceled source and loser components, and
     creates no parent cancellation epoch; `Propagate` additionally closes
     registration and folds every active scope descriptor into that aggregate,
     while `Capture` does not disturb unrelated scopes;
     or
   - an already admitted parent `Requested(e)` claims `CancelClaimed(e)`,
     builds/extends `TaskCleanupObligation(ParentEpoch(e), oid)` in
     `CancelOwned` mode from both operands and overlapping structured work, and
     jointly enters `Handling(e)`.

   A race operand, its structured node, and a scope descriptor may share
   identity but cannot duplicate result-disposition or retained-reference
   authority in different components.
4. **Join and drain**: Only `NormalClaimed` moves a selected payload into one
   armed typed winner temporary. The combinator cancels every non-winning or
   source-cleanup operand and waits until it is terminal, all registrations are
   inactive, and its result obligation is discharged. A live normal loser
   result is privately claimed once, typed-dropped, and only then published
   `ReadyLive -> Taken`; a canceled operand has no payload.
5. **Aggregate commit**: Completing one operand commits only its component.
   If `NormalClaimed` remains selected and no parent request suppresses it, the
   final disposition publishes `Discharged` and constructs exactly one
   `RaceWinner`. If the first terminal child was canceled, no `RaceWinner` is
   constructed: `SourceCleanup` drains the other operand and, for `Propagate`,
   `ScopeCleanup` drains the active scope descriptors before the aggregate-
   empty commit propagates that source cancellation or reaches an explicit no-
   value boundary. A parent request arriving during source-owned cleanup re-keys
   and converts that same aggregate, preserving its `oid` and components, to
   `ParentEpoch(e) + CancelOwned + Handling(e)` without winning the cleanup
   group.

If a post-`NormalClaimed` request is admitted while loser or scope cleanup
remains, only `ResolutionCleanup(e, await_obligation_id)` and
`ScopeCleanup(task_cleanup_obligation_id)` are permitted. The `ResolutionOwned`
aggregate preserves `Requested(e)`, the typed winner witness, and every named
loser/scope/value obligation until the aggregate-empty commit; repeated task
cancellation coalesces outside those groups.

If parent task cancellation wins before normal/source resolution, both operands
become cancel-join-drain components and no winner is constructed. If a child-
canceled source claim wins first, source cleanup and a racing parent request must still
produce exactly one ordered outcome: source-only propagation/capture, or
conversion to the parent-cancellation aggregate. Neither path may fabricate a
typed winner or repeat an operand claim.

A conforming implementation must not expose any `race2` outcome until every
non-returned operand is terminal, disarmed, and result-discharged. Terminal state
alone is not this predicate.

---

## 4. Retained Lifetime (`TaskRef` & `TaskScope`)

The Phase 5 retained-lifetime target is:

- `TaskScope` manages tasks using `TaskRef` objects holding the full `TaskToken`
  and checked `lifetime_ref_count` ownership.
- Owning heterogeneous enrollment consumes the typed `TaskHandle` in one
  transaction: validate/retain the full token, transfer
  `ConsumerOwned -> RuntimeOwned(ScopeNode(full_node_token))`, link that exact
  node with typed-drop disposition, and only then activate or expose the child.
  The generic detached drain must reject this owner; result readiness routes to
  the node's progress protocol. Failure before commit leaves caller cleanup
  authority intact; failure after commit joins the registry's closing
  descriptor. The current split
  `task_ref_from_handle(cede handle)` followed by `track_ref`/`start` does not
  establish this ordering and is not Phase-5 conformance evidence. A plain
  retain/join-only `TaskRef` has no result disposition and cannot be used to
  steal or wait for an externally consumer-owned payload.

The initial standard-library repair removed that public split. The next runtime
substrate moves the child collection behind an opaque `TaskScope` registry. Its
`Open | Closing | Closed` word and child list share the runtime arbiter:
`spawn_into(scope, cede task)` links only while `Open`, clears the consumed
handle before activation, and returns `Err(TaskHandle<T>)` unchanged if closing
already won. Explicit `close()` and the timed-out branch of `shutdown_async`
publish `Closing` before cancellation and publish `Closed` only after every
linked child is terminal and its scope-held reference is released. A scope
created while a task is running is also registered in that parent's
mutex-protected scope list; parent cancellation snapshots a temporary scope
reference, publishes `Closing`, then requests child cancellation outside the
arbiter. Enrollment observes the parent's request under the same arbiter, so a
late child is rejected before transfer. The current substrate additionally
transfers `Consumer -> Scope` result authority during accepted enrollment and
installs a compiler-generated, return-type-specific drop hook. `reap_finished`,
successful `finish_close`, and scope destruction's fallback transfer to the
detached owner first acquire-observe `Completed`, publish
`Unclaimed -> Dropping`, retain the TCB across the callback, invoke that hook
only after releasing the registry arbiter, then release-publish
`ReadyLive -> Taken` and `Dropped`. `await`, `wait`, and async entry result
extraction first acquire-observe `Completed`, claim the same private word as
`Unclaimed -> ClaimedByConsumer`, then publish `ReadyLive -> Taken` before
moving the payload, so a later handle drop cannot repeat the disposition.

This remains a deliberately restricted result-disposition substrate, not
Phase-5 conformance: `Dropping`/`Dropped` is only a per-TCB completion marker,
not a descriptor that can aggregate cancellation reason, scope progress, and
helpable completion. It still lacks full-token validation, reason/aggregate
arbitration, those aggregate callback-completion descriptors, helpable close
progress, and TCB/slot retention independent of frame eligibility.
- Its registry has `Open | Closing(reason) | Closed` state under the same
  parent-cancellation/close arbiter used for child registration. Registration
  either links `Tracked` (and, for a new child, does so before activation/
  exposure), acquire-observes an already terminal child for immediate drain,
  or observes `Closing` and joins that still-live cancel-join-drain descriptor.
  Observing `Closed` rejects before lifecycle/result-authority transfer,
  releases the temporary retain, and leaves the full typed handle/cleanup
  authority with the caller. If cancellation is `Requested|Handling` while the
  registry is still `Open`, registration either creates/joins one concrete
  `Closing(TaskCanceled(epoch), descriptor)`, adds its canonical
  `ScopeDescriptor` component to the task-wide aggregate, or rejects before
  authority transfer. `Requested + ResolutionOwned` preserves `Requested` and
  extends that aggregate; `Handling + CancelOwned` extends its matching
  aggregate; an unobserved `Requested` may jointly establish
  `Handling + CancelOwned`. It cannot create a parallel scope obligation or
  duplicate authority already represented by an await/race component. A
  snapshot followed by an unlocked close flag is insufficient.
- An observer registration retains its own valid TCB reference even if an
  external `TaskHandle` is later detached or dropped. An owning enrollment has
  no later ordinary drop of the consumed handle; its committed registry/runtime
  references replace that obligation.
- Parent/scope-before-child is the only nested lock order. Close/cancel holds the
  scope arbiter only to publish `Closing`, select nodes, retain immutable work,
  and update in-flight accounting. It releases that arbiter before requesting
  cancellation, installing/waiting on progress, claiming results, decrementing
  delegated references, or invoking typed cleanup. A child terminal publisher
  releases the child arbiter before acquiring the scope arbiter.
- Every selected node is a helpable state machine:

  ```text
  CommitClaimed(CancelPending)
    -> WaitingTerminal
    -> WaitingNoActive
    -> ResultPending
    -> CallbackClaimed(owner)?
    -> ReleaseReady
    -> Inactive
  ```

  Helpers may request cancellation and advance bounded phases. The sole
  `CallbackClaimed` owner retains the frame, releases all runtime arbiters,
  performs the non-suspending typed drop, and release-publishes progress.
  `ReleaseReady -> Inactive` uniquely owns registry removal, in-flight
  decrement, and retained-reference handoff. Observer-only nodes skip result
  phases and leave external `ConsumerOwned` results untouched.
- `ScopeProgress` uses a subscribe-or-observe arm handshake. Scope-cleanup
  prepare/commit first revalidates the stable aggregate id and its exact current
  key/mode pair; a source-to-parent conversion leaves the suspension kind
  unchanged. After the parent publishes and links a fully initialized
  `ScopeCleanup` WaitSet, it enters the retained descriptor's progress arbiter
  and revalidates the full parent
  `WaitToken`, active-set link, `Waiting` state, descriptor identity, and phase
  snapshot. If phase already advanced or the descriptor is `Closed`, it links
  nothing and routes one immediate progress attempt after unlocking; otherwise
  it links `Active` before unlock. Phase publication and unsubscribe compete
  through equivalent `Active -> Selected -> CommitClaimed -> Inactive` states.
  A selected publisher retains descriptor/parent references, releases the
  descriptor arbiter, and only then attempts the parent group CAS. Terminal,
  no-active, result-drop completion, node `Inactive`, and descriptor `Closed`
  all publish progress. This covers progress-before-arm, arm-before-progress,
  and progress-during-arm without a new generation domain.
- A conforming close drains every selected node through `Inactive`; only an
  empty registry with all node-owned result dispositions discharged and no in-
  flight selected descriptor may publish `Closing -> Closed`. Each closed
  descriptor removes only its canonical aggregate component. Remaining await,
  loser, value, or scope components select the next cleanup kind without
  consuming/closing the parent state; only the task-wide aggregate-empty commit
  can do so. A future typed join API must define a separate typed reservation/
  return channel; `TaskScope.close` does not transfer results.
- The retained references are the intended UAF-prevention substrate. Frame
  release follows the TCB's five frame guards, including irreversible frame-
  access retirement; the TCB and task-registry slot
  remain live until every owner/handle/`TaskRef`/queue/wait/subscription/helper
  reference is released. If frame and TCB are coallocated, reference zero is
  also a frame-free guard. The record does not claim closure until cancellation,
  join, result discharge, frame reclamation, and TCB lifetime pass the gates
  below.

## 5. Required Cancellation and Synchronous Wait Boundaries

- `race2` installs completion subscriptions before activating either input. It
  then idempotently starts both inputs, so cold `TaskHandle` values are valid
  race operands.
- A structured combinator registers each child TCB with its parent cancellation
  context through the shared registry arbiter. Parent cancellation atomically
  closes open registration and selects all tracked children; a racing
  registration either creates/enters a concrete closing descriptor or is
  rejected before ownership transfer; register-after-`Closed` cannot resurrect
  a completed descriptor. Natural completion and close compete
  `Tracked -> ChildSelected | CloseSelected -> CommitClaimed -> Inactive`;
  selected nodes remain in in-flight accounting until terminal/no-active state,
  their node-authorized result disposition, and retained references are all
  discharged. Every owning selected child's timer/IO registrations and result
  obligation are discharged before the canceled parent frame is released;
  observer-only registration never grants result authority.
- Async `.await` propagates `CANCELED` through the current coroutine and does
  not fabricate a `T` payload. A synchronous `.wait`/`block_on` encountering
  an unhandled canceled task is a non-returning runtime error; callers needing
  recoverable cancellation must use `.await?`. This explicit outcome boundary
  produces `Option<T>`: `Some(T)` consumes a normal result and `None` captures
  a cancellation from the current or awaited task. A task that handles its own
  cancellation through this boundary may return a normal domain outcome; the
  runtime records that completion as `COMPLETED`, not as an unhandled canceled
  task.
- Child-terminal delivery, a direct `WaitOutcome::SourceCanceled`, and parent
  cancellation jointly claim the await's `AwaitResolution` under the
  cancellation/cleanup arbiter. A normal child plus parent `Open|Consumed`
  claims `NormalClaimed` and authorizes one typed transfer. A canceled
  child/source plus parent `Open|Consumed` claims
  `SourceCanceledClaimed(source)`, fixes the boundary's `Capture|Propagate`
  disposition, and has no `T`; remaining work uses
  `TaskCleanupObligation(SourceOutcome(source_token, disposition), oid)` in
  `SourceOwned` mode. `Capture` owns only this await/combinator's components;
  `Propagate` also closes structured registration and adds every active scope
  descriptor. A single canceled await with no asynchronous component left,
  including no scope required by `Propagate`, performs its bounded no-payload
  validation and delegated reference release and publishes
  `SourceCanceledClaimed -> Discharged` without installing an empty aggregate.
  Only after that release may `Capture` continue with parent `Open|Consumed`, or
  `Propagate` jointly close it with canceled finalization. A pre-claim parent
  `Requested(e)` instead claims `CancelClaimed(e)`, establishes
  `TaskCleanupObligation(ParentEpoch(e), oid)` in `CancelOwned` mode, and jointly
  enters `Handling(e)`. A post-normal-claim request establishes or extends
  `ParentEpoch(e) + ResolutionOwned`, preserves `Requested(e)`, and cannot claim
  the child result again.
- An await whose parent task cancellation wins does not immediately enter user
  CFG or terminal frame cleanup. After uninstalling the old parent WaitSet, an
  internal await-cleanup continuation cancels and joins the retained child to
  terminal/no-active-registration, then typed-drops any normal result while
  retaining the child reference and aggregate component. It may suspend only
  with the shielded `AwaitCleanup(e, await_obligation_id)` kind on a new child-
  terminal-only WaitSet; repeated task cancellation coalesces and cannot win
  that group.
  After any callback returns outside runtime arbiters, a component commit proves
  result discharge, publishes `CancelClaimed -> Discharged`, and removes only
  that exact authority. Delegated decrements run outside all arbiters. If
  components remain, the same commit selects the next permitted
  `AwaitCleanup`, `ResolutionCleanup`, `ScopeCleanup`, or `SourceCleanup` while
  preserving the mode-appropriate parent state. Only the aggregate-empty final
  commit may publish aggregate `Discharged` and consume/close a parent epoch, or
  publish `SourceCanceledClaimed -> Discharged` and capture/propagate the fixed
  `SourceOutcome` disposition. `Capture` preserves parent `Open|Consumed`;
  `Propagate` jointly closes it with canceled finalization after all required
  scope descriptors are closed. User CFG and parent terminal cleanup are
  invisible until that commit and its delegated releases finish.

## 6. Pending Phase 5 Closure Gates

Phase 5 remains open until current-revision evidence proves all of the following
against the normative TCB RFC:

1. **CAS-first cancellation:** ready, timeout, completion, and cancellation
   races across every slot have one group winner; only a winning/helped
   `TaskCanceled` descriptor admits
   `Open(e)|Consumed(e) -> Requested(e+1)` or claims its cancellation wake.
   `ChildTerminal`/source-canceled winners schedule without changing the parent
   epoch. Losing and stale wait-local events have no side effects. A task-level
   request that encounters a ready/timeout winner
   helps or observes committed uninstall and retries the TCB arbiter, without
   duplicating queue publication or losing the request.
2. **Result take/drop:** a normal result is published `Pending -> ReadyLive` and
   only after acquire-observing normal terminal completion exactly one claimant
   performs private `Unclaimed -> Claimed`, transfers or invokes the compiler-
   installed typed drop entry, and then publishes the frozen public transition
   `ReadyLive -> Taken`. A second claim fails;
   canceled completion has no readable payload, and all publications obey the
   normative release/acquire edges. A re-entrant destructor probe proves typed
   result/frame cleanup is invoked with no runtime arbiter held.
3. **Cold no-body cleanup-before-terminal:** canceling a `Created` task or
   dropping its last handle never runs its body, drops frame-owned `cede`
   parameters and other armed obligations exactly once, and publishes/notifies
   terminal cancellation or reclaims the frame only after that cleanup.
4. **Suspend rollback:** every allocation or registration failure after
   `prepare_suspend` aborts the suspension, completes or invalidates any
   `WonPending` descriptor, disarms partial timer/reactor/completion/progress/
   parent-cancellation registrations, restores a runnable TCB state, and
   releases all temporary wait-set/slot, TCB, and descriptor references without
   resetting or discharging task-wide cleanup. The four internal kinds retain
   their exact pre-attempt authority: `AwaitCleanup` preserves
   `Handling + CancelOwned + CancelClaimed`; `ResolutionCleanup` preserves
   `Requested + ResolutionOwned + NormalClaimed` and every winner/loser/value
   witness; `ScopeCleanup` preserves its stable aggregate id, exact current
   key/mode pair, retained descriptors, and phase snapshots—including the
   source-owned or atomically converted form; and `SourceCleanup` preserves
   `SourceOutcome(source_token, disposition) + SourceOwned` with
   `Open|Consumed + SourceCanceledClaimed`, or the same components after atomic
   conversion to
   `ParentEpoch(e) + CancelOwned + Handling(e)`. Only nodes installed by the
   failed WaitSet attempt are invalidated.
5. **Identity freshness:** stale ready, wait, join, and cancellation entries
   carrying an old task-instance, wait-set, or wait-slot generation cannot
   retain, schedule, drain, or release a newer TCB/group that reused the
   numeric slot; every generation domain fails closed at exhaustion.
6. **Structured join consumers:** `race2`, `select2`, and explicit `TaskScope`
   close paths do not return until losing/canceled children are terminal, their
   registrations are disarmed, and their result/frame obligations are
   discharged. The old winner WaitSet is logically uninstalled before the
   loser cancel/join path may install another suspension. This does not
   establish lexical borrowed-task semantics.
7. **Queue publication:** forced preemption after a successful TCB queue claim,
   after `WonCommitted`, and after logical uninstall proves a helper publishes
   the same full ticket exactly once. A late helper after worker dequeue sees
   `NoTicket`/generation mismatch and cannot reinsert or resume the epoch.
8. **Completion subscription:** completion-before-subscribe,
   subscribe-before-completion, normal/canceled terminal publication,
   parent-arm-vs-child-terminal, another-source-wins-during-arm, publisher-vs-
   unsubscriber, and stale child/parent token permutations prove one
   `ChildTerminal` group attempt and exactly one registry removal/retained-
   reference release, with no orphan node or lost wake. Initial helper ref
   acquisition is registry/hazard protected, and a forced single-worker run
   proves the bounded `CommitClaimed` action never waits on queued self-work.
9. **Await resolution and task-wide cleanup barrier:** parent cancellation at
   every child lifecycle point, normal/canceled child terminal delivery, direct
   `WaitOutcome::SourceCanceled`, and cancellation immediately before/after the
   joint resolution prove exactly one `NormalClaimed`,
   `SourceCanceledClaimed`, or `CancelClaimed`. A pre-claim request jointly
   performs `Requested -> Handling`; a post-`NormalClaimed` request remains
   `Requested` under `ResolutionOwned` and cannot reselect the result. Single-
   child canceled capture/propagation, first-child-canceled and both-canceled
   `race2`, and a parent request during `SourceCleanup` or source-owned
   `ScopeCleanup` prove that no `T` or `RaceWinner` is fabricated. `Capture`
   preserves `Open|Consumed` and leaves
   unrelated scopes alone; `Propagate` closes registration and drains every
   active scope descriptor before canceled finalization. The atomic
   conversion from `SourceOutcome(source_token, disposition) + SourceOwned` to
   `ParentEpoch(e) + CancelOwned` jointly performs
   `Open|Consumed -> Handling(e)`, retains the aggregate `oid`, does not change
   an active `ScopeCleanup` kind, and neither wins that progress set nor loses
   or duplicates any source/loser/scope/value component.

   Forced single-worker cases exercise `AwaitCleanup`, `ResolutionCleanup`,
   `ScopeCleanup`, and `SourceCleanup` and prove that each kind accepts only its
   declared child-terminal or `ScopeProgress` source. The old set is inactive
   before an internal cleanup set is installed; repeated task cancellation
   coalesces outside that set. Component commits preserve the mode-appropriate
   `Requested|Handling|Open|Consumed` state, perform typed callbacks and
   delegated releases outside arbiters, and cannot expose CFG or finalization.
   A component commit may complete its matching `AwaitResolution` and remove
   only that exact authority. Except for the bounded single-canceled-await fast
   path with no aggregate, `SourceCanceledClaimed` remains armed until the same
   aggregate is empty, whether its key is still `SourceOutcome` or has converted
   to `ParentEpoch`. Only that aggregate-empty commit publishes the
   `TaskCleanupObligation` and retained resolution witness as `Discharged` and
   consumes/closes or preserves the parent state according to its key and
   disposition. Every normal child result
   is transferred or typed-dropped exactly once, and parent-canceled `race2`
   drains both operands without constructing a winner.
10. **Detach/complete race:** detach before terminal, terminal before detach,
    detach between `ReadyLive` and `Completed`, canceled completion, and
    concurrent drain helpers all invoke the same acquire
    `try_drain_detached_result` protocol only for `RuntimeOwned(Detached)`.
    Exactly one runtime claimant typed-drops a live detached result and publishes
    `Taken`; no ordering strands the obligation or reclaims the frame first.
    `RuntimeOwned(ScopeNode(full_node_token))` is rejected by this helper and
    routes result readiness to that exact node's Gate 11 progress protocol.
11. **Structured-child registry and progress:** register-before-close,
    close-before-register, already-terminal registration, natural completion
    versus close selection, repeated close/cancel help, multiple scopes, racing
    enrollment, and stale-token permutations prove no activated child is
    omitted and no result/reference authority appears in two aggregate
    components. Owning cold enrollment proves
    `ConsumerOwned -> RuntimeOwned(ScopeNode(full_node_token))` and registry
    linking precede activation; `Open + Requested` creates/joins a real closing
    descriptor or rejects before transfer; register-after-`Closed` releases only
    its temporary retain and preserves caller authority. Observer-only nodes do
    not claim or block on an external result.

    Forced preemption and re-entrant/single-worker cases help every node through
    `CommitClaimed(CancelPending)`, `WaitingTerminal`, `WaitingNoActive`,
    `ResultPending`, optional `CallbackClaimed`, `ReleaseReady`, and `Inactive`
    in order, with the unique typed callback outside all runtime arbiters and
    the fixed parent/scope-before-child lock order. Progress-before-arm, arm-
    before-progress, progress-during-arm, stale parent/descriptor identity, terminal/no-active,
    result-callback completion, node `Inactive`, and descriptor `Closed` prove
    the `ScopeProgress` subscribe-or-observe handshake has no lost wake and adds
    no generation domain. `ScopeCleanup` binds the stable aggregate `oid` and
    revalidates its exact current `ParentEpoch + CancelOwned + Handling`,
    `ParentEpoch + ResolutionOwned + Requested`, or
    `SourceOutcome(source_token, disposition) + SourceOwned + Open|Consumed`
    pair while descriptors remain; source-to-parent conversion preserves the
    `oid` and active progress set. A descriptor
    reaches `Closed` only after every selected node is `Inactive` and all node-
    owned dispositions/references are discharged; closing it removes only its
    canonical component, and only the task-wide aggregate-empty commit may
    consume/close the parent state.
12. **TCB lifetime references:** an extra `TaskRef` retained across terminal,
    result drain, subscription teardown, and eligible frame release keeps the
    TCB and registry slot live and unreused. Stale/overflow retain fails closed,
    zero is not resurrected, and coallocated frame/TCB storage is not freed
    until the final checked reference is released.
13. **Frame-access retirement:** forced preemption after terminal publication and
    concurrent detached `ReadyLive -> Taken`, but before the terminal publisher,
    final-suspend, or cold finalizer releases its last pin, proves the frame is
    not reclaimed. A still-valid claimant may race `Open(0) -> Open(1)`; only a
    reclaimer that revalidates the other guards and wins `Open(0) -> Retired`
    may null/free storage. Stale, overflow, and retired acquisition fail closed.

An ABI baseline, a source-only functional test, or a historical phase result is
supporting evidence, not proof that these gates are closed.
