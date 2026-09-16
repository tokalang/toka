# Channel private storage contract (candidate, not Accepted)

Scope: the source-visible SDK Channel factory and its exact endpoint instances.
This is not general recursive-container, wrapper, Send, or ClosedPayload proof.
No runtime protocol, carrier layout, native ABI, or public signature changes.

## Publication invariant

Each endpoint owns one counted endpoint role and one reference to the same live
shared core. The core owns its queue storage, mutex, and two condition variables.
The queue owns exactly its live elements. Element qualification uses the existing
proven domain and actual instantiated operations; a type name or Send alone is
insufficient. WaitQueue is not universally ClosedPayload.

Construction prepares native children and an empty owned queue before publishing
the Sender/Receiver pair. A failed construction publishes neither endpoint.
Channel allocation/factory failure follows existing infallible SDK failure policy;
no new recoverable public result is introduced by this contract.

Clone locks the same core, checks the count, acquires one endpoint count, and only
then constructs the new endpoint/reference. Lock/count failure must not return a
new endpoint. The current infallible clone signatures require termination on such
failure; they cannot silently publish an uncounted alias. No cleanup guarantees
are invented for process termination. Normal clone/drop must remain exact-once.

Send owns its input. It either publishes it into the queue once or releases it
on failure. Bounded send retains its wait loop and must observe close after wake.
Recv takes one live queue element and retires that slot before returning it.
Growth preserves live elements, capacity and cleanup responsibility under the
existing queue/raw_take implementation contract, not a fabricated initialized
extent proof. Unbounded operation is not replaced with a fixed-capacity queue.

Last-sender close wakes empty-queue receivers; queued values remain drainable.
Last-receiver close wakes blocked senders. Remaining queued values stay owned by
the core and are destroyed once on final core release. Endpoint drop must not
silently abandon its count when locking fails. Native children outlive every
guard/waiter; final storage release occurs only after all endpoint owners retire.

## Safe operations remain closed after publication

Endpoint fields and constructors are governed by the actual nominal Encap policy
and defining-module identity. External safe code may not access, overwrite,
destructure, forge or reinitialize the core fields through an endpoint, including
through aliases or extension methods. Whole endpoint transfer/replacement remains
legal only with its ordinary source and cleanup checks. Each permitted private
operation preserves the shared invariant; merely invalidating one local witness
is NOT an adequate response to a shared-storage mutation.

Unsafe raw escapes do not acquire this contract, ownership or extra lifetime.
Missing/invalid provenance and source-hidden providers cannot publish a witness.

## Qualification and validation

An unforgeable Sema record must bind the actual factory edge/instance, role,
shared core, complete element type, queue storage and native child plans.
Clone derives from that exact owner only after the checked operation succeeds.
Capture/transfer retains that identity with existing rollback and liveness rules.
CodeGen validates exact edges, types, declaration identities and completion;
it does not rebuild ownership or accept an independent boolean/type-name escape.

Required candidate matrix: independent Sender; both original MPSC programs at
unchanged concurrency/backpressure; nonempty queue cleanup; failed send;
last-endpoint wakeups; clone/drop counts; forged sources, borrowed elements,
unsafe exposure/invalid evidence, missing or mismatched plans. The three already
recovered stress programs remain controls. This document is not Accepted.

## Implemented candidate evidence

- Both public factories delegate to the same private `__channel_create` source
  definition. The plan checks its actual instantiated body, allocation, original
  field set, endpoint declaration identities and Encap policy. Both endpoints
  must refer to the actual core binding ID, not a later name lookup.
- The initial queue is tied to the checked WaitQueue/RingCore/Vec constructor
  edges, including zero live length/capacity and a null buffer. This finite SDK
  construction check is not an arbitrary raw-storage history or recursion proof.
  Normal queue mutation remains the private library implementation's contract.
- Three actual native child preparations are matched to the exact fields and
  element type. Sema requires completed definitions; CodeGen consumes the native
  finalizer's sealed copies after comparing all prepared identity/type fields.
- Capture witnesses match the actual closure capture entry and environment
  declaration. Non-Channel synthesized closures are not newly qualified.
- Unknown mutable calls retire the affected binding's candidate identity.
  This is conservative local provenance handling, NOT a substitute for the
  invariant-preserving operations and inaccessible shared core fields above.
- `send` immediately moves its ceded input into an owning local before locking.
  This uses existing local cleanup on every error return; it does not change the
  general synchronous parameter ABI/cleanup implementation. A leaked failed-send
  input was observed before this library adaptation and is now covered.

Concentrated CTest: Channel storage, original five thread programs, native owner
witness, public thread/sync, ClosedPayload: 5/5 (91.81 s). Afterwards the Channel
test was strengthened with post-publication private-write denial and controlled
allocation-failure assertions; its final standalone run also passed.

The runtime matrix covers 64 unique nonempty-queue payload drops across growth,
closed-send input cleanup, remaining-clone usability, both last-endpoint wakeups,
lock failure/retry, clone lock failure without publication, native init failure
and allocation failure without publication. The allocation/clone cases terminate
by the established failure paths, not SIGSEGV; the native runtime is not patched.
Four source rejection cases retain parity and no object/IR. Fourteen missing or
wrong witness checks require E0701 and no artifact. All five original thread
programs run at their existing thread/iteration/backpressure budgets.

No full-suite results are inferred. No public signature, runtime protocol, ABI,
reference-count algorithm, TKI or E change. Source-hidden/unproven element cases
do not gain this contract. This candidate does not repair unrelated early type
instantiation failures exposed by some concrete Sender formals; the unknown-call
invalidation regression uses a generic formal to reach the intended check.
