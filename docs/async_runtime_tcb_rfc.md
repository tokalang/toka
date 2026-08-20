# RFC: Toka Async Runtime Refactoring (TCB, Generation-based WaitTokens, and Cooperative Cancellation)

- **Status**: Approved normative design contract. Implementation conformance and
  current-HEAD requalification are pending.
- **Target Release**: Toka v1.0-async
- **Authors**: Toka Core Runtime Team

This document is the normative authority for TCB, wait-token, cancellation,
result-consumption, and frame-reclamation invariants. Phase specifications and
implementation records are subordinate to it. A phase record may describe the
current implementation, but it cannot weaken or replace a rule in this RFC.
"Approved" therefore means that the design contract is frozen; it does not
claim that the current runtime has passed the conformance gates in Section 8.

---

## 1. Overview & Core Motivation

At this RFC's proposal baseline, Toka supported basic coroutine lowering,
`.await`, `.start`, epoll/kqueue/select event loops, and basic timer functions,
but the design still had these safety and correctness gaps under concurrency,
racing event conditions, or cancellation:

1. **Raw Frame Address Passing**: OS Reactors and Timers directly store raw coroutine frame pointers (`void*`), leading to Use-After-Free (UAF) if a task completes or is destroyed before a late IO/Timer event fires.
2. **Inline Resumption Reentrancy**: Completing an awaited coroutine directly calls `coro_resume` on the awaiter inline, causing deep callstacks and reentrancy deadlocks/double-resumptions.
3. **Lack of Cancel/Timeout Coordination**: Timers cannot be efficiently canceled without $O(N)$ heap scans, and cancelled I/O registrations can cause stale wakeups on reused file descriptors.

This RFC freezes four non-negotiable runtime invariants and establishes the
technical blueprint for Phase 1 through Phase 6. Phase labels describe the
design sequence, not implementation-completion claims.

---

## 2. The Four Non-Negotiable Invariants

1. **No Raw Frame Addresses**: OS Reactor (epoll/kqueue/select) and Timer Heap MUST NEVER store raw coroutine frame addresses.
2. **Scheduler Queueing Only**: All wakeups MUST go through
   `Scheduler.try_schedule(task_token, task_schedule_generation)`. That
   protocol first claims the exact TCB epoch/ticket and, after required logical
   WaitSet teardown, completes its publication half through
   `Scheduler.publish_once(ticket)`. A helper may complete only that existing
   ticket's publication half; it cannot originate an unclaimed wake. Event
   sources and completion paths CANNOT directly invoke `coro_resume`.
3. **Exactly-Once Scheduling**: A single suspension epoch
   (`task_schedule_generation`) enters the ready queue AT MOST ONCE. The atomic
   transition into `Queued(gen)`—from `Created`, `Suspended`, or
   `PreparingWithPendingWake`—creates one full queue ticket, and the scheduler's
   linearizable `publish_once(ticket)` inserts that ticket at most once. Ready-
   queue scanning is not the proof.
4. **Frame Life-Cycle Bound**: A coroutine frame is freed ONLY AFTER:
   - Task reaches terminal completion (`TCBState == Completed` or
     `CompletedCanceled`),
   - the task-wide cleanup aggregate is absent or discharged after every
     canonical await/loser/source/scope/value component has reached its
     required terminal/no-active/result/callback state,
   - the TCB has no active WaitSet link and ALL of its WaitSets and
     `WaitRegistration`s/`CompletionSubscription`s are logically inactive
     (`no_active_registration`), AND
   - its result obligation is discharged: a live normal payload has been
     claimed exactly once and either transferred to a consumer or destroyed by
     its typed drop plan, or cancellation published no payload, AND
   - the frame-access word has been retired after proving no worker, coroutine
     resume/final-suspend path, terminal publisher, typed claimant, or cleanup
     callback can still access the frame.

Detaching a handle transfers the result obligation to the runtime; detach alone
does not discharge it and never permits an untyped frame free of a live result.
These five conditions govern the coroutine frame, not reuse of the containing
TCB or task-registry slot. The TCB remains alive until its lifetime references
also reach zero under Section 5; if the implementation coallocates the frame
and TCB, that zero-reference condition is an additional frame-free guard.

---

## 3. Data Structures, Disambiguation & State Machine

### 3.1 Generation Terminology Disambiguation

To prevent epoch collision, generation counters are split into five
independent domains:

1. `wait_set_generation` (u64): Monotonically managed by `WaitRegistry`
   groups. Identifies one shared winner arbitration for a suspension.
2. `wait_slot_generation` (u64): Monotonically managed by individual event
   slots. Validates a physical event token before it resolves its group.
3. `task_schedule_generation` (u64): Monotonically managed by `TaskControlBlock` (TCB). Incremented at every async suspension point (`suspend_and_register_wait`) to guarantee idempotent ready-queue scheduling.
4. `task_instance_generation` (u64): Monotonically managed by the task registry
   for a numeric `task_id`; distinguishes a new TCB from stale ready-queue,
   join, and cancellation entries that used the same slot.
5. `cancellation_epoch` (u64): Monotonically identifies each admitted task-level
   cancellation request and its one permitted observation/consumption.

No counter may wrap. Increment is checked before an identity becomes visible.
A wait-set, wait-slot, or task-registry slot at `u64::MAX` is permanently
retired and a fresh numeric slot is allocated. A task whose schedule generation reaches
`u64::MAX` refuses another suspension before installing any registration and
follows the ordinary suspend-failure rollback path. A cancellation epoch at
`u64::MAX` rejects a further request without changing task-visible state.
Reusing zero or a wrapped generation is forbidden; resource exhaustion fails
closed rather than admitting an ABA collision.

### 3.2 Data Structure Definitions

```toka
shape TaskToken (
    task_id: u64,
    task_instance_generation: u64
)

shape WaitSetToken (
    wait_set_id: u64,
    wait_set_generation: u64
)

shape WaitToken (
    wait_id: u64,
    wait_slot_generation: u64,
    owner_set: WaitSetToken,
    slot_index: u32
)

shape TaskControlBlock (
    token: TaskToken,
    frame: *void,
    frame_access_state: FrameAccessState, // Open(pin_count) | Retired
    lifetime_ref_count: AtomicU64,
    task_schedule_generation: u64,
    state: TCBState,
    queue_publication: QueuePublicationState,
    cancel_state: CancelRequestState,
    cleanup_obligation: Option<TaskCleanupObligation>,
    result_state: TaskResultState, // Pending, ReadyLive, Taken, Canceled
    result_claim_state: TaskResultClaimState, // Unclaimed, Claimed
    result_payload: TaskResultPayload,
    result_drop_fn: fn(*void),
    frame_cleanup_fn: fn(*void, FrameCleanupMode),
    result_owner: TaskResultOwner,
    // ConsumerOwned(disposition) | RuntimeOwned(Detached | ScopeNode(node_token))
    active_wait_set: Option<WaitSetToken>,
    completion_registry: CompletionRegistry
)

shape WaitSet (
    token: WaitSetToken,
    task: TaskToken,
    task_schedule_generation: u64,
    state: WaitSetState, // Waiting, WonPending(outcome),
                         // WonCommitted(outcome), Inactive
    winner: Option<WaitOutcome>, // write-once stable record retained for resume
    active_slot_count: u32
)

shape WaitRegistration (
    token: WaitToken,
    task: TaskToken,
    task_schedule_generation: u64,
    state: WaitRegistrationState, // Active, Inactive
    source_tag: u32
)

shape CompletionSubscription (
    parent_wait: WaitToken,
    child: TaskToken,
    state: CompletionSubscriptionState,
    descriptor_refs: AtomicU64
    // Active, Selected(Publisher|Unsubscriber),
    // CommitClaimed(Publisher|Unsubscriber), Inactive
)

shape CompletionRegistry (
    state: CompletionRegistryState, // Open, Closed
    subscriptions: CompletionSubscription[]
)
```

`WaitOutcome` is the immutable pair of stable source identity and one of
`Ready`, `Timeout`, `ChildTerminal`, internal `ScopeProgress`,
`SourceCanceled`, or `TaskCanceled`. Only `TaskCanceled` is allowed to admit a
parent `Requested(e)` epoch. `ScopeProgress` is bound to a retained closing-
descriptor/node generation and reports terminal observation, result-
disposition progress, node `Inactive`, or descriptor `Closed`; it is not a
user event.

For each queueable epoch, `QueuePublicationState` is one of
`NoTicket`, `Unpublished(QueueTicket(TaskToken, schedule_generation))`, or
`Published(QueueTicket(...))`. Claiming `Queued(gen)` creates exactly one
matching unpublished ticket. `Scheduler.publish_once(ticket)` is a linearizable
intrinsic with only these outcomes: matching `Unpublished -> Published` inserts
one entry; matching `Published -> Published` is a no-op; `NoTicket` or a
different task/generation ticket rejects without insertion. Worker dequeue
accepts only matching `Published`, atomically consumes that queue entry, and
returns the TCB publication state to `NoTicket` as it claims `Running(gen)`.
Every winner/helper may call `publish_once` for the already claimed ticket;
exactly one queue insertion occurs even if the original scheduling thread is
preempted after the TCB state CAS, and a late helper cannot reinsert an epoch
after worker dequeue.

`Preparing`, `PreparingWithPendingWake`, and `Suspended` also carry an internal
`SuspensionKind`:

```text
Ordinary
AwaitCleanup(cancel_epoch, await_obligation_id)
ResolutionCleanup(cancel_epoch, await_obligation_id)
ScopeCleanup(task_cleanup_obligation_id)
SourceCleanup(source_obligation_id)
```

Ordinary source suspension is admitted only with no outstanding cancellation.
`AwaitCleanup` is compiler/runtime-only and is admitted only while the same
epoch is `Handling(cancel_epoch)`, the task-wide aggregate is
`Armed(CancelOwned, ...)`, and the named await component is `CancelClaimed`.
`ResolutionCleanup` is compiler/runtime-only and is admitted only while the
aggregate is `Armed(ResolutionOwned, ...)`, that obligation is `NormalClaimed`,
and the later parent request remains `Requested(cancel_epoch)`; it shields
mandatory loser/join cleanup without consuming or reclassifying that request.
Both await cleanup kinds admit only their declared child-terminal sources.
`ScopeCleanup` is compiler/runtime-only and is admitted only with a matching
armed aggregate—`CancelOwned + Handling`, `ResolutionOwned + Requested`, or
`SourceOwned + Open|Consumed`—bound by its stable aggregate id to a retained
task-wide set of live `Closing(..., descriptor)` registries. It waits only for validated
`ScopeProgress` from that set; each wake helps/drains committed node phases and
may re-arm for the remainder. A task request against source-owned scope cleanup
atomically re-keys the same aggregate but does not win or uninstall that
progress set; other repeated task cancellation coalesces outside it.
`SourceCleanup` is admitted with
`Armed(SourceOutcome(source, disposition), SourceOwned) + Open|Consumed`, or with the same
source components after a parent request re-keys and converts the aggregate to
`Armed(ParentEpoch(e), CancelOwned) + Handling(e)`. It waits only for retained canceled-
source/loser terminal progress and by itself admits no parent epoch. The
conversion adds all scope/value cleanup components without letting the request
win the cleanup WaitSet. Every kind is part of the exact
scheduling epoch and cannot be changed by a helper or interface metadata.

Each active await also carries a compiler/runtime-only resolution word bound to
its obligation id:

```text
AwaitResolution(oid) = Armed | NormalClaimed | SourceCanceledClaimed(source)
                     | CancelClaimed(epoch) | Discharged
```

This word is not a source-visible result state. It supplies the single
linearization point between normal/canceled-child result disposition and a
concurrent request to cancel the parent; Section 4.1 defines the joint
arbitration.

Shielded async cleanup is represented by at most one aggregate per task, not
one independently finalizable obligation per child or scope:

```text
TaskCleanupObligation(key, oid) =
  Armed(mode, components) | Discharged

key = ParentEpoch(e) | SourceOutcome(source_token, disposition)
disposition = Capture | Propagate
mode = CancelOwned | ResolutionOwned | SourceOwned
components = {
  AwaitDrain(await_oid)?,
  CanceledSourceDrain(source_token)?,
  MandatoryLoser(child_token)*,
  ScopeDescriptor(scope_token, close_descriptor_token)*,
  SuppressedFrameValue(exact_place, typed_drop_plan)*
}
```

`CancelOwned` requires `ParentEpoch(e)` and is paired with `Handling(e)` when
cancellation won before normal result disposition. `ResolutionOwned` also
requires `ParentEpoch(e)` and is paired with `Requested(e)` after a
`NormalClaimed` disposition; it preserves that request while mandatory cleanup
finishes and may suppress/drop the already frame-owned result. `SourceOwned`
requires `SourceOutcome(source_token, disposition)` and is paired with parent
`Open|Consumed` when an awaited/raced child itself completed canceled; it
drains remaining operands without fabricating a parent cancellation epoch,
then either propagates that source outcome or lets an explicit language
boundary capture no value. `Capture` owns only the current await/combinator
cleanup. `Propagate` also closes structured registration and canonicalizes every
active scope descriptor into the aggregate before parent finalization; it may
not close `Open|Consumed` while an unrelated structured child remains. If a
task-level request wins before source cleanup finishes, one arbiter transaction
changes the same aggregate key and mode
`SourceOutcome(source_token, disposition) + SourceOwned -> ParentEpoch(e) + CancelOwned`,
adds any newly required scope/value components, and performs
`Open|Consumed -> Handling(e)`. The aggregate id and already owned components
do not change.
Component sets are retained and mutated only under the task cancellation/
scope-cleanup arbiter. Their descriptor identities are non-reused while
retained; event freshness uses the existing full `WaitToken` plus that retained
identity rather than a sixth generation domain. Each component names exact,
non-overlapping authority ids for result disposition, await retention, registry
retention, and callback ownership. Building or extending the aggregate
canonicalizes by those ids: the same authority cannot occur in two components.
When an awaited/raced operand is also a structured-registry node, the aggregate
either references that node's existing full descriptor or atomically transfers
the exact authority and tombstones it in the old component; it never duplicates
the result-drop or reference-release authority. A racing structured enrollment
either joins the same set before authority transfer or rejects.

Completing one component never consumes or closes an epoch while another
remains. For a `ParentEpoch(e)` aggregate, only the aggregate-empty final commit
may publish the aggregate and every still-retained matching resolution witness
`Discharged`—including a `SourceCanceledClaimed` preserved by source-to-parent
conversion—and perform the permitted
`Requested|Handling -> Consumed|Closed`; `Closed` jointly claims
`Running -> FinalizingCanceled`. For a `SourceOutcome` aggregate, the empty
final commit publishes both the aggregate and its
`SourceCanceledClaimed -> Discharged`. An explicit no-value capture leaves the
parent `Open|Consumed` state unchanged and resumes only after delegated
releases; ordinary propagation instead jointly performs
`Open|Consumed -> Closed` and `Running -> FinalizingCanceled`. This is source
outcome propagation, not admission of a parent cancellation epoch. There is
therefore no observable `Handling` without its matching armed aggregate and no
gap while switching between await, loser, value-drop, and scope cleanup modes.

`FrameAccessState` is the internal frame-access guard. A worker or legitimate
late result claimant acquires a checked pin with `Open(n) -> Open(n+1)` before
frame access and releases it with `Open(n) -> Open(n-1)` afterward. In
particular, the final-suspend/terminal publisher keeps its pin after release-
publishing `Completed|CompletedCanceled`, through completion-subscription
selection and its last frame access. `Open(0)` is only an inactive snapshot,
not permanent retirement: a still-valid claimant may race it to `Open(1)`.
After acquire-revalidating the other four frame guards, the reclaimer must win
the sole `Open(0) -> Retired` CAS and revalidate those guards before publishing
a null frame/result-storage pointer and freeing storage. A pin acquire that
observes `Retired`, a null pointer, counter overflow, or a guard/token mismatch
fails closed; `Retired` cannot return to `Open`. Terminal publication alone is
not permission to free the frame.

`no_active_registration(task)` is true only when the matching TCB's
`active_wait_set` is empty, every WaitSet owned by that task is logically
`Inactive`, and every member wait registration and completion subscription is
`Inactive` with all descriptor-held references capable of dereferencing that
task/group released, all observed with the acquire edges in Section 5. A zero
slot count, a missing physical reactor entry, a logically unlinked subscription
whose descriptor references remain live, or a group that is merely
`WonCommitted` is not a substitute for the applicable reclamation guard.

The erased callbacks are installed by compiler/runtime-owned lowering from
typed plans. A runtime transition may claim callback ownership while holding an
arbiter, but it first retains the TCB/frame and releases every scheduler, wait,
completion, scope, terminal, and cancellation arbiter before invoking
`result_drop_fn` or `frame_cleanup_fn`. The callbacks are non-suspending, may
discharge only their designated obligations, and cannot free the frame or TCB;
the runtime publishes `Taken` or terminal state only after the callback
returns. This rule permits a destructor to re-enter task/runtime APIs without
self-deadlocking. Frame reclamation remains centralized under Section 5's
guard.

`TaskResultOwner` is a tagged private authority, not a detached boolean.
`RuntimeOwned(Detached)` authorizes the two-sided detached drain.
`RuntimeOwned(ScopeNode(full_node_token))` instead reserves the private result
claim for that retained structured-node descriptor: terminal publication emits
or immediately routes `ScopeProgress(ResultReady)` and never lets the generic
detached helper claim it. The node's helpable `ResultPending` phase performs
the unique claim/drop. Token mismatch fails before result access.

### 3.3 Strict TCB & Wait State Transition Matrices & CAS Order

#### 3.3.1 TCBState Lifecycle Matrix

`TCBState`, `WaitSetState`, `WaitRegistrationState`, `TaskResultState`, and the
compiler's frame-local PlaceState are distinct state sorts. A transition in one
sort may guard or trigger a transition in another, but their enum cases are
never interchangeable. Every ready-queue, wait, join, and cancellation
reference carries its full token; registry lookup validates every generation
and retains the TCB/WaitSet before dereferencing it. A stale token is rejected
without touching a new task or wait group that reused a numeric slot.

| Current State | Target State | Guard / Action | Valid / Illegal |
| :--- | :--- | :--- | :--- |
| `Created` | `Queued(1)` | `start_task(tid)` creates the epoch's queue ticket and invokes idempotent `publish_once` | Valid |
| `Queued(gen)` | `Running(gen)` | `pop_worker_task()` requires and consumes the matching published ticket | Valid |
| `Running(gen)` | `Preparing(gen+1, Ordinary)` | `prepare_suspend(tid)` jointly proves `Open|Consumed`, no active set, and no armed cleanup-only suspension while claiming the next epoch through the cancellation/install arbiter | Valid |
| `Running(gen)` | `Preparing(gen+1, AwaitCleanup(e, oid))` | `prepare_cleanup_suspend` jointly proves `Handling(e)`, matching `Armed(CancelOwned)` aggregate/await component `oid`, and no active set | Valid internal-only |
| `Running(gen)` | `Preparing(gen+1, ResolutionCleanup(e, oid))` | `prepare_resolution_cleanup_suspend` jointly proves `Requested(e)`, matching `Armed(ResolutionOwned)` aggregate, `AwaitResolution(oid) == NormalClaimed`, mandatory child/loser cleanup, and no active set; it does not consume the request | Valid internal-only |
| `Running(gen)` | `Preparing(gen+1, ScopeCleanup(oid))` | `prepare_scope_cleanup_suspend` jointly proves the same stable task-wide aggregate id in a mode-appropriate pair (`CancelOwned + Handling(e)`, `ResolutionOwned + Requested(e)`, or `SourceOwned + Open|Consumed`), its retained live closing-descriptor set has unfinished progress, and no active set | Valid internal-only |
| `Running(gen)` | `Preparing(gen+1, SourceCleanup(oid))` | `prepare_source_cleanup_suspend` jointly proves matching unfinished canceled-source/loser components under `Armed(SourceOutcome(source, disposition), SourceOwned) + Open|Consumed` or atomically re-keyed `Armed(ParentEpoch(e), CancelOwned) + Handling(e)`, and no active set; scope components use `ScopeCleanup`, and the source mode itself admits no parent epoch | Valid internal-only |
| `Preparing(gen, kind)` | `Suspended(gen, kind)` | `commit_suspend(tid)` after all registrations succeed and the joint arbitration proves the cancellation precondition for that same kind and no pending winner | Valid |
| `Preparing(gen, kind)` | `PreparingWithPendingWake(gen, kind)` | An admitted source for that kind wins while suspension is being prepared; records the reason without queueing twice | Valid |
| `PreparingWithPendingWake(gen, kind)` | `Queued(gen)` | `commit_suspend(tid)` creates/publishes the already-won epoch ticket | Valid |
| `Preparing(gen, kind)` or `PreparingWithPendingWake(gen, kind)` | `Running(gen)` | `abort_suspend(tid)` after any allocation/registration failure; disarms partial registrations and preserves the kind's cancellation state | Valid |
| `Suspended(gen, kind)` | `Queued(gen)` | winning source admitted for that kind / `try_schedule` | **Valid (Single transition allowed)** |
| Any state/epoch not admitted by a row above | `Queued(gen)` | Duplicate or stale `try_schedule` attempt | **REJECTED (Returns false)** |
| `Created` | `FinalizingCanceled` | Cold cancellation or last-handle cold release wins an exclusive start/finalization claim; body does not run | Valid |
| `Running(gen)` | `FinalizingNormal` | Body return jointly closes `Open|Consumed`, and proves normal CFG plus no armed async child cleanup; an outstanding `Requested` can never take this row | Valid |
| `Running(gen)` | `FinalizingCanceled` | Unhandled `Requested`, a propagated canceled-child/source outcome under `Open|Consumed`, or discharged `Handling` jointly closes admission after every required async child cleanup | Valid |
| `FinalizingNormal` | `Completed` | Result commit and all remaining frame-local cleanup complete; publish terminal state | Valid |
| `FinalizingCanceled` | `CompletedCanceled` | Frame-local cleanup completes and result becomes `Canceled`; publish terminal state | Valid |
| `Completed` or `CompletedCanceled` | *Any* | Any further transition attempt | **ILLEGAL** |

A cancellation request against `Queued`, `Preparing`, or `Suspended` does not
jump directly to terminal state. It must win the applicable wait/request
arbitration, reach or restore `Running`, and be observed there; only then may
`Running -> FinalizingCanceled` occur. A caught cancellation outcome remains
`Running`; ordinary compiler CFG becomes visible immediately only when no
shielded cleanup is required, otherwise `Handling(e)` quarantines it until the
barrier is discharged. The cold `Created` row is the sole no-body shortcut.

#### 3.3.2 Cancellation admission

`CancelRequestState` is an epoch state:

```text
Open(e) | Requested(e) | Handling(e) | Consumed(e) | Closed
```

It prevents a request racing normal finalization from applying a late side
effect and gives explicit cancellation capture one-time consumption semantics:

| Current State | Target State | Guard / Action | Valid / Illegal |
| :--- | :--- | :--- | :--- |
| `Open(e)` or `Consumed(e)` | `Requested(e+1)` | A task-level cancellation contender wins its applicable wait CAS, or a no-active-wait task request wins this CAS; increment is checked | Valid |
| `Requested(e)` | `Handling(e)` | One cancellation/cleanup-arbiter transaction builds `Armed(CancelOwned, components)` from every unresolved await/race drain and active structured closing descriptor, jointly claims each unresolved await `Armed -> CancelClaimed(e)`, and closes independent scope registration. A `NormalClaimed` request instead uses `Armed(ResolutionOwned, ...)` while remaining `Requested(e)`. | Valid exactly once |
| `Requested(e)` | `Consumed(e)` | The running task explicitly handles the current-task cancellation at a permitted boundary only when no aggregate is armed, or as the final commit of a fully discharged `ResolutionOwned` aggregate | Valid exactly once |
| `Handling(e)` | `Consumed(e)` | The final commit of the matching fully discharged `CancelOwned` aggregate lets `.await?` expose its caught no-value continuation | Valid exactly once |
| `Open(e)` or `Consumed(e)` | same state | The aggregate-empty final commit of `SourceOutcome + SourceOwned` captures the canceled source at an explicit no-value boundary; it discharges `SourceCanceledClaimed` without admitting a parent request | Valid exactly once |
| `Open(e)` at `Created` | `Closed` | Cold cancellation or last-handle cold release jointly wins `Created -> FinalizingCanceled` | Valid |
| `Open(e)` or `Consumed(e)` at `Running` | `Closed` | Normal CFG may win `FinalizingNormal`; an independently propagated canceled child/source may instead win `FinalizingCanceled`; no async cleanup is armed | Valid |
| `Open(e)` or `Consumed(e)` at `Running` | `Closed` | The aggregate-empty final commit of `SourceOutcome + SourceOwned` propagates the canceled source and jointly wins `Running -> FinalizingCanceled`; all components are discharged first | Valid exactly once |
| `Requested(e)` | `Closed` | Unhandled task cancellation with no aggregate, or the final commit of a fully discharged `ResolutionOwned` aggregate, jointly wins `Running -> FinalizingCanceled`; it can never publish normal completion | Valid |
| `Handling(e)` | `Closed` | The final commit of the fully discharged `CancelOwned` aggregate jointly wins `Running -> FinalizingCanceled` | Valid |
| `Requested(e)` | `Requested(e)` | Duplicate request for the outstanding epoch | **NO-OP** |
| `Handling(e)` | `Handling(e)` | A further request coalesces with the in-progress cleanup and may help cancel its retained children; it creates no new parent epoch or wake winner | **NO-OP on parent state** |
| `Closed` | `Requested(*)` | Late cancellation after finalization was claimed | **REJECTED; no side effects** |

`cancel_requested` in pseudocode is the derived predicate that `cancel_state`
is `Requested(e)`. `Handling(e)` means a matching `Armed(CancelOwned)` aggregate
quarantines user CFG; `Requested(e)` may likewise be quarantined by a matching
`Armed(ResolutionOwned)` aggregate after normal result disposition. Neither is
an independently writable boolean. The TCB finalization claim atomically pairs
the permitted transition to `Closed` with the applicable lifecycle transition
into `FinalizingNormal` or `FinalizingCanceled`. An implementation may pack
these states into one atomic word or use an equivalent proven linearization
protocol. This same arbiter is the defined object for a task with no active
wait.

Typed callbacks run outside runtime arbiters while the aggregate and their
component remain armed. After a callback/join completes, a short aggregate
commit marks only that component discharged and transfers any physical
reference decrement to an immediate post-commit releaser. If components
remain, the same commit selects the next
`AwaitCleanup`/`ResolutionCleanup`/`SourceCleanup`/`ScopeCleanup` mode and does
not change the mode-appropriate parent state. Only when the component set is
empty does one non-suspending final commit publish the aggregate `Discharged`
and disarm it. A `ParentEpoch` aggregate then performs its permitted
`Requested|Handling -> Consumed|Closed`; `Closed` jointly claims
`Running -> FinalizingCanceled`. A `SourceOutcome` aggregate instead completes
the matching `SourceCanceledClaimed -> Discharged` and either preserves
`Open|Consumed` for explicit no-value capture or jointly closes it with
`Running -> FinalizingCanceled` for ordinary propagation. Delegated decrements
run with no arbiter held and before user CFG or terminal frame cleanup. No
observer can therefore see `Handling` without an armed aggregate, consume one
component while another is live, or create a new epoch between aggregate
disarm and consumption/closure.

For a request with no active ordinary wait, cancellation admission and its
lifecycle action are fully determined by the table below. The final
`Handling` row is the explicit cleanup-suspension exception and takes priority
even when a shielded child-terminal WaitSet is active:

| Observed TCB state | Required paired action |
| :--- | :--- |
| `Created` | Instead win the joint `Created -> FinalizingCanceled` / cancellation `-> Closed` cold claim; if start won first, retry against the new state. |
| Any nonterminal state with `Requested(e)` | Treat the request as a duplicate. With matching `Armed(ResolutionOwned)`, it may help an already selected `ResolutionCleanup` child-terminal or `ScopeCleanup` progress descriptor/ticket, but cannot win a still-waiting cleanup group, consume the request, or create another epoch/lifecycle transition. |
| `Queued(gen)` with `Open|Consumed` | Admit the next `Requested(e)` epoch and call `publish_once` for the existing ticket; this may help a preempted scheduler but cannot insert a second entry. |
| `Running(gen)` with `Open|Consumed` | Admit the next `Requested(e)` epoch and leave it running until the next explicit observation boundary. |
| `Preparing(gen, Ordinary)` with `Open|Consumed` | Jointly admit the next `Requested(e)` epoch and win `Preparing -> PreparingWithPendingWake` with task-canceled reason; `commit_suspend` queues that won epoch instead of sleeping. |
| `PreparingWithPendingWake(gen, Ordinary)` with `Open|Consumed` | Admit the next `Requested(e)` epoch while preserving the already selected winner/reason; `commit_suspend` remains the sole enqueue and cannot queue twice. |
| `Suspended(gen, Ordinary)` with `Open|Consumed` | Jointly admit the next `Requested(e)` epoch, win `Suspended -> Queued`, and publish its ticket exactly once. |
| Any nonterminal state with `Armed(SourceOutcome(source, disposition), SourceOwned)` and `Open|Consumed` | Under the cancellation/cleanup arbiter admit the next request, atomically re-key the same aggregate as `ParentEpoch(e)`, convert it to `CancelOwned`, add all active scope/value components without duplicating authority ids, and pair it with `Handling(e)`. Help retained work but do not win or uninstall a still-waiting `SourceCleanup` or `ScopeCleanup` set. |
| Any nonterminal state with `Handling(e)` and matching `Armed(CancelOwned)` aggregate `oid` | This is the matching shielded-cleanup rule, including the running continuation between `AwaitCleanup`/`SourceCleanup`/`ScopeCleanup` waits: coalesce with the handled epoch, help the aggregate's retained child/scope work and already selected descriptors, but never win a still-waiting cleanup set or create another parent epoch/wake. |
| `Finalizing*`, `Completed`, or `CompletedCanceled` | The joint finalization claim has closed admission; reject with no side effects. |

Ordinary `prepare_suspend` checks that `cancel_state` is `Open|Consumed` before
registration, and the `Running -> Preparing(Ordinary)` claim performs that
check in the same combined CAS, short lock, or equivalent proven arbiter used
by cancellation admission. `prepare_cleanup_suspend` is one internal exception:
it requires the exact `Handling(e)` and armed obligation encoded in its
`AwaitCleanup(e, oid)` kind and its `Armed(CancelOwned)` aggregate.
`prepare_resolution_cleanup_suspend` requires the exact pending `Requested(e)`,
`NormalClaimed` resolution, and `Armed(ResolutionOwned)` aggregate encoded in
`ResolutionCleanup(e, oid)`. `prepare_scope_cleanup_suspend` requires the same
stable aggregate id and the mode-appropriate pair (`Handling + CancelOwned`,
`Requested + ResolutionOwned`, or `Open|Consumed + SourceOwned`) with
unfinished retained descriptor progress.
`prepare_source_cleanup_suspend` requires
`Armed(SourceOutcome(source, disposition), SourceOwned) + Open|Consumed` or the same source
components after atomic re-keying to
`Armed(ParentEpoch(e), CancelOwned) + Handling(e)`; the unconverted source mode
preserves the absence of a parent epoch.
A separate load followed by a lifecycle CAS is insufficient: cancellation
could otherwise admit `Requested(e)` between them and the task could enter
`Preparing(Ordinary)` with an unobserved request. Ordinary `commit_suspend`
participates in the paired `Preparing` arbitration above; a request racing
after that preparation claim therefore becomes a pending wake rather than a
lost cancellation or a sleeping requested task. Await cleanup kinds admit only
their child-terminal sources; `ScopeCleanup` admits only retained descriptor
progress. All coalesce further task requests. `ResolutionCleanup` preserves
the pending request and `ResolutionOwned` aggregate; `ScopeCleanup` preserves
its mode-appropriate `Requested|Handling|Open|Consumed` state and descriptor
set until every selected node disposition is discharged. `SourceCleanup`
preserves the parent epoch unchanged while canceled-source/loser components
drain.

#### 3.3.3 WaitSet group CAS and cancellation side effects

Every suspension, including an ordinary one-source wait, owns exactly one
`WaitSet`. A one-source wait is a one-slot set; `race2` is a two-slot set.
Individual registrations validate and route events but do not decide the
winner. For an `Ordinary` set, all admitted ready, timeout, child-terminal/
source-canceled, and task-level cancellation contenders compete on its single
`WaitSetState` CAS. `AwaitCleanup` and `ResolutionCleanup` admit only their
declared child-terminal sources; `SourceCleanup` likewise admits only its
retained source/loser child-terminal sources; `ScopeCleanup` admits only validated
`ScopeProgress` from its retained descriptor set. Task-level requests coalesce
outside those groups; the first request against a source-owned cleanup instead
performs the Section 3.3.2 atomic re-keying outside the group and then further
requests coalesce.

```text
Waiting -> WonPending(source_identity, reason)
```

`source_identity` is the stable slot identity or a distinguished task-
cancellation contender. `WonPending` is a write-once, helper-readable
descriptor, not permission for the worker to run. The winner or any bounded
helper must idempotently complete the selected TCB action, publish
`WonCommitted`, logically uninstall the entire set, and only then make a
`Queued` task visible to a worker. Once any source wins, every other
registration or wait-local source loses the same group CAS and is forbidden
from changing the cancellation epoch, scheduling the task, consuming a result,
or applying any other TCB side effect through that set.

> [!IMPORTANT]
> **CAS-First Order Requirement**:
> `try_wake` MUST execute the atomic CAS transition
> `WaitSetState::Waiting -> WonPending(source, reason)` BEFORE applying any TCB side
> effects (such as admitting cancellation). A per-registration CAS is not a
> substitute for this group arbitration.
> ONLY a winning `TaskCanceled` descriptor or a helper completing that same
> descriptor is permitted to perform its next
> `Open|Consumed -> Requested` epoch transition. Every admitted winner/helper
> may perform only its descriptor's schedule action.
> For a task without an active wait, including a cold `Created` task,
> cancellation must likewise win the TCB's cancellation/terminal arbitration
> before it changes task-visible state. A losing wait-local cancellation
> attempt has no side effects; a task-level request follows the retry protocol
> below and never mutates the already selected group as its loser side effect.

Task-level `request_cancel(TaskToken)` is not a one-shot wait-local event. It
is a retrying TCB operation:

1. After retaining and validating the full task token, it snapshots
   `active_wait_set` through the same install/uninstall arbiter.
2. If `cancel_state` is already `Requested(e)`, the call is a duplicate; it may
   help an already selected descriptor/queue ticket but cannot create another
   group winner or epoch. With matching `Armed(ResolutionOwned)` in either
   `ResolutionCleanup` or `ScopeCleanup`, it specifically cannot win a still-
   `Waiting` cleanup set or consume the pending epoch. If `cancel_state` is
   `Handling(e)` for an `Armed(ParentEpoch(e), CancelOwned)` cleanup obligation
   (including its running continuation or an `AwaitCleanup`, converted
   `SourceCleanup`, or `ScopeCleanup` suspension),
   it coalesces with that epoch and helps cancellation of the retained child
   obligations. It may complete an already selected cleanup descriptor/ticket,
   but does not contend to win a still-`Waiting` cleanup WaitSet or create
   another parent wake. Otherwise, if no set is present, it applies Section 3.3.2's TCB
   table. A matching `Armed(SourceOutcome(...), SourceOwned)` cleanup is the
   other exception: the operation applies that table's atomic re-keying and
   mode conversion without contending on the active `SourceCleanup` or
   `ScopeCleanup` set. If the
   matching ordinary set is `Waiting`, it performs the epoch-
   overflow preflight and attempts the group winner CAS.
3. On a group win, it completes that descriptor's cancellation admission,
   lifecycle/schedule action, committed publication, logical uninstall, and
   any matching `publish_once` queue ticket.
   On CAS loss, or when it observes `WonPending`/`WonCommitted`, it applies no
   independent side effect to the old set but bounded-helps the already chosen
   descriptor through committed publication, logical uninstall, and queue-
   ticket publication. It then acquire-retries from the TCB snapshot. Waiting
   for a resume prologue is forbidden because a caller may occupy the only
   executor worker.
4. The operation stops only when the request has admitted one epoch (or found
   that epoch already `Requested`/`Handling`), checked overflow rejects it, or
   the joint finalization/terminal claim rejects it. If a different active-set
   token is observed, the algorithm restarts against that set.

A wait-local source-canceled or child-terminal notification, by contrast,
attempts only its captured set. If it loses or its token is stale, it is a no-
op and never retries against the TCB. If it wins, it schedules the parent with
a stable `SourceCanceled` or `ChildTerminal` outcome but does not mutate the
parent's cancellation epoch. Thus a Ready winner followed by a task-level
cancellation cannot lose the later task request, while a trailing event from
the old wait cannot mutate the task.

Before attempting the group CAS, a task-level cancellation contender performs
a read-only preflight that the current `Open(e)` or `Consumed(e)` has
`e < u64::MAX`. While
that WaitSet is `Waiting`, the task cannot consume or close its own epoch, so a
winner is guaranteed to complete the corresponding checked
`Requested(e+1)` admission. At `u64::MAX`, cancellation rejects before touching
`WaitSetState`; ready/timeout remains able to win. This preflight reserves no
authority and has no task-visible side effect, so CAS-first ordering is
preserved.

| Current group state | Target group state | Trigger / Action Order | Valid / Illegal |
| :--- | :--- | :--- | :--- |
| `Waiting` | `WonPending(source, Ready)` | group CAS records the immutable winner; descriptor/helper performs the matching TCB schedule claim | Valid |
| `Waiting` | `WonPending(source, Timeout)` | group CAS records the immutable winner; descriptor/helper performs the matching TCB schedule claim | Valid |
| `Waiting` | `WonPending(source, ChildTerminal or SourceCanceled)` | group CAS records the immutable wait-local outcome; descriptor/helper performs the matching TCB schedule claim and does not touch the parent cancellation epoch | Valid |
| `Waiting` | `WonPending(source, TaskCanceled)` | only for an `Ordinary` suspension with parent `Open|Consumed`; group CAS records the task-level winner, then descriptor/helper admits at most one next `Requested(e)` and performs the matching TCB schedule claim | Valid |
| `WonPending(outcome)` | `WonCommitted(outcome)` | all selected cancellation/lifecycle effects and the schedule claim are complete; release-publish the stable winner record | Valid commit |
| `WonCommitted(outcome)` | `Inactive` | invalidate every slot, clear only the matching TCB active-set token, and disarm the wait teardown obligation before ready-queue visibility or another suspension | Valid teardown |
| `Waiting` | `Inactive` | `abort_suspend` invalidates a set whose suspension never committed | Valid rollback |
| `WonPending(*)`, `WonCommitted(*)`, or `Inactive` | `WonPending(*)` | Any trailing registration/wait-local arrival | **NO-OP (group CAS fails, no side effects)** |

Every operation used to complete a `WonPending` descriptor is idempotent: the
cancellation epoch uses its own admission CAS, the lifecycle/schedule claim
uses the exact task generation, and active-link clearing compares the full
WaitSet token. Queue delivery uses the matching full `QueueTicket` and
`Scheduler.publish_once`. A helper may complete the chosen operation but cannot
change its winner or reason. `commit_suspend` may create/publish the pending-
wake ticket only after it acquire-observes `WonCommitted` and the matching
logical uninstall. The ready-queue publication happens after logical uninstall,
so no worker can resume the task while its old active-set link is still
installed; the atomic queue ticket prevents both a lost enqueue and a duplicate
enqueue if the original winner is preempted.

#### 3.3.4 Completion subscription and terminal publication

Await/join completion registration is a separate two-party protocol; checking
`child.state` and then appending a callback is not sufficient. Each
`CompletionSubscription` carries the parent's full `WaitToken`, the child's
full `TaskToken`, and retained references to both sides. Installation,
terminal publication, and unsubscribe compete through the child's one
completion-registry/terminal arbiter:

1. **Parent/child arm handshake:** the fully initialized parent set and its TCB
   link are first published as `Waiting`, with the frame teardown obligation
   already armed. Subscription installation then acquires the parent set's
   install/uninstall arbiter and the child terminal/registry arbiter in the
   fixed parent-before-child order. It revalidates the same full parent token,
   matching TCB link, and `Waiting` state while both are held. If the parent is
   no longer waiting, it installs nothing and releases its temporary refs. If
   the child is already terminal, it installs nothing and routes the immediate
   terminal outcome after releasing the arbiters. Otherwise it links `Active`
   before either arbiter is released. Group winner selection uses the same
   parent arbiter or an equivalent install bit, so it cannot pass the recheck
   and uninstall between validation and node insertion. A terminal publisher
   never holds the child arbiter while acquiring the parent arbiter.
2. **Subscribe before terminal:** after validating and retaining the full child
   token, the subscriber installs `Active` while the child is nonterminal. The
   terminal publisher later enters that same arbiter, closes the registry,
   release-publishes `Completed` or `CompletedCanceled`, and only then release-
   publishes each `Active -> Selected(Publisher)` before unlocking. An acquire
   `Selected -> CommitClaimed` therefore also observes terminal/result
   publication; every publisher commit additionally acquire-checks the child's
   terminal state before routing `ChildTerminal`. The selector retains the
   node plus parent TCB/WaitSet until exactly one group CAS has been attempted
   (which may lose to another source) or it observes another committer finish.
3. **Terminal before subscribe:** if the same arbiter acquire-observes
   `Completed` or `CompletedCanceled`, no registration is installed. The
   subscriber itself owns the retained references, routes the same
   `ChildTerminal` event immediately through the parent group CAS, releases
   those references exactly once, and returns without an `Active` node.
   Completion-before-registration is therefore not a lost wake and does not
   need an inline coroutine resume.
4. **Commit/release once:** publisher and unsubscriber selection use
   `Active -> Selected(Publisher|Unsubscriber)`. Any permitted owner/helper may
   then win the sole `Selected(owner) -> CommitClaimed(owner)` CAS. That winner
   performs the owner's action, removes the node, releases its retained
   references exactly once, and publishes `Inactive`; every loser does
   nothing. A publisher action attempts `ChildTerminal` before release. If that
   group CAS wins, the parent winner descriptor retains its own independent
   parent/child references, commits this source subscription to `Inactive`, and
   only then begins whole-group uninstall. If the group CAS loses, the same
   commit simply removes/releases the node. An unsubscriber action sends no
   event.
   Selection takes a descriptor-storage reference before publishing
   `Selected`. A helper may obtain its first descriptor reference only while
   holding the child registry arbiter or through an already retained
   descriptor/hazard that protects the node; loading a bare node pointer and
   incrementing its reference count afterward is forbidden. Every helper then
   retains the node before its commit CAS. Removal is logical unlink only.
   Physical node storage is freed after `Inactive` and only when the publisher/
   unsubscriber selector plus all helper descriptor references have been
   released, so a publisher iteration cannot dereference a node already freed
   by a helper.
5. **No lock/self wait:** a thread observing `CommitClaimed` may acquire-wait
   only after releasing both child and parent arbiters. Waiting while holding
   the parent arbiter is forbidden. Whole-group teardown must first help or
   observe its selected source subscription `Inactive`, then skips that node
   while selecting/committing every remaining `Active` subscription as
   `Unsubscriber`. Thus a child publisher never waits on the group teardown
   that its own node is blocking, and no helper can double-release it. The
   unique owner action after `CommitClaimed` is inline, bounded, non-suspending,
   invokes no user cleanup, and cannot hand work back to the same executor and
   wait for it. An implementation needing an unbounded action must split it
   into an explicitly helpable state machine rather than block the only worker.
6. **Stale identity:** failure to validate either full token rejects before
   dereferencing a reused child TCB or parent WaitSet. A stale completion event
   cannot release a reference owned by a newer subscription. Descriptor-ref
   retention is checked and fails closed on overflow; it never wraps into
   premature storage reclamation.

`ChildTerminal` never admits a parent cancellation epoch. The resumed internal
await/join continuation first acquire-observes the child's terminal state and
then its result state. A child `CompletedCanceled` becomes the language-defined
canceled child outcome; it is not rewritten into `Requested(e)` on the parent.
Terminal result publication and subscription notification obey Section 5's
ordering, and all notification paths still use the parent's group CAS and
`Scheduler.publish_once` rather than inline resumption.

#### 3.3.5 Explicit structured-child registry arbitration

An explicit `TaskScope`/structured combinator that promises zero orphan tasks
uses a registry state `Open | Closing(reason) | Closed`. A lock-free snapshot
of a separate child list and cancellation flag is not this protocol.

The registry does not manufacture result authority. Each node records one
linear disposition established by the API that enrolled it:

- an owning heterogeneous `TaskScope` consumes the only typed `TaskHandle`,
  jointly transfers `result_owner: ConsumerOwned ->
  RuntimeOwned(ScopeNode(full_node_token))`, and gives
  the node authority to typed-drop an unconsumed normal result;
- a typed race/join combinator consumes each handle and retains an internal
  typed disposition token that authorizes winner transfer or losing/canceled-
  operand drop, but never both; or
- a retain/join-only observer keeps no result disposition, may not claim an
  externally `ConsumerOwned` payload, and must not make `Closed` depend on that
  external consumer discharging it.

An owning enrollment is one transaction: validate/retain the full child token,
transfer the consumed handle's lifecycle and result authority, link the node,
and only then activate or expose the child. Failure before commit leaves all
authority with the caller; failure after commit is resolved by the registry's
closing path and cannot run ordinary handle drop. A sequence that converts a
cold handle to an untyped `TaskRef`, lets the consumed handle destruct, and
later calls `track_ref`/`start` is not this transaction.

1. **Register or join closing:** child registration first retains the full
   child `TaskToken`, then enters the same parent cancellation/scope-close
   arbiter used to change `Open -> Closing`. If still `Open` and the parent has
   no `Requested|Handling` cancellation, it takes the child terminal arbiter in
   the fixed parent-before-child order and either links one `Tracked` node or
   acquire-observes terminal and creates a registry-counted `ChildSelected`
   immediate-drain descriptor before releasing either arbiter. For a newly
   created child, linking must precede activation or exposure. If
   `Closing(reason)` already won, the child is atomically selected into that
   still-live closing descriptor's cancel-join-drain set. Observing `Closed`
   rejects before lifecycle/result-authority transfer, releases the temporary
   retain, and leaves the complete typed handle/cleanup authority with the
   caller; a completed descriptor is never resurrected. If the parent is
   `Requested|Handling` while the registry is still `Open`, registration under
   the same arbiter either creates/joins
   `Closing(TaskCanceled(epoch), descriptor)` and atomically adds that exact
   descriptor authority to the existing task-wide aggregate before child
   authority transfer, or rejects before transfer. In particular,
   `Requested + Armed(ResolutionOwned)` preserves `Requested` and extends that
   aggregate; `Handling + Armed(CancelOwned)` extends its matching aggregate;
   an unobserved `Requested` may instead jointly establish
   `Handling + Armed(CancelOwned)`. It cannot create an independent scope
   obligation or duplicate authority already represented by an await/race
   component. Registration failure leaves ownership with the caller; no path
   exposes an untracked activated child.
2. **Lock order and work extraction:** parent/scope-before-child is the only
   nested acquisition order. A child terminal publisher may publish terminal
   state and take a protected scope-node descriptor reference while holding the
   child arbiter, but it releases that arbiter before acquiring the scope
   arbiter and revalidating/selecting the node. It never takes child-then-scope.
   Close/cancel holds the scope arbiter only to publish `Closing`, select nodes,
   retain immutable work descriptors, and update in-flight accounting. It
   releases the arbiter before requesting child cancellation, installing or
   waiting on completion/progress, claiming results, or invoking cleanup. No
   path waits for a child while holding the scope arbiter.
3. **Helpable node progress:** natural child completion and close selection
   compete `Tracked -> ChildSelected | CloseSelected`. The selected node then
   installs one immutable commit descriptor and advances through equivalent
   helpable phases:

   ```text
   CommitClaimed(CancelPending)
     -> WaitingTerminal
     -> WaitingNoActive
     -> ResultPending
     -> CallbackClaimed(owner)?
     -> ReleaseReady
     -> Inactive
   ```

   A helper may idempotently request cancellation, install/observe terminal and
   no-active subscriptions, or claim the next bounded phase. Waiting phases
   register a validated `ScopeProgress` source and return/suspend; they never
   block an executor worker. `CallbackClaimed(owner)` is the sole typed-drop
   owner, runs outside every runtime arbiter, is non-suspending, and release-
   publishes the next phase; re-entry may help other phases but cannot wait on
   or repeat that callback. The protocol does not claim tolerance of permanent
   OS-thread failure during arbitrary destructor code. `ReleaseReady ->
   Inactive` is the unique final registry-removal, in-flight decrement, and
   retained-reference release commit. Forced preemption in any other phase is
   recoverable by a helper. A retain/join-only node skips result phases and
   leaves external result ownership untouched.
4. **Descriptor progress arm handshake and finish:** the parent first fully
   initializes a `ScopeCleanup` WaitSet, publishes it as `Waiting`, and links
   its full token in the TCB. To arm a progress source it then acquires that
   retained descriptor's progress arbiter and revalidates the full parent
   `WaitToken`, matching active-set link and `Waiting` state, the non-reused
   descriptor identity, and the phase snapshot. If the descriptor already
   advanced past the snapshot or is `Closed`, it installs nothing and routes
   one immediate `ScopeProgress` attempt after releasing the arbiter. Otherwise
   it links an `Active` progress subscription before releasing the arbiter.
   Phase publication advances the phase and selects every matching `Active`
   subscription under that same progress arbiter; unsubscribe competes through
   the completion protocol's equivalent
   `Active -> Selected -> CommitClaimed -> Inactive` ownership states. The
   selected owner retains the descriptor and parent references until its one
   group attempt and reference release finish. It never holds the descriptor
   arbiter while taking the parent WaitSet arbiter. Thus progress-before-arm,
   arm-before-progress, and progress-during-arm cannot lose a wake.

   Each phase publication capable of unblocking cleanup emits one such
   `ScopeProgress`; terminal is not the only notification. In particular,
   result-drop callback completion, no-active completion, node `Inactive`, and
   descriptor `Closed` wake a waiter that saw an earlier `Claimed` or
   incomplete state. Freshness is the existing full `WaitToken` plus retained,
   non-reused descriptor identity and phase snapshot; this protocol adds no
   sixth generation counter. Only an empty registry with every selected node
   `Inactive`, every node-owned result disposition discharged, and no in-flight
   `ChildSelected|CloseSelected` descriptor may publish `Closing -> Closed`.
   An observer-only node neither steals nor waits for an external
   `ConsumerOwned` result. Already-terminal immediate-drain nodes remain
   in-flight through the same phases. Stale full tokens fail before touching a
   reused child TCB.
5. **Task-wide scope cleanup components:** every active structured-close
   descriptor is registered in the parent task's cancellation/scope arbiter.
   When cancellation is observed, or when a source-canceled `Propagate`
   disposition is claimed, one joint transaction closes further independent
   scope registration, creates or reuses every affected closing descriptor,
   and adds one canonical `ScopeDescriptor` component per exact descriptor
   authority to the task-wide aggregate. A pre-resolution parent request
   establishes `ParentEpoch(e) + CancelOwned` and performs
   `Requested(e) -> Handling(e)`; a post-`NormalClaimed` request instead extends
   `ParentEpoch(e) + ResolutionOwned` while preserving `Requested(e)`. Source
   propagation establishes `SourceOutcome(source, Propagate) + SourceOwned`
   while preserving `Open|Consumed`. A
   descriptor discovered or extended later joins that same aggregate before
   child authority transfer or registration rejects. Thus multiple scopes and
   racing child enrollment cannot create `Handling` with no named work or own
   one result/ref disposition twice.

   Any old ordinary WaitSet is inactive before a `ScopeCleanup(oid)` set is
   installed. That set waits only on the validated `ScopeProgress` handshake,
   helps/drains nodes, and may re-arm until every represented descriptor is
   `Closed`. The first task request in source-owned mode atomically re-keys the
   same aggregate without winning this set; requests against a parent-owned
   mode coalesce and cannot win it. Closing a
   descriptor removes only its matching aggregate component and delegates its
   final descriptor/TCB reference decrements outside all arbiters. If another
   await, loser, value, or scope component remains, cleanup selects the next
   mode without consuming or closing the current parent/source state. Only the task-wide
   aggregate-empty commit applies the final semantics defined in Section 3.2.
   Delegated releases finish before user CFG or terminal frame cleanup. An
   explicit normal `scope.close().await` uses ordinary waits while
   `Open|Consumed`; if task cancellation wins, it switches to this shielded
   protocol.

This is the runtime substrate for explicit scope close and combinators only.
Automatic lexical cancel-then-join on every source scope exit remains the
separate language contract required before Scoped Borrowed Tasks.

---

## 4. Complete vs Cancel Linearization Semantics

Cancellation is a cooperative request, NOT an immediate terminal state. Winning
cancellation arbitration admits cancellation processing; terminal publication
occurs only after the required cleanup has completed.

1. **Ready/Child-Terminal-First Wake Path**:
   - A ready event or child terminal notification linearizes first through its
     parent WaitSet group CAS.
   - CAS transitions the shared `WaitSetState` to
     `WonPending(source, Ready or ChildTerminal)`; the bounded winner
     transaction commits the schedule action, uninstalls the set, and only
     then publishes the queue ticket. The parent cancellation epoch is
     unchanged by that winner, even when the child is `CompletedCanceled`.
   - After acquiring the child terminal/result state, `.await?` may map a
     canceled child to its explicit no-value continuation; ordinary `.await`
     may propagate cancellation through the language CFG. Neither path
     fabricates a parent `Requested(e)` epoch.
   - Only when the selected source/result is normal and no later task-level
     cancellation has been admitted may the body eventually win
     `Running -> FinalizingNormal`, commit its typed result, clean remaining
     frame locals, and publish `Completed`. A later admitted task cancellation
     or a propagated canceled child follows its own caught/unhandled path.
   - A trailing wait-local cancellation event for that set fails the group CAS
     and returns without changing `cancel_state`. A distinct task-level
     `request_cancel` helps/observes committed uninstall and retries the TCB
     arbitration, so it is not silently lost.
   - If the parent body ultimately completes normally, one normal typed payload
     is published **EXACTLY ONCE** as `ReadyLive`.

2. **Task-Cancel-First Execution Path**:
   - A task-level cancellation request linearizes first through the active
     group.
   - CAS transitions the shared `WaitSetState` to
     `WonPending(cancellation_source, TaskCanceled)`.
   - **ONLY THROUGH THAT WINNER DESCRIPTOR**: transition `cancel_state` to the
     next `Requested(e)` epoch, claim the matching lifecycle/schedule action,
     publish `WonCommitted`, logically uninstall the group, and then expose
     any ready-queue entry.
   - Worker dequeues task, observes `Requested(e)`, and reaches an
     explicit cancellation-observation boundary. Unhandled cancellation runs
     `Running -> FinalizingCanceled`, scope cleanup/drop handlers, and
     `Pending -> Canceled` before publishing `CompletedCanceled`.
     Explicit source-level capture such as `.await?` may instead continue to a
     later normal domain result. If the request targets the current task, that
     continuation consumes exactly that epoch with direct
     `Requested(e) -> Consumed(e)` when no async cleanup is armed, or with
     `Requested(e) -> Handling(e) -> Consumed(e)` after the matching cleanup
     barrier. Cancellation of only the awaited child does not mutate the
     current task's cancellation epoch. Capture never fabricates a `T` payload
     and is not a persistent `cancel_handled` boolean.
   - Trailing ready/timeout/completion events for every slot in that set fail
     the group CAS.
   - The eventual terminal result is published **EXACTLY ONCE**.

3. **Cold-Cancel Execution Path**:
   - Cancellation of a `Created` task first wins the TCB cancellation/terminal
     arbitration. It does not execute the task body.
   - `Created -> FinalizingCanceled`; frame-owned parameters and other armed
     cleanup obligations are destroyed exactly once.
   - Only after that cleanup is complete may the task publish
     `CompletedCanceled`, notify joiners, or become reclaimable. Observers must
     never see a terminal cold-canceled task whose frame cleanup is still
     pending.

A direct wait source that reports `SourceCanceled` is the same language-level
no-payload source outcome as a canceled child, not an implicit parent request.
After the old WaitSet is inactive, its lowered await obligation enters the
cancellation/cleanup arbiter. With parent `Open|Consumed` it claims
`Armed -> SourceCanceledClaimed(source)` and follows the declared
`Capture|Propagate` disposition, using `SourceOwned` if source teardown or
propagating scope cleanup can suspend. With parent `Requested(e)` the joint
claim is instead `Armed -> CancelClaimed(e)` plus
`Requested(e) -> Handling(e)` and the exact source teardown joins the
`CancelOwned` aggregate. A later task request atomically re-keys any still-live
`SourceOwned` aggregate as defined in Section 3.2. Thus a direct canceled I/O or
runtime source neither fabricates a parent epoch nor bypasses the joint
disposition protocol.

### 4.1 Await/source cleanup barrier

An active await owns more than a wake registration. Its frame carries an
internal, non-user-visible `AwaitCleanupObligation` for the retained child
`TaskToken`, completion subscription, and result obligation. Waking the parent
does not by itself discharge that obligation or authorize user continuation.

- If `ChildTerminal` wins, the internal continuation acquire-observes the
  completion subscription already `Inactive`—the publisher/unsubscriber
  protocol owns that node and its references—and then acquires the child's
  terminal/result publication. The frame's separate await-owned child/result
  reference remains live. Before exposing a result or canceled-child outcome,
  the continuation enters the same cancellation/cleanup arbiter and resolves
  `AwaitResolution(oid)` exactly once:
  - with parent `Open|Consumed` and child `Completed`,
    `Armed -> NormalClaimed` linearizes ownership of the normal typed result;
  - with parent `Open|Consumed` and child `CompletedCanceled`,
    `Armed -> SourceCanceledClaimed(child)` records a no-payload source outcome
    without creating a parent epoch; any remaining operand cleanup is carried
    by `Armed(SourceOutcome(child, Capture|Propagate), SourceOwned)`;
  - with parent `Requested(e)`, one joint commit performs
    `Armed -> CancelClaimed(e)` and `Requested(e) -> Handling(e)`, so no
    continuing `T` path may claim the result.
  All fallible preparation precedes this commit. For a single-child canceled
  await with no remaining asynchronous component—including no structured scope
  that a `Propagate` disposition must close—one bounded release sequence
  validates the no-payload terminal state, delegates the await-owned child
  reference release outside all arbiters, and publishes
  `SourceCanceledClaimed(child) -> Discharged`; no empty aggregate is installed.
  Only after the delegated release may `.await?` expose its no-value capture
  while leaving parent `Open|Consumed` unchanged, or ordinary `.await` jointly
  close `Open|Consumed` with `Running -> FinalizingCanceled`. For a single-child
  normal await,
  after `NormalClaimed` the physical typed transfer and await-reference release
  are one bounded, non-suspending ownership commit performed without runtime
  arbiters held; they cannot call user code or expose CFG midway. Completion
  release-publishes `NormalClaimed -> Discharged` before exposing user CFG.
  Multi-child combinators retain `NormalClaimed` and an armed internal winner
  temporary through their mandatory loser cleanup, as specified below.
  `NormalClaimed` irrevocably assigns the child result disposition to the
  awaiting frame; it does not promise that later task cancellation must expose
  that value to source code. A parent request admitted after the claim creates
  or extends `Armed(ResolutionOwned)` with the mandatory await/loser work,
  active scope descriptors, and a `SuppressedFrameValue` component when its
  outcome will hide the value. It remains `Requested(e)` until the aggregate
  final commit. The cancel path cannot claim the child result again; the
  aggregate's typed component destroys the already transferred value before
  unhandled propagation or no-value capture. After `CancelClaimed(e)`, the
  child is already joined but any live result is privately claimed and typed-
  drained before that aggregate component is discharged.
- If the parent's `TaskCanceled` contender wins while the child is nonterminal,
  the old parent WaitSet is first logically uninstalled. At the observation
  boundary one transaction builds `Armed(CancelOwned)` from this await and all
  active structured components, claims `AwaitResolution(oid): Armed ->
  CancelClaimed(e)`, and performs `Requested(e) -> Handling(e)`. An internal continuation then requests cancellation of the child
  and joins it to terminal/no-active-registration. It may use
  `prepare_cleanup_suspend(Handling(e), obligation_id)` to install a new
  child-terminal-only WaitSet and suspend internally; ordinary
  `prepare_suspend` remains forbidden. No source-level continuation, callback,
  unwind, or terminal parent finalization is visible during this cleanup
  substate, and further task-level cancellation coalesces with `Handling(e)`.
- After that join, a child that nevertheless completed normally is privately
  claimed and destroyed with its compiler-installed typed drop plan because a
  `CancelClaimed` resolution has no continuing `T` path. A canceled child has
  no payload. The retained child reference and await component remain owned
  and armed until its aggregate commit.
- Under the cancellation/cleanup arbiter, the component commit verifies child
  terminal/no-active/result discharge, publishes
  `CancelClaimed(e) -> Discharged`, and removes only this await component. It
  delegates the last child-reference decrement outside all runtime arbiters.
  If aggregate components remain, the same commit selects the next
  `AwaitCleanup`/`SourceCleanup`/`ScopeCleanup` mode and leaves
  `Requested|Handling` unchanged. Only the aggregate-empty final commit may
  perform `Consumed` or `Closed`; only after its delegated releases may a no-
  value continuation or terminal frame cleanup become visible.
  `Running -> FinalizingCanceled` is illegal while an await cleanup obligation
  remains armed; the terminal frame callback is non-suspending and is not a
  substitute for cancel-join.

For `race2`/`select2`, the committed child winner must pass the same joint
resolution claim before constructing an internal winner value.

- If the selected child is `Completed`, `NormalClaimed` authorizes one typed
  winner disposition plus a loser cancel-join-drain obligation. If a later
  parent request becomes `Requested(e)` while that loser cleanup still needs to
  suspend, the combinator may use only `ResolutionCleanup(e, oid)`; the set
  admits child-terminal sources, preserves the pending request, and publishes
  `NormalClaimed -> Discharged` only after the loser is terminal/disarmed/
  drained. The immediate post-discharge boundary then handles or propagates
  the request. It may destroy the frame-owned winner and suppress a source-
  visible `RaceWinner`, but it cannot reselect an operand or double-claim either
  result.
- If the first selected child is `CompletedCanceled` while the parent is
  `Open|Consumed`, the claim is instead
  `Armed -> SourceCanceledClaimed(child)`. No `T` exists and no `RaceWinner` may
  be constructed. One `SourceOutcome(child, Capture|Propagate) + SourceOwned` aggregate
  canonicalizes the canceled-source reference and the other operand's
  mandatory cancel-join-drain authority; `Propagate` also closes registration
  and includes every active structured-scope descriptor, while `Capture` does
  not disturb unrelated scopes. It requests cancellation of the other operand
  and uses only `SourceCleanup(oid)` if it must suspend. A concurrently
  or already normally completed other operand is typed-dropped; an already
  canceled operand has no payload. Thus the both-canceled case follows the
  same single aggregate. When it empties, an explicit no-value combinator
  boundary captures the source outcome without changing the parent epoch, or
  ordinary `race2`/`select2` propagation jointly closes the parent as canceled.
  If task-level parent cancellation wins during this cleanup, the atomic
  `SourceOwned -> CancelOwned` re-keying from Section 3.2 preserves every owned
  component and the still-selected cleanup WaitSet; repeated requests only
  help/coalesce.
- If parent `TaskCanceled` wins before either source-resolution claim, there is
  no internal winner disposition: `Armed -> CancelClaimed(e)` and every retained
  operand becomes one canonical cancel-join-drain component. All must be
  terminal, disarmed, and result-discharged before the parent cancellation
  outcome or finalization proceeds.

---

## 5. WaitSet/registration recycling and frame memory protocol

> [!IMPORTANT]
> **Active Slot Overwriting Guard**:
> `suspend_and_register_wait` MUST verify that any existing registration slot at `slot_idx` is inactive (`active == false`) BEFORE overwriting.
> Overwriting an active slot, or installing a second set while
> `TCB.active_wait_set` is non-empty, is strictly prohibited.

1. **Recycling Sequence**:
   - Step 0: Initialize the WaitSet, every planned full `WaitToken`, `TaskToken`,
     schedule generation, source field, retained TCB/group reference, and frame
     teardown obligation before release-publishing the group as `Waiting` and
     linking `TCB.active_wait_set`. That group/link publication is one install
     linearization transaction. External timer/reactor/completion nodes then
     attach through the set's install/uninstall arbiter; completion nodes also
     use Section 3.3.4's parent/child handshake. A source may win after group
     publication but before every external node attaches; each remaining attach
     revalidates `Waiting` and otherwise installs nothing. `commit_suspend`
     proceeds only after every planned node is active or an already selected
     winner has been committed/uninstalled. A cancellation request is therefore
     classified wholly before group publication as the no-active `Preparing`
     case, or wholly after it as an active-set group CAS; it never dereferences
     uninitialized group fields. Event lookup acquire-validates set generation,
     slot generation, and active state before reading any initialized field. A
     shared-lock or equivalent state-machine happens-before edge is valid;
     unsynchronized multiword publication is not.
   - Step 1: Exactly one source moves the shared group
     `Waiting -> WonPending(source, reason)`; every other registration or
     wait-local source loses without side effects.
   - Step 2: The winner or a bounded helper uses the immutable descriptor to
     retain the TCB/group and any required child/result references, performs
     the selected idempotent cancellation/lifecycle/schedule actions, and
     release-publishes `WonPending -> WonCommitted`.
   - Step 3: Before any ready-queue publication, the same short transaction
     invalidates every registration, clears the matching
     `TCB.active_wait_set`, disarms the frame's wait-set teardown obligation,
     and moves the group to `Inactive`. A resume prologue acquire-captures the
     stable retained winner record, but is not responsible for logical
     uninstall. This transaction must finish before user/combinator code can
     return, unwind, call a callback, or install another suspension.
     `abort_suspend` performs `Waiting -> Inactive` when no source won. If it
     encounters `WonPending` or `WonCommitted`, it completes/observes that
     descriptor and uninstalls the set while preserving any already admitted
     `Requested(e)` and selected outcome before restoring `Running`.
   - Step 4: If the selected lifecycle action claimed `Queued(gen)`, the winner
     or any helper calls `Scheduler.publish_once` for that exact ticket. The
     insert and `Unpublished -> Published` transition share one scheduler
     linearization point. For `PreparingWithPendingWake`, `commit_suspend`
     creates and publishes the ticket only after Step 3; abort instead resumes
     locally and creates no queue entry.
   - Step 5: Only after logical uninstall does `WaitRegistry` advance the
     wait-set and affected slot generations. Physical reactor unregistration
     may execute asynchronously because trailing kernel events carry only
     invalidated old tokens and cannot resolve the TCB/group.

Installing a set arms one frame/TCB teardown obligation. The winner transaction
normally consumes it before queue visibility. Suspend rollback, unhandled
cancellation, and terminal frame cleanup must consume it idempotently if a
partial winner/abort path has not. A task may never publish a terminal state,
become worker-visible, or replace `active_wait_set` while the old group remains
logically active. This gives `race2` one strict order: retain/capture the winner
descriptor, uninstall the entire old set, then cancel/join/drain the loser
through any later wait.
2. **Frame Life-Cycle Guard**:
   - Coroutine frames can ONLY be marked freed when ALL 5 conditions hold:
     1. `tcb.state == Completed` or `CompletedCanceled`,
     2. the task-wide cleanup aggregate and every canonical component are
        discharged,
     3. `no_active_registration` for task, AND
     4. the result obligation is discharged, AND
     5. after acquire-revalidating conditions 1--4, the reclaimer wins
        `frame_access_state: Open(0) -> Retired`, revalidates 1--4, and thereby
        proves every worker, resume/final-suspend path, terminal publisher,
        typed claimant, and cleanup callback has released its frame pin.
   - Normal finalization first constructs the return value in a compiler-owned
     live return place or temporary. One non-suspending ownership commit copies
     or moves that typed payload into result storage, transitions
     `Pending -> ReadyLive`, and disarms the return place's local cleanup. Any
     fallible preparation occurs before this commit; failure leaves the return
     place live and the result `Pending`.
   - After the result commit, finalization destroys every other armed
     frame-local obligation and only then publishes `TCBState::Completed`.
     `ReadyLive` is not claimable before that terminal publication.
   - These five predicates permit frame release only after no in-flight typed
     transfer/drop, terminal publication path, or cleanup callback retains the
     frame. `Open(0)` alone is not a guard because a legitimate claimant may
     acquire a new pin; only the reclaimer's `Retired` claim is irreversible.
     They do not permit
     TCB or task-registry-slot reuse. Every owner, `TaskHandle`, `TaskRef`,
     ready-queue entry, WaitSet/subscription, registry node, and helper/hazard
     that may dereference the TCB owns one checked lifetime reference. The TCB
     and its numeric slot are freed/reused only after terminal publication,
     `NoTicket`, and `lifetime_ref_count == 0`; slot reuse advances the full
     task-instance generation first. Creation installs one owner/registry
     reference before publishing the token. Each ownership handoff either
     transfers that reference or takes a checked additional retain; the last
     release removes/invalidates the registry entry under its arbiter before
     zero becomes observable, and zero can never be resurrected. Reference
     acquisition begins under the task registry/owning arbiter or an existing
     retained hazard, fails closed on token mismatch or counter overflow, and
     never loads a bare pointer then increments it. If frame and TCB are
     coallocated, this zero-reference
     predicate is also required before freeing the frame. If they are separate,
     the frame reclaimer release-publishes a null frame/result-storage pointer
     after the `Retired` claim and before freeing it; later `TaskRef` operations are terminal TCB-only
     queries/releases and cannot dereference the retired frame. Any operation
     that still needs frame storage must already own a frame-retaining
     obligation and therefore prevents the frame-retirement predicates from
     holding.
   - While `result_state == ReadyLive`, exactly one claimant CASes the private
     `result_claim_state` from `Unclaimed -> Claimed` with acquire semantics.
     This claim word is not a fifth Promise ABI result state. An await/wait
     claimant transfers the typed value; a detached runtime, a `race2` losing/
     canceled-operand disposition, or an owning heterogeneous `TaskScope`
     runtime disposition calls the TCB's compiler-installed `result_drop_fn`
     over the result storage. A `race2` winner instead consumes its distinct
     typed-transfer disposition. The callback is derived from the typed
     DropPlan, is non-suspending, cannot be supplied by ordinary TKI metadata,
     and is invoked with no runtime arbiter held as required by Section 3.2.
   - The claimant retains the TCB/frame until transfer or typed drop finishes,
     then publishes `ReadyLive -> Taken` with release semantics. Only `Taken`
     means the normal result obligation is discharged; a second claimant is
     rejected and frame reclamation cannot observe discharge during the drop.
   - Canceled publication transitions `Pending -> Canceled` and exposes no
     payload. It occurs only after cancellation cleanup and cannot race a
     successful normal result commit.
   - `try_wake` MUST validate both set and slot generations BEFORE accessing TCB
     or coroutine frame pointers.
   - Trailing/stale token events immediately return `false` with ZERO TCB/frame dereferences.

3. **Publication and memory order:**
   - a successful group `Waiting -> WonPending(source, reason)` CAS is acquire-
     release. The selected cancellation-epoch admission and lifecycle/schedule
     action happen-before release publication of `WonCommitted`; whole-group
     uninstall happens-before ready-queue enqueue. Helpers and
     `commit_suspend` acquire the committed descriptor before proceeding;
   - every `CancelRequestState` transition and joint finalization claim is
     acquire-release. A running task's cancellation-observation boundary uses
     an acquire load before deciding to consume, continue, or finalize;
   - `publish_once` atomically release-publishes the full queue entry and its
     `Published` state; duplicate helpers observe that state without another
     insertion. Worker dequeue is acquire and atomically consumes the matching
     ticket; before
     resumption the worker also acquire-loads the matching TCB epoch and
     `cancel_state`, so it observes the winning reason and any `Requested(e)`;
   - whole-group uninstall, active-link clearing, registration invalidation,
     and generation increments are release-published. Stale-event validation
     and the frame reclaimer's
     `no_active_registration` check are acquire operations; physical reactor
     unregistration may trail only because its token can no longer validate or
     dereference the TCB;
   - payload construction and the result-storage ownership commit happen-before
     the release publication of `ReadyLive`;
   - remaining frame-local cleanup happens-before the release publication of
     `Completed` or `CompletedCanceled` and before join notification enqueue;
   - terminal publication does not release the publisher's frame-access pin.
     Completion-registry close/selection and every final-suspend/cold-finalizer
     frame access happen-before its final `Open(n) -> Open(n-1)` release. The
     frame reclaimer acquire-observes `Open(0)`, revalidates the other guards,
     and must win `Open(0) -> Retired` before nulling/freeing storage;
   - closing the completion registry and terminal publication share the
     terminal/registry arbiter. Subscriber installation and the immediate-
     terminal path acquire that publication; publisher/unsubscriber claim
     transitions release exactly one retained-reference owner before
     `Inactive`;
   - a claimant or joiner must first acquire a terminal TCB state, then acquire
     `ReadyLive` before attempting private `Unclaimed -> Claimed`; and
   - `ReadyLive -> Taken` is release-published by the unique private claimant,
     while the final frame-reclamation check acquires it together with terminal
     state and registration liveness before the reclaimer's `Retired` claim.
   - `ConsumerOwned -> RuntimeOwned(disposition)` transfer is release-published.
     Only the `Detached` disposition and normal terminal publication invoke the
     same acquire `try_drain_detached_result`; `ScopeNode(token)` routes
     terminal/result readiness to the retained node instead. Each path observes
     owner, terminal, public result, and private claim state in that order.

These edges are mandatory even when lifecycle and result state use separate
atomic words. Notification alone is not the publication barrier.

---

## 6. TaskHandle Ownership & Detach Policy

1. **Cold-handle release**: Dropping the last handle to a `Created` task wins
   the same exclusive no-body cleanup claim as cold cancellation. It closes
   start/cancel admission, runs `frame_cleanup_fn` exactly once for frame-owned
   parameters and other armed obligations, unconditionally publishes
   `Pending -> Canceled` and `FinalizingCanceled -> CompletedCanceled`, and
   only then releases the frame under Section 5's guard. Whether an observer
   remains controls notification, not terminal/result publication. The body is
   never started.
2. **Explicit cold detach**: Detaching a `Created` handle jointly transfers the
   result obligation to `RuntimeOwned(Detached)` and claims the ordinary cold start/
   queue-ticket path. It queues rather than inlining the body. If cold release
   or cancellation won first, detach fails/retries against that terminal claim;
   it cannot both reclaim and activate the frame.
3. **Activated-handle backward compatibility**: Dropping an already activated
   handle retains detach semantics; the task continues under runtime ownership
   until completion and result discharge. Detach atomically transfers
   `result_owner: ConsumerOwned -> RuntimeOwned(Detached)`; a plain boolean observation is
   not the ownership linearization.
4. **Result Recycling**: If a detached task completes with a normal live
   result, the runtime must first acquire-observe `Completed`, then win private
   `Unclaimed -> Claimed` while the public state is `ReadyLive`, invoke
   `result_drop_fn`, and publish `ReadyLive -> Taken`. To close detach/complete
   races, both a successful transfer to `RuntimeOwned(Detached)` and terminal publication
   call the same idempotent `try_drain_detached_result`. If detach sees
   nonterminal state it may return because terminal publication will retry; if
   terminal publication saw consumer ownership, a later detach will retry.
   `CompletedCanceled` has no payload to drain. It may reclaim the frame only
   after the remaining frame guards hold; the TCB/slot additionally obeys the
   lifetime-reference rule in Section 5.
5. **Structured Concurrency Substrate**: Phase 5 may provide retained child
   references and explicit cancel/join operations. An owning scope must consume
   a cold handle through Section 3.3.5's atomic enrollment; ordinary conversion
   to `TaskRef` followed by later registration/start is not conforming.
   Automatic cancel-then-join on lexical scope exit is a separate language/
   runtime contract; it is not implied by an ordinary `TaskScope` value drop
   and must not silently block an executor worker.

### 6.1 Source-less and trust boundary

Async result types, calling convention declarations, and public task
signatures are declaration facts that a TKI importer recomputes. Frame capture
layout, return-place transfer, local cleanup discharge, and cancellation unwind
are body-derived obligations. Initial Level-A integration rechecks a retained
body, lowers that exact body, and links only the consumer-generated object;
later Level-B bodyless integration may rely on a separately
accepted-provenance, exact-object-bound compiler attestation. A standalone TKI
cannot assert either proof source into safe use. Scheduler, wait-token, TCB, and
result-state rules are compiler/runtime intrinsics selected only through
resolver-owned runtime ABI provenance; neither TKI nor manifest text may
override them.

---

## 7. Phased Implementation Roadmap & Scope Freezes

### Phase 1: Minimal CodeGen Promise Hook + TCB & Unified Ready Queue
- Introduce `TaskControlBlock` (TCB) with atomic `Suspended(gen) → Queued(gen)` state machine and a `TaskToken`-keyed Scheduler Ready Queue.
- **CodeGen Realignment**: Introduce minimal runtime hooks (`task_complete`, `task_yield`) so coroutine completion queues awaiter to Ready Queue instead of calling inline `coro_resume`.

### Phase 2: Generation-based WaitRegistry & Timer Heap
- Implement `WaitRegistry` with independent `wait_set_generation` and
  `wait_slot_generation` lazy invalidation.
- TimerHeap stores `WaitToken` instead of raw frame pointers.

### Phase 3: Multi-Platform Reactor Tokenization
- Tokenize epoll (Linux) and kqueue (macOS) userdata to `WaitToken`.
- **Windows Baseline Freeze**: Phase 3 tokenizes existing Windows `select` Reactor. Migration to IOCP is explicitly decoupled as a future milestone.

### Phase 4: Context & IO Timeout Integration
- Integrate `CancellationToken` with `WaitRegistry`.
- Update `TcpStream` async IO operations to use Reactor tokens and timeouts.

### Phase 5: `race2` / `select2` & Structured `TaskScope`
- Implement `race2` and `select2` with explicit shape results.
- Loser cleanup guarantee before `race2` returns.

### Phase 6: CodeGen ABI Convergence & Cross-Module Stabilization
- Finalize C-ABI runtime hooks. Remove legacy direct promise layout access in CodeGen.
- Verify `.tki` declaration replay and retained-body recheck for the initial
  language integration. Object-attested bodyless replay is a later manifest-
  payload level; serialization stability alone is insufficient.

---

## 8. Layered implementation conformance gates

This RFC has separate closure levels so the runtime TCB does not depend on the
later language PlaceState integration or semantic-manifest payload.

### 8.1 Runtime-core closure (`AS` in the semantic roadmap)

The runtime core is not declared conformant until all of these gates pass at
the exact revision being qualified:

1. **CAS-first cancellation:** controlled complete/ready/timeout/cancel
   permutations prove that only a winning group descriptor applies that
   group's cancellation side effects or schedule action. A losing or stale
   wait-local source is a no-op; a task-level request encountering an already
   chosen group bounded-helps committed uninstall and retries the TCB arbiter
   until the selected queue ticket is published and the request is admitted,
   duplicated, overflow-rejected, or terminal-rejected. Child-terminal and
   source-canceled wait outcomes never mutate the parent's cancellation epoch.
2. **Result claim/drop and publication:** normal publication is
   `Pending -> ReadyLive`; exactly one claimant wins private
   `Unclaimed -> Claimed`, transfers the typed payload or invokes the installed
   erased drop entry, and publishes public `ReadyLive -> Taken` only after
   first acquire-observing normal terminal completion. A second claim is
   rejected, canceled completion exposes no payload, and every
   terminal/result/join edge satisfies Section 5's release/acquire order.
   Re-entrant destructor probes also prove typed drop/frame cleanup is invoked
   with no scheduler, wait, completion, scope, terminal, or cancellation
   arbiter held.
3. **Cold no-body cleanup-before-terminal:** canceling a `Created` task or
   dropping its last handle does not execute its body, invokes the installed
   frame-cleanup entry exactly once, and makes terminal completion, join
   visibility, or frame reclamation possible only after the callback returns.
   This runtime test does not by itself prove that CodeGen generated the
   correct PlaceState-sensitive callback.
4. **Suspend rollback:** every failure after `prepare_suspend` but before a
   committed suspension calls `abort_suspend`, completes or invalidates any
   `WonPending` descriptor, disarms every partial timer, reactor, completion,
   progress, and parent-cancellation registration, and restores a runnable
   state without resetting or discharging task-wide cleanup. In particular,
   rollback preserves `Handling + CancelOwned + CancelClaimed` for
   `AwaitCleanup`, `Requested + ResolutionOwned + NormalClaimed` plus every
   winner/loser/value witness for `ResolutionCleanup`, the mode-appropriate
   aggregate and retained descriptors for `ScopeCleanup`, and
   `Open|Consumed + SourceOwned + SourceCanceledClaimed` (or its atomically
   converted `Handling + CancelOwned` form) for `SourceCleanup`. It invalidates
   only nodes installed by the failed WaitSet attempt and leaks neither a wait-
   set/slot nor a retained TCB/descriptor reference.
5. **Identity and generation exhaustion:** focused stale-token and near-overflow
   tests prove that task-instance, wait-set, wait-slot, cancellation-epoch, and
   task-schedule generations never wrap or let an old ready/wait/join token
   dereference a new TCB or wait group, and that failure installs no partial
   registration.
6. **N-way winner and teardown:** cross-slot permutations prove that all slots
   share one `Waiting -> WonPending` arbitration, losing slots have no TCB
   effects, the selected descriptor reaches `WonCommitted`, and the entire set
   is logically inactive before worker visibility, nested suspension, terminal
   publication, or registry-slot reuse. Winner, rollback, and terminal cleanup
   each discharge the same teardown obligation exactly once.
7. **Queue-publication preemption:** forced preemption after each admitted
   transition into `Queued` (`Created`, `Suspended`, and pending-wake commit),
   after `WonCommitted`, and after logical uninstall proves that another helper
   can publish the same full queue ticket, that the task never remains
   permanently `Queued` without an entry, and that no epoch is inserted or
   dequeued twice—including by a late helper after dequeue.
8. **Completion subscription:** completion-before-subscribe,
   subscribe-before-completion, parent-arm versus child terminal/other group
   winner, normal/canceled terminal, publisher-versus-unsubscriber, and stale
   child/parent token permutations prove exactly one `ChildTerminal` group
   attempt, registry removal, and retained-reference release, with no orphan
   node, lost wake, or inline resume. Helper acquisition begins under the child
   arbiter or an already retained hazard; forced single-worker execution proves
   each `CommitClaimed` owner action completes inline without queuing work and
   waiting on itself.
9. **Await cleanup barrier:** parent task cancellation at every child lifecycle
   point, normal-child, canceled-child, and direct `SourceCanceled` outcomes,
   and forced parent cancellation immediately before/after the joint
   `AwaitResolution` claim prove exactly one `NormalClaimed`,
   `SourceCanceledClaimed`, or `CancelClaimed`. A pre-claim request jointly
   performs `Requested -> Handling`; a post-`NormalClaimed` request remains
   pending for the immediate post-discharge boundary and cannot double-claim or
   reselect the result disposition. Single-child canceled capture/propagation,
   first-child-canceled `race2`, both-canceled `race2`, and a parent request
   during `SourceCleanup` prove that no `T`/`RaceWinner` is fabricated, source
   capture preserves `Open|Consumed`, source propagation closes all active
   structured scopes before finalization, and the atomic
   `SourceOutcome + SourceOwned -> ParentEpoch + CancelOwned` conversion neither
   loses nor duplicates a component. A forced single-worker `race2`
   interleaving with a nonterminal loser proves
   `ResolutionCleanup(e, oid)` can suspend to child terminal while preserving
   `Requested(e)`, then discharges cleanup before that request is handled or
   propagated; an unhandled request may drop the frame-owned winner rather than
   return it.
   The old WaitSet is inactive before a matching shielded cancel-join
   suspension, and repeated cancellation coalesces without winning the cleanup
   group; no user CFG or parent finalization is reached before every retained
   awaited child is terminal, disarmed, and result-discharged.
   Forced observation around the final commit proves there is no state with
   `Handling(e)` but no matching armed obligation; child-reference release
   occurs afterward, outside arbiters, before continuation/cleanup exposure.
   Parent-canceled `race2` drains all operands and constructs no winner.
   An operand represented simultaneously by an await/race and structured node
   proves authority-id canonicalization: its one result disposition and each
   exact retained reference are released once, never by two aggregate
   components.
10. **Detach/complete handoff:** detach-before-result, result-before-detach,
    detach between `ReadyLive` and `Completed`, concurrent drain helpers, and
    canceled completion prove that one side observes
    `RuntimeOwned(Detached) + Completed + ReadyLive`, wins at most one private claim, and
    publishes `Taken` after typed drop. Neither ordering may strand a live
    detached result or reclaim its frame early.
11. **Structured-child registration and progress:** register-before-cancel/close,
    cancel/close-before-register, already-terminal registration, natural-child-
    completion versus close selection, repeated close helpers, and stale-token
    permutations prove no activated child escapes the registry, every selected
    child is cancel-joined/disarmed once, every node-owned result disposition
    is drained once, and exactly one side releases each retained reference
    before `Closed`. Forced preemption after every helpable node phase proves a
    single worker or re-entrant callback can advance
    `CommitClaimed -> WaitingTerminal -> WaitingNoActive -> ResultPending ->
    CallbackClaimed? -> ReleaseReady -> Inactive` without waiting on queued
    self-work. Progress-before-arm, arm-before-progress, phase-change-during-arm,
    stale parent/descriptor tokens, result-callback completion, node `Inactive`,
    and descriptor `Closed` prove the `ScopeProgress` arm/publish handshake has
    no lost wake and adds no independent generation domain. Cold owning enrollment proves result/
    lifecycle authority transfer and registry linking precede activation;
    `Open + Requested` either creates/joins a concrete closing descriptor or
    rejects before transfer, while register-after-`Closed` releases only its
    temporary retain and leaves caller authority intact. Multiple scopes and
    enrollment racing task-wide cleanup prove each exact descriptor joins the
    one aggregate before authority transfer and that completing one scope cannot
    consume/close an epoch while another await/loser/value/scope component
    remains. `Closed` waits for every in-flight
    `ChildSelected|CloseSelected` node disposition but never steals or waits on
    an observer-only external result.
12. **TCB lifetime references:** retain an extra `TaskRef` across child terminal
    publication, result drain, subscription teardown, and eligible frame
    release. The TCB/registry slot remains live and unreused until the last
    checked reference is released; a stale or overflowed retain fails closed,
    zero cannot be resurrected, and a coallocated frame remains allocated until
    the same zero-reference condition.
13. **Frame-access retirement:** force preemption immediately after
    `Completed|CompletedCanceled` publication and, for a detached normal task,
    after concurrent `ReadyLive -> Taken`, but before the terminal publisher/
    coroutine final-suspend or cold finalizer releases its last frame pin. The
    frame remains allocated and inaccessible to the reclaimer until that path
    performs its final frame access and releases its pin. The reclaimer then
    competes with any still-valid claimant for `Open(0)`, wins the sole
    `Open(0) -> Retired` CAS only after revalidating the other guards, and only
    then nulls/frees storage; pin overflow or acquisition from `Retired` fails
    closed.

### 8.2 Async/place language bridge (`AB` in the semantic roadmap)

After Section 8.1 and the synchronous PlaceState/partial-`cede` prerequisites
are qualified, the separate bridge closes only when:

1. **Return commit and finalization:** typed return transfer, local cleanup
   disarm, remaining frame cleanup, result visibility, and terminal publication
   follow the exact Section 5 order for normal, caught-cancel, unhandled-cancel,
   and cold-cancel paths.
2. **Generated frame cleanup:** CodeGen's `frame_cleanup_fn` consults the same
   exact-place facts and runtime discriminators as Sema and synchronous cleanup,
   including partial places in the admitted capability slice.
3. **Cancellation CFG:** a direct current-task
   `Requested(e) -> Consumed(e)` continuation, or the shielded
   `Requested(e) -> Handling(e) -> Consumed(e)` path after async child cleanup,
   preserves live frame places. A post-`NormalClaimed`
   `ResolutionCleanup(e, oid)` preserves `Requested(e)` and its armed winner
   temporary until discharge, then takes the direct caught/unhandled boundary.
   `SourceCanceledClaimed` has no `T`: capture preserves the parent epoch,
   whereas propagation uses `SourceCleanup` to close every required structured
   descriptor before canceled finalization. A later parent request converts
   that same aggregate rather than creating a parallel cleanup path. Unhandled
   terminal cancellation has no continuing join, cannot enter finalization
   with an armed task-wide cleanup obligation, and destroys exactly the
   remaining armed obligations.
4. **Post-resolution winner cleanup:** a multi-child `NormalClaimed`
   disposition arms exactly one typed frame-local winner cleanup witness before
   any `ResolutionCleanup` suspension. Successful construction/return transfers
   and disarms it; any later cancellation edge suppressing the normal
   outcome—unhandled propagation or explicit no-value capture—destroys and
   disarms it exactly once before canceled finalization or continuing CFG.
   Neither path repeats the child's private result claim.
5. **Level-A cross-module trust:** source and retained-body-rechecked providers
   agree, and the retained body is lowered/linked by that same consumer compile
   action; a separate provider object and a bodyless standalone TKI fail closed
   for every body-derived cleanup claim.

### 8.3 Later object-attested bodyless replay

Accepted-provenance, exact-object-bound providers are an additional Level-B
gate after the Semantic Manifest payload is frozen. They must agree with
Section 8.2 and reject a mismatched or substituted object. This level is not a
prerequisite for runtime-core (`AS`) or retained-body bridge (`AB`) closure.

Each claimed level requires focused runtime race evidence and the relevant
public TaskHandle redlines. Passing an earlier phase test or ABI-only source/TKI
test is not a substitute for current-revision conformance at that level.
