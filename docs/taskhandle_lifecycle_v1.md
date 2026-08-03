# TaskHandle Lifecycle Contract v1

The versioned contract in
[`spec/taskhandle_lifecycle.v1.json`](../spec/taskhandle_lifecycle.v1.json)
is the machine-readable source for the lifecycle of a move-only
`TaskHandle<T>`. Its schema is
[`schemas/toka.taskhandle-lifecycle.v1.schema.json`](../schemas/toka.taskhandle-lifecycle.v1.schema.json).

It freezes the facts an AI tool needs before proposing an async repair:

- an async call creates a cold `created` task;
- activation by `.start`, `.await`, or `.wait` must not inline the body;
- cancellation has no fabricated `T` payload and propagates through `.await`;
  `.await?` is the explicit `Option<T>` cancellation-capture boundary;
- dropping a cold handle reclaims its frame, while dropping/detaching active
  work transfers lifetime responsibility to the runtime;
- executor shutdown drains active detached work; and
- a normal result is claimed once while `ready-live`; typed transfer or
  destruction finishes before the public
  `pending -> ready-live -> taken` protocol reaches `taken`.

`ready-live` means that the payload ownership commit has occurred, not that a
consumer may immediately read it. Every awaiter, waiter, joiner, or detached
dropper must first acquire-observe normal terminal completion, then acquire the
public result state and compete for the private one-shot claim. This terminal
gate keeps frame-local finalization and join publication ordered before result
consumption without adding a fifth public result state.

The `activate` record includes already activated and terminal source states so
the repeated-`start` no-op is represented rather than merely asserted in prose.
Only `created` may transition into a first queued execution; every other listed
source preserves its lifecycle state and cannot create another execution.

Detach transfers the result obligation atomically to
`RuntimeOwned(Detached)`. Detach and terminal publication are both required to
run the same idempotent drain check,
so either ordering observes a normal terminal `ready-live` result, wins the
private claim once, performs typed destruction, and publishes `taken`. Sampling
a detached flag once on each side is not sufficient because both sides could
miss the other's later publication.

Cancellation of a parent blocked in `.await`/`.await?` first enters an internal
cleanup barrier: the retained child is canceled, joined to terminal with no
active registration, and any normal result is cancel-owned and typed-dropped;
this path never transfers `T` to its source continuation.
Only then may `.await?` expose `None` or ordinary `.await` propagate toward
parent finalization. This barrier may suspend internally but cannot expose user
control flow while the child obligation remains armed.

Normal child-result disposition and parent cancellation are resolved by one
internal claim, not by sampling cancellation and handing off the result later.
If normal disposition wins, the awaiting frame becomes the unique result owner
and completes mandatory child cleanup before user control flow; a parent
request admitted afterward remains pending for the immediate post-cleanup
boundary. That request cannot double-claim the child, but it may suppress
source-visible delivery while ordinary frame cleanup destroys the transferred
value. If cancellation wins first, the internal barrier owns the child drain
and no `T` reaches that continuation.

If a structured scope consumes a cold `TaskHandle`, it must transfer the
handle's lifecycle/result obligation and link the child into the scope before
activation. Letting the consumed handle run ordinary drop between an untyped
`TaskRef` conversion and a later `track_ref`/`start` is not a valid handoff. A
failed pre-commit enrollment leaves cleanup authority with the caller; a
committed enrollment is discharged by the scope's closing path.

This contract describes public lifecycle behavior, not the private C struct
layout or every scheduler-internal transition. `preparing` and the wait
registry remain implementation mechanisms and therefore do not appear as
public operation source states; their in-flight behavior is covered by the
surrounding `running`/`suspended` obligations. The public contract is the
stated operation, resource, and result obligation.

The `cancel.target_states` list includes `suspended` because a repeated request
during an internal cleanup-only suspension coalesces and may help retained
work, but cannot uninstall or win that cleanup WaitSet. A request admitted
against an ordinary suspension instead wakes or queues the task. The list also
includes both immediate delivery states and the two possible eventual terminal
states. Unhandled cancellation reaches
`completed-canceled`; an explicit `.await?` capture of the current task's
request may consume it once, continue in `running`, and later reach ordinary
`completed`. This is an erratum aligning the operation record with the v1
redline that already required current-task cancellation to return a domain
outcome. It changes neither the v1 schema nor the four-value public result ABI.
The terminal-before-claim rule, two-sided detached-result drain, and awaited-
child cancellation barrier similarly make existing v1 result/lifetime
obligations precise; they add no public operation or result-state value.

## Tool use

For a cancellation, ownership, or task-lifetime edit, inspect the contract and
run only the redline programs relevant to the proposed change. A tool must not
assume that cancellation yields a zero `T`, that dropping a cold task starts
it, or that `block_on` may leave runtime-owned detached work behind.

`tools/scripts/test_taskhandle_lifecycle.py` validates the frozen JSON shape
and compiles/runs every listed redline program under a timeout. The runtime
implementation and broader ABI evidence remain documented in
[`async_runtime_p5_spec.md`](async_runtime_p5_spec.md) and
[`async_runtime_p6_abi_baseline.md`](async_runtime_p6_abi_baseline.md).

Each JSON `redline_tests[].proves` entry names the obligation that a passing
test is intended to witness; it is not a current-conformance status field.
Only a recorded green run at an exact revision turns that target into evidence.

Passing that runner would not by itself close the normative TCB concurrency
matrix for queue-publication preemption, completion-subscription arming,
shielded await cleanup, detach/terminal drain, or structured-child registry
races. At this documentation-closure revision the runner is still red:
`g11_async_p5_redline_test.tk` fails to compile because the current `tokac`
process exits 139 without a diagnostic. That is a P-1 implementation blocker,
not a reason to weaken the contract or report current conformance.
