# `@encap` Epoch Slice 0 Go/No-Go Review

**Decision:** **No-Go for semantic activation. Continue Slice 0 evidence work.**

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

## Blocking evidence still absent

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

## Required next gate

Do not activate any proposed `@encap` epoch rule. Finish the five missing
evidence rows above, classify every legacy/shadow disagreement, then replace
this report with a reviewed Go or permanent No-Go decision.
