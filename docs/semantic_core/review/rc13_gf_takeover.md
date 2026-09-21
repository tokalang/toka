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

Full validation identifies a fixed local WIP SHA, compiler hash, source
manifest, and separate build/PASS/FAIL/CTest logs. The independently modified whole-value-generics RFC is excluded
from the checkpoint; its preserved hash is
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.

## Fixed-candidate results (2026-09-21)

Implementation: `fcead1a8c6777577f7c7a65a5f988661ca124477` (local WIP, not accepted).
Compiler SHA-256: `f6ffc10df374b986de803463f0ef579bd513fefb0af7f39bd5803866201d68c6`.
Evidence directory:
`/Users/zhyi/GitDP/tokalang/validation/gf-takeover-20260921/fixed-candidate`.
`metadata.json` records the commands, durations, compiler hash, and empty tracked
file-change lists after every suite. `source-manifest.json` and
`preserved-worktree.patch` preserve the exact source state, including the
uncommitted, untouched RFC. No implementation changed during this run.

| Gate | Actual result | Seconds |
| --- | --- | --- |
| Complete tools build | passed | 8.27 |
| Complete PASS | 452/459 | 351.98 |
| Complete FAIL | 480/480 | 133.93 |
| Unfiltered CTest | 113/115 | 1321.42 |

No abnormal compiler/runtime exits were detected in the PASS/FAIL comparison.
The new safety gate passed in this complete CTest run; its successful programs
include both original HTTP programs, scoped Wait, genuinely Copy data, and
exact-once resource transfer. Its negatives check both object and LLVM modes
without executing escaping views. JSON-factory and CSV gates passed as well.

Compared with the audit's retained earlier WIP logs (not one immutable base):

- PASS restores the two HTTP programs and has seven newly failing programs.
- FAIL remains 480/480; no oracle was changed by the takeover safety fixes.
- CTest restores JSON-factory and CSV, retains indirect-parameter failure, and
  exposes return-matrix failure. The added safety test is **not** a recovery.

Remaining positive failures, from independent same-binary check-only probes:

| Programs | First error |
| --- | --- |
| `g04_anon_records`, `g08_noshared`, `g08_test` | binding `IncompleteFacts` at anonymous record construction |
| `g09_async_wait_syntax` | binding `IncompleteFacts` at `std/task.tk:463`, opaque `TaskHandle<T>` parameter's Wait result |
| `g10_build_hybrid_test`, `g15_stdx_toml_test`, `g18_stdx_template_test` | `ElementDependenciesUnproven` at `std/vec.tk:35` |

The return-matrix failure has the same Vec first error in build-return buffers.
The indirect-parameter gate still fails its source-hidden signature test.
Full stderr is retained per failed program; `comparison.json` in the evidence
parent lists every restored/new/remaining case. These are open positive targets,
not newly declared illegal programs. Their independent causes must be resolved
without reinstating the quarantined rules by default.

This closes the takeover's safety correction and fixed-baseline measurement,
not overall integration acceptance. No freeze ref, remote, PR, or release was
changed, and phase E remains inactive.
