# `@encap` Epoch Slice 0 Gate Status

**Status:** **In progress — semantic activation remains disabled.**

This is not a final Go/No-Go decision. The RFC makes all six exit conditions
mandatory for a Go decision. Missing evidence is work remaining in Slice 0;
it is not evidence that the proposed epoch is infeasible.

## Evidence achieved

- The legacy baseline is recorded in
  [encap_slice0_baseline.md](encap_slice0_baseline.md).
- Resolver records now carry an observational `Known`/`Unknown` shadow
  coordinate and dependency-manifest JSON exposes it.
- Package node IDs are derived from locked package facts, independently of
  alias and installation path, and are passed by `toka build` as `--pkg-node`.
- Missing workspace, toolchain, or package-node input remains `Unknown`; it
  cannot silently become a path-derived crate identity.
- The compiler and bundled tools rebuild successfully after the observation
  changes. No Sema, TKI, cache, access, Copy/Dup, or lowering decision has
  changed.
- `tools/scripts/test_encap_slice0_audit.py` passes: direct compilation is
  `Unknown`, workspace identity is deterministic when supplied, and locked
  package nodes are stable across aliases.
- The existing source-less semantic replay suite passes, including nominal and
  generic module-identity cases. This preserves legacy behaviour; it is not
  evidence for the proposed epoch.

## Current exit-gate audit

| RFC Slice 0 exit condition | Current evidence | Result |
| --- | --- | --- |
| Identity is deterministic under relocation and import aliases | Package node identity is alias/installation independent; workspace relocation and alias fixture matrix is absent. | Not proven |
| Same-crate and cross-crate positive/negative matrices complete | Existing `pub(crate)` is unconditional and `pub(path)` remains physical-path based; no shadow access matrix exists. | Not proven |
| Source, generated TKI, cached TKI identity decisions agree | Legacy replay passes, but it does not serialize or compare shadow coordinates. | Not proven |
| Custom-drop order/mask and Copy-SCC redlines reviewed | Legacy ownership tests exist, but no proposed hook-tail/Copy-SCC prototype or expected-result matrix exists. | Not proven |
| Forged or missing metadata fails closed | Missing resolver input produces `Unknown`; forged package/workspace/TKI metadata has no dedicated matrix. | Partially proven |
| Existing path grants resolve to one owner and target | No full standard-library, official-package, and build-tool inventory has been generated. | Not proven |

## Current consequence

1. Workspace and toolchain node IDs are not yet emitted by the toolchain, so
   their supported resolver paths remain intentionally `Unknown`.
2. No redline matrix yet compares logical and legacy authorization across
   relocation, symlink, package alias, source, generated TKI, cached TKI,
   missing identity, and forged metadata.
3. Custom-hook tail order, raw resource provenance, and typed partial-move
   actions have not been prototyped or redlined under the proposed model.
4. The three-state Copy SCC/CopyRecipe prototype and Copy/Dup overlap matrix
   do not yet exist.
5. Consequently no migration inventory or source/generated/cached-TKI parity
   result exists for the proposed epoch.

The current resolver work is useful preparatory infrastructure, but it does
not yet satisfy the semantic epoch's safety case. The correct action is to
continue Slice 0 while keeping all legacy semantics active.

## Required work before a new decision

Do not activate any proposed `@encap` epoch rule. Finish the five missing
evidence rows above, classify every legacy/shadow disagreement, then issue a
new review against the exact same exit-gate table.
