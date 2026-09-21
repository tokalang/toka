# GF WIP takeover — not accepted

Base: `ff20a78a12c32b2ddd145d82b43341d8a1e8e713`.
This checkpoint preserves the preceding library/test migrations. It is not an
acceptance, freeze, release, or activation of phase E.

## Safety corrections

- Wait is a temporary result, not evidence of independence. Remove its blanket
  dependency erasure. For a checked source-visible async producer, map the
  selected result contract to actual inputs and feed those origins into both
  the planner and existing return/binding lifetime checks. Unmatched, modified,
  shadowed, opaque, or unsupported task provenance remains unproved. The
  current path does not claim general method-task or compound-input support.
- Remove the added short-name Copy list. Resolve Copy from the existing
  declaration proof/recipe. A user `TimerHeap`/`SlabID` resource is not Copy by
  spelling. Resource transfer and a genuinely proven Copy declaration have
  independent runtime controls.
- Restore recursive raw-field completeness validation for result-source
  collection. Absence of borrowed fields does not prove opaque storage is
  harmless. Keep the existing `empty_storage_mutated` rejection unchanged.
- Public thread preparation and CodeGen distinguish compiler-elaborated
  temporary transfer wrappers from user `cede`. CodeGen still matches the
  exact closure construction, type, invoke, and complete Sema plan.

## Quarantined admission changes

The takeover backup is outside the worktree:
`validation/gf-takeover-20260921/before.patch` (relative to the repository parent).
The earlier audit also preserves `rc13-24-final-review.xPtHHH/implementation.diff`.

The following proposed rules have been removed from production consumers:

- container declaration identity plus element types implies complete storage;
- encountering an active recursive shape implies dependency freedom;
- a non-consuming callable is automatically dependency-free;
- RawUnsafe classification erases already-known referent/dependency facts.

`Sema_RawTake.cpp` consequently has no takeover diff against the base. There is
no new container certificate, raw-take rule, ABI, or runtime protocol here.
If earlier green results depended on these rules, they remain open rather than
being converted to new negative language expectations. Any future proposal must
identify construction, mutation, retirement, remainder, and dependency evidence;
trusted declaration identity alone is not that contract.

## Validation accounting

The audit's 24/24 subset, 480/480 FAIL, earlier 457/459 PASS and 111/114 CTest
remain separate historical runs; none is a result of this checkpoint.

New `toka_rc13_wait_copy_safety` checks normal/shadow parity, target diagnostics,
object/LLVM non-production, successful artifacts and execution. It includes
direct/bound Wait escape, lexical shadowing, scoped use, declaration-name Copy
controls, resource cleanup, the protected raw-field negative, and both original
HTTP programs. Network skipping does not count as successful execution.

Full validation must identify a fixed local WIP SHA, compiler hash, source
manifest, and separate build/PASS/FAIL/CTest logs. Results are pending until the
run completes. The independently modified whole-value-generics RFC is excluded
from the checkpoint; its preserved hash is
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.
