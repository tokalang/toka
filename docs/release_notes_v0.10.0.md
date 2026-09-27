# Toka v0.10.0

**Status on 2026-09-27: pending publication.** This is source-controlled draft
copy for a future `v0.10.0` release. It is not an installation announcement and
does not claim that public archives or `SHA256SUMS` already exist. Consult the
GitHub Releases page for current publication status. Qualification evidence,
asset digests and public download links belong in the release audit when they
actually exist.

## Development route

The 1.0 release-candidate cycle has ended. Subsequent development uses the
`0.10.x` line while foundational language and standard-library contracts keep
evolving. The published RC history, including RC13, remains available with its
original tags and assets. This full-release classification on GitHub does not
declare 1.0-level API stability: major version zero remains an evolving API.
The project aims to keep `0.10.x` compatible where practical and to collect
planned breaking changes in `0.11.0`, with explicit migration guidance.

## Candidate contents

- Concrete payloads passed through unique or shared owning roots now use the
  checked payload address for ordinary direct and method call arguments. The
  repair also covers same-storage wrappers and single-evaluation conversions;
  handle identity, consuming unique and init-place paths remain distinct.
- Memory-contract verification reuses a one-shot prepared capture module while
  independently recomputing expected records and checking IR attributes. It
  remains fail-closed on changed IR, summaries, formal names and borrow mode.
- Release-validation tests cover both repairs, ELF object parity, and the
  Linux `pkg-config` prerequisite. The version-route change itself does not
  bump native ABI or the compiler-interface/TKI key.

## Boundaries

- Nested-owned-Vec eligibility and `pop`/`remove` remain unresolved and are
  reserved for a separate design. Dependency-contract elision (E) and new RFC
  implementations are not part of this candidate.
- Linux x64/ARM64 and macOS x64/ARM64 remain the planned SDK targets. Windows
  remains a source-build/dogfood target for this release.
- `0.10.0` has lower SemVer precedence than `1.0.0-rc.13`. Installation and
  migration must use an explicit tag or, after publication, the GitHub Latest
  full-release selector; version comparison alone does not express chronology.
