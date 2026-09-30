# Toka v0.10.0

**Published on 2026-09-30.** [v0.10.0](https://github.com/tokalang/toka/releases/tag/v0.10.0)
is a public full release and the current GitHub Latest release. The
[protected promotion](https://github.com/tokalang/toka/actions/runs/36663395868)
published candidate `a1fdc191b4be453654f458d465dd9f3fb1735696` using the original
archives from [four-platform qualification run 36367398160](https://github.com/tokalang/toka/actions/runs/36367398160),
with [macOS x64 replay 36418706986](https://github.com/tokalang/toka/actions/runs/36418706986).

SDK downloads:

- [Linux x64](https://github.com/tokalang/toka/releases/download/v0.10.0/toka-v0.10.0-linux-x64.tar.gz)
- [Linux ARM64](https://github.com/tokalang/toka/releases/download/v0.10.0/toka-v0.10.0-linux-arm64.tar.gz)
- [macOS x64](https://github.com/tokalang/toka/releases/download/v0.10.0/toka-v0.10.0-macos-x64.tar.gz)
- [macOS ARM64 / Apple Silicon](https://github.com/tokalang/toka/releases/download/v0.10.0/toka-v0.10.0-macos-arm64.tar.gz)
- [SHA256SUMS](https://github.com/tokalang/toka/releases/download/v0.10.0/SHA256SUMS)

## Development route

The 1.0 release-candidate cycle has ended. Subsequent development uses the
`0.10.x` line while foundational language and standard-library contracts keep
evolving. The published RC history, including RC13, remains available with its
original tags and assets. This full-release classification on GitHub does not
declare 1.0-level API stability: major version zero remains an evolving API.
The project aims to keep `0.10.x` compatible where practical and to collect
planned breaking changes in `0.11.0`, with explicit migration guidance.

## Release contents

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

- Nested-owned-Vec support is not included in `v0.10.0`; its planned integration
  targets `0.11.0`. Dependency-contract elision (E) and new RFC implementations
  are not part of this release.
- SDK archives are published for Linux x64/ARM64 and macOS x64/ARM64. Windows
  remains a source-build/dogfood target for this release.
- `0.10.0` has lower SemVer precedence than `1.0.0-rc.13`. Installation and
  migration must use an explicit tag or the GitHub Latest
  full-release selector; version comparison alone does not express chronology.
