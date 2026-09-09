# Public-thread qualification: unresolved implementation boundaries

Date: 2026-09-09
Status: pending-handle implementation landed in WIP; NOT Accepted

## Initial failure and implemented correction

The public library declarations, sealed source plans, packet adapters, opaque
handle representation and Result/ThreadError lowering are being integrated on
`impl/std-thread-handoff-v1`. The original missing-plan failure had two causes:
private same-module helpers bypassed the imported-symbol lookup, and their
generic method bodies could be cache-warmed before final qualification. Lexical
declaration lookup now captures the actual call, while handle recipes remain
non-authoritative until the final Sema pass. Public dynamic/state/Unit programs
now compile and run; this is not a complete integration acceptance.

Two attempts were rejected by tool-level automatic review and were NOT applied:

1. Qualifying thin/mutable environments using prior variable usage flags (and
   removing an invalid ModuleScope-origin member lookup in the same patch).
   Usage flags must not replace an actual environment identity/alias proof.
   The origin lookup has instead been repaired through the actual SourceModule;
   the toolchain-origin requirement is retained.
2. Removing the speculative guard, including a narrower retry for validated
   handle-operation specializations. Neither exception was applied. The later
   implementation does not publish a validated carrier inside the probe.

## Implemented pending/validated separation

Do not grant authority during a candidate probe. Separate a pending recipe from
the final Sema-validated source carrier:

- Prepare immutable actual/formal/edge/type/capability facts in the original
  checking context; record its exact caller definition and specialization.
- A pending recipe has Complete=false and is not attached to the executable edge.
- Promote after all module and shape checks, outside probes, only for the exact
  complete checked std/thread wrapper body, its mutable self contract, a Valid
  intrinsic specialization and matching AST edge/types. The checked body record
  is required; absence of global diagnostics alone is not qualification.
- Reuse the prepared recipe; do not re-run checkExpr, reconstruct facts from
  moved post-state, or turn global absence of diagnostics into a proof.
- Record promotion once. CodeGen accepts only the resulting validated carrier.

Required dynamic controls: candidate-warmed join/detach/drop, reverse selection
order, invalid caller/dependency/cache hit, discarded candidates, missing or
wrong-edge recipe, and normal/shadow diagnostic parity.

## Proposed safe resolution for callable environment transfer

Track a semantic environment storage identity and its ownership/alias edges.
Copy, transfer, assignment, aggregate storage, capture and opaque escape must
either preserve that relation or explicitly make the proof incomplete. Moving
the last owning thin environment and publishing mutable dynamic state requires
the complete alias/borrow closure, not HasBeenUsed/HasBeenMutated, @Send, a type
name, or a direct-initializer pattern. Unknown paths stay rejected.

This is compiler fact tracking, not a new capture layout, reference-count
algorithm, dynamic registry, or unsafe uniqueness assertion. The old thin-fn
positive targets remain positive targets; they cannot be silently replaced by
dyn-fn-only coverage. Required controls include local and projected copies,
assignment, scope exit, opaque escape, branch join, failed-call rollback,
consuming transfers, shared dynamic aliases and mutable dynamic exclusivity.

The rejected usage-flag uniqueness heuristic is not used. Current verified paths
include owned closure records, linear consuming thin callables, existing dynamic
owners, and fresh mutable constructions materialized directly into their owned
packet. Non-Copy callable checks and borrowed-formal consumption checks enforce
their actual contracts. Unknown erased-environment/alias combinations remain
rejected; original runtime goals are retained through explicit source migration.
This branch remains a WIP, not a public-thread candidate or a binding freeze.
