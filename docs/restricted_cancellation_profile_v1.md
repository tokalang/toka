# Restricted Cancellation Profile v1

`restricted_cancellation_profile.v1.json` records the direct-task cancellation
subset already exercised by Toka's compiler, runtime, conformance suite, and
official Redis/PostgreSQL pool consumers.

## Status

**Candidate, not stable.** This profile intentionally does not amend the
revision-bound `TaskHandle` lifecycle v2 record. Lifecycle v2 remains the
qualified subset for cold tasks, direct result claims, `.await?`, and detached
drain. This document formalizes the cancellation evidence without asserting
the full Async Runtime TCB contract.

## Dual-Track Architecture & Included Semantics

The profile cleanly bifurcates cancellation into two orthogonal tracks:

### Track A: Task Cancellation (`task_cancel(handle)`)
- Cooperative request observed at supported suspension points (Timer / TCP read); if a normal result has already been claimed, that normal result remains valid.
- `RCP-G1`: Cooperative request observable at suspension points; never fabricates a normal result.
- `RCP-G2`: Direct `.await?` represents canceled child or current-task cancellation as `Option::None`.
- `RCP-G3`: Cold cancel/drop runs frame-owned cleanup before canceled terminal publication.
- `RCP-G4`: Whole-place, direct-field (`cede s.f`), and fixed-array-index (`cede arr[i]`) cancellation exactly-once drop.
- `RCP-G5`: Normal direct-result claim is one-shot and cannot be revoked by a later cancellation request.
- `RCP-G6a-Timer`: Timer suspension cancellation logically invalidates the wait token, drives task to terminal completed-canceled, executes frame cleanup without post-await user CFG, and prevents resurrection on timer expiry.
- `RCP-G6a-TCP`: TCP read suspension cancellation (on POSIX) unregisters WaitRegistry and OS reactor subscriptions, achieves kernel event silence upon writable peer, and executes frame cleanup without post-await user CFG.

### Track B: Explicit Context Cancellation (`Context Canceler`)
- In-band recoverable domain cancellation where the Task itself remains alive and healthy.
- `RCP-G7a`: Direct `Canceler.call()` on raw TCP read/accept returns the propagated cancellation error (`"context canceled"`), caller continues execution retaining raw buffer allocation or listener instance, and the same `TcpListener` instance can accept subsequent connections.

## Excluded & Deferred Semantics

The profile explicitly defers:
- TLS suspension cancellation across handshake, read, and write (subject to OpenSSL state machine nuances).
- Context deadline timeout error propagation (deferred to dedicated qualification).
- Owner-carrying async buffer restitution (e.g. `read_into_async_context` returning an owned `Buffer` on cancel).
- Concurrent dual-source cancellation (`task_cancel` and `Canceler.call()` fired simultaneously).
- `TaskScope` close progress, tree propagation, and multi-source `race2`/`select2` loser draining.
- Scoped borrowed tasks, dynamic projections, slices, and arbitrary nested async `cede` cleanup.

## Verification

Run from the `toka` repository root:

```sh
python3 tools/scripts/test_restricted_cancellation_profile.py --build-dir build \
  --conformance-output /tmp/toka-restricted-cancellation.json
```

The gate validates only the evidence named in the profile. A passing result is
candidate evidence, not permission to claim the deferred semantics.
