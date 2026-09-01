# Toka v1.0.0-rc.10 Release Candidate Notes

**Status:** Published public prerelease on 2026-08-31.

RC10 is available from the
[GitHub release](https://github.com/tokalang/toka/releases/tag/v1.0.0-rc.10)
with SDK archives for Linux x86_64, Linux aarch64, macOS x86_64, and macOS
aarch64. It remains a Public Preview release candidate, not a stable 1.0
compatibility promise.

## Ownership and cleanup correctness

- Non-consuming match and guard pattern binders are place aliases. Attempting
  to move, `cede`, or return an owning value through such a binder is rejected
  with `E04646` instead of creating a second cleanup owner.
- Consuming `match cede` and `guard ... = cede` binders receive ownership only
  for the payload fields they actually bind by value.
- Wildcard, reference, elided, and otherwise unbound payload fields retain a
  compiler-managed residual owner. Their cleanup participates in normal scope
  unwinding, including early returns and OR-pattern control flow.
- A failed consuming guard drops the unmatched active payload before entering
  its diverging `else` branch.
- Returning a ceded pattern binder clears its local drop flag, preventing the
  returned value and its source transport from becoming double owners.

## Explicit aggregate transfer

- Owning aggregate initialization and assignment require an explicit `cede`
  when the source is a named place.
- Constructing `Option` and `Result` variants from owning named values follows
  the same explicit-transfer rule.
- `Option::expect`, `Result::expect`, and `Result::expect_err` now consume their
  receiver and expose the selected payload as the unique return owner.

## Async and ecosystem qualification

- Cancellation and async-frame regressions verify that in-flight owning
  results and locals are dropped exactly once across suspension and unwind.
- Redis, PostgreSQL, and OpenAI-compatible official packages were qualified
  against the RC10 ownership rules.
- TokaKV passed its 53-stage package qualification unchanged, including full
  program ASan/LeakSanitizer coverage, five TSan concurrency runs, WAL and
  compaction crash recovery, MVCC snapshots, leases, and capability diagnostics.

## Package and SDK hardening

- Package publication uses deterministic archives with normalized metadata and
  rejects unsafe or symbolic-link inputs.
- Redis pool cancellation qualification uses a wider bounded scheduling window
  to avoid host-load flakes while preserving the cancellation assertions.

## Interface cache boundary

RC10 keeps `.tki` format `3` and place-yield ABI schema `1`, but advances the
compiler-interface compatibility key to `0.9.9-16`. RC9 `.tki`, object,
semantic-manifest, and semantic-cache artifacts must be discarded and rebuilt.

Install RC10 explicitly rather than relying on the stable release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.10
```
