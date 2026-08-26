# RC9 Ownership Transfer Plan — Draft / Non-normative

**Status:** RC9 M0 audit only. This document is not a language contract and
does not authorize semantic, TKI, ABI, or surface-syntax changes.

**Baseline:** `v1.0.0-rc.8` / `997713f4828b43a5b82aa3363d99a37e9e6f2417`.

## Audit model

Every ownership-sensitive edge is reviewed along three independent axes:

1. **Transfer decision** — which semantic fact selects move, retain, value
   copy, or identity copy.
2. **Source invalidation** — whether the source place becomes `Moved`, has a
   projection bit cleared, or has its physical slot zeroed.
3. **Drop liability** — which place owns the matching destructor or shared
   release after the edge.

Closing only a drop flag is not a move proof. Clearing a slot is not a Sema
fact. Retaining a shared handle is not permission to keep a unique source
live. RC9 implementation work must preserve those distinctions.

## RC8 CodeGen call-site ledger

The RC8 baseline has ten calls to `suppressDropForMove()` (excluding its
definition). Line numbers are discovery receipts, not stable identifiers.

| Edge | Current selector | Source invalidation | Drop liability | M0 classification |
| --- | --- | --- | --- | --- |
| Method captured cede argument | `CedeExpr` plus `Arg.IsCeded` | Partial/whole cleanup suppression; no uniform slot rule | Callee | Legacy contextual reconstruction |
| Consuming method receiver | `self.IsCeded` and receiver morphology | Unique heap slot has a separate post-call path | Callee | Legacy multi-path reconstruction |
| Consuming closure/dyn receiver | `CallableReceiverMode::Consuming` | Environment may be freed separately | Callee | Legacy contextual reconstruction |
| Captured ordinary call cede argument | `CedeExpr` plus address-passing choice | Whole or projection cleanup suppression | Callee | Legacy contextual reconstruction |
| Consuming callable value | `CallableReceiverMode::Consuming` | No uniform source-slot fact | Callee | Legacy contextual reconstruction |
| `genCedeExpr` | Source syntax and resolved morphology | Shared slot is zeroed; other forms vary | Destination/callee | Partially explicit, not unified |
| Struct spread `CededBases` | Sema-populated name list | Cleanup suppression only | New aggregate | Legacy side channel |
| Ceded closure capture | Capture metadata | Source pointer/aggregate is zeroed | Closure environment | Explicit physical path, no common plan |
| Return value | Return shape and CodeGen expression walk | Unique direct variables may be zeroed separately | Caller | Legacy CodeGen inference |
| Aggregate transfer | `AggregateTransferKind` from Sema | `MoveOwned` clears the source slot | Plan-selected destination/source | Qualified RC8 reference path |

## Candidate internal model

The aggregate path is the M0 reference, not yet the universal design:

```text
TransferDisposition
    MoveOwned
    RetainShared
    CopyValue
    CopyIdentity

SourceDisposition
    KeepLive
    MarkWholeMoved
    MarkProjectionMoved(path)
    ClearPhysicalSlot

DropDisposition
    SourceRetainsLiability
    DestinationAssumesLiability
    SharedLiabilityIncremented
    NoLiability
```

These are logical dimensions. An implementation may use a sum type to remove
invalid combinations, but M0 deliberately does not freeze its representation.

## RC9 implementation admission criteria

Implementation remains blocked until RC8 qualification is resolved and a
separate review approves all of the following:

- Sema is the sole authority for every reachable transfer decision.
- PAL and `PlaceState` record the same source invalidation before CodeGen.
- CodeGen fails closed when a liability-bearing edge lacks a plan.
- Shared retain/release and unique move/drop matrices are source-backed and
  source-hidden.
- Branch, loop, partial move, closure, async, return, and call edges have
  explicit tests.
- No TKI or ABI change is inferred from this draft.

## M0 exclusions

- No new syntax or hats.
- No alias return or general place calculus.
- No TKI/interface version change.
- No mutation of RC8 release objects or hosted workflows.
