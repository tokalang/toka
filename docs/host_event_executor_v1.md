# Host Event / Async Executor Integration v1

Status: `implemented` — narrow foundation slice.

## Problem

`std/task` already owns one executor for ready tasks, timer wakeups, and OS
socket readiness.  A native GUI host has a separate event source: on macOS,
AppKit must be pumped on its UI thread.  Making applications alternate an
ad-hoc `App::wait_for_event(16)` loop with async work leaves the scheduling
relationship implicit and encourages a second, competing loop.

This contract adds a **bounded, synchronous host-event source** to the
existing executor.  It deliberately does not make `std` depend on GUI or on
any platform toolkit.

## Public v1 surface

`std/task` defines:

```toka
pub shape HostEventPoll (
    Event = 1 |
    Idle = 2 |
    Stopped = 3
)

pub trait @HostEventSource {
    pub fn poll_events(self#, timeout_millis: i32) -> Result<HostEventPoll, string>
}

pub shape ExecutorPump (
    ready_tasks: i32,
    timer_wakes: i32,
    reactor_events: i32,
    host_poll: HostEventPoll
)

pub fn pump_with_host<'H: @HostEventSource>(
    host#: H,
    max_wait_millis: i32
) -> Result<ExecutorPump, string>
```

`poll_events` is synchronous and non-escaping.  The executor never stores the
host source, invokes it from a worker thread, or retains a raw native handle.
Consequently a GUI package can implement the trait for a UI-affine `App`
without making `App` `@Send`.

`Stopped` is an observation, not implicit cancellation.  The application owns
its cancellation policy and decides whether to request cancellation of its
root task or continue draining work.  An adapter failure is returned as a
typed `Err(string)`; it is never converted into `Idle`.

## One pump turn

For non-negative `max_wait_millis`, one turn is ordered as follows:

1. reap completed detached tasks and drain all currently ready tasks;
2. wake expired timers and drain their ready continuations;
3. if no work was made ready, compute
   `min(max_wait_millis, next_timer_deadline)`; otherwise use zero;
4. invoke the host source once with that bounded timeout;
5. wake timers that expired while the host waited, poll the socket reactor
   with timeout zero, and drain resulting ready tasks.

The host wait therefore cannot defer a timer beyond its deadline, and no
ready task waits for a later application iteration once a non-blocking reactor
poll made it ready.  An OS socket event may wait at most the caller-selected
host bound; GUI callers should use a small frame-friendly bound (for example,
16 ms).  This is a bounded fairness contract, not a promise that AppKit and a
kernel reactor are atomically co-waited in v1.

The existing `block_on` / async-main executor keeps its current reactor-only
behavior.  This preserves CLI and server semantics; a host application opts
into the bridge explicitly by calling `pump_with_host` in its UI-thread loop.

## GUI adapter

`official/gui::App` maps its existing bounded AppKit pump to
`@HostEventSource`:

- event handled → `Event`;
- deadline without an event → `Idle`;
- stopped app → `Stopped`;
- AppKit pump failure → `Err(message)`.

The adapter is a package implementation of a `std` trait.  `std/task` imports
no `official/gui` type, Objective-C ABI, framework, or platform conditional.

## Safety and non-goals

- The source is only called by its caller's current thread.  Existing
  `@Send` checks continue to reject moving `App` or `Window` into
  `thread_spawn`.
- The executor never accepts a raw event handle, callback registration, or
  borrowed callback that could outlive the call.
- There is no implicit UI cancellation, global event-source installation,
  nested executor, worker-to-UI dispatch, or cross-platform GUI promise.
- Native backends may later provide a lower-latency co-wait primitive, but
  that is a separate platform-specific extension.  It must preserve this
  source ownership and bounded-wait contract.

## Acceptance evidence

1. a pure-Toka fake `@HostEventSource` proves a timer-suspended coroutine
   completes through repeated `pump_with_host` calls;
2. the fake source records that every received timeout is non-negative and no
   larger than the requested bound;
3. a source failure is returned rather than silently ignored;
4. existing async timer/reactor qualifications remain green;
5. the GUI package consumer build proves that `App` implements the standard
   trait without `std` acquiring a GUI dependency.  Interactive AppKit
   behavior remains a macOS desktop-session gate.
