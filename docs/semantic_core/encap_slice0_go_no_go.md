# `@encap` Epoch Slice 0 Gate Status

**Status:** **Slice 0 evidence complete — Go to Slice 1 preparation;
semantic activation remains disabled.**

This is a Go only for the RFC's next implementation-preparation slice. It is
not permission to activate the new epoch or publish a mixed language state.

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
  `Unknown`; workspace relocation and symlink resolution preserve a supplied
  logical coordinate; package aliases share their locked node; and toolchain
  modules receive the configured toolchain coordinate.
- The existing source-less semantic replay suite passes, including nominal and
  generic module-identity cases. This preserves legacy behaviour; it is not
  evidence for the proposed epoch.
- [encap_slice0_redline_results.md](encap_slice0_redline_results.md) records
  the full expected-result audit: logical access, source/TKI/cache identity,
  lifecycle/resource, Copy/Dup, and the library/build grant inventory.

## Current exit-gate audit

| RFC Slice 0 exit condition | Current evidence | Result |
| --- | --- | --- |
| Identity is deterministic under relocation and import aliases | Workspace relocation and symlink fixtures preserve a supplied coordinate; package aliases share a locked node; toolchain modules have a configured coordinate. | Proven |
| Same-crate and cross-crate positive/negative matrices complete | The independent RFC model covers owner/global/crate/path/unknown positive and negative cases, and classifies all three legacy/shadow disagreements. | Proven |
| Source, generated TKI, cached TKI identity decisions agree | One workspace fixture compares source, source-less sibling TKI, cached TKI, and forged `source_path`; all retain the resolver-derived coordinate. | Proven |
| Custom-drop order/mask and Copy-SCC redlines reviewed | The independent expected-result model fixes custom hook/tail ordering, partial-move eligibility, resource provenance, Copy states, recursive layout, and Dup overlap. | Proven |
| Forged or missing metadata fails closed | Missing workspace/package node identity yields `Unknown`; a forged TKI `source_path` cannot change a resolver coordinate. | Proven |
| Existing path grants resolve to one owner and target | Inventory covers every shipped library/build grant: three module targets and the existing `std` logical namespace tree. Wildcards are separately listed for Slice 6 removal. | Proven |

## Current consequence

1. A loose direct compilation intentionally remains `Unknown`; it cannot
   obtain a crate identity from its path.
2. The redline models are audit evidence, not active Sema/CodeGen behaviour.
3. Wildcard grants, legacy clone/delete forms, structural witnesses, and
   physical-path authorization remain active until their assigned later slices.

Slice 0 supplies the safety case inputs for later implementation slices. The
correct next action is to begin Slice 1 data-model work while keeping every
legacy semantic rule active.

## Required work before a new decision

Do not activate any proposed `@encap` epoch rule. Slice 1 must preserve every
redline result and add implementation-backed coverage before Slice 2 can
change parsing or access decisions.
