# Toka v0.11.0

**Status on 2026-09-30: integration candidate, pending qualification and publication.**
There is no public `v0.11.0` SDK download. The published `v0.10.0` tag and
assets remain the reproducible installation entry point. This source release
note does not claim that local validation is hosted four-platform qualification.

## Candidate contents

- Owning nested Vec elements now work with the production standard library:
  construction, growth, replacement, borrowing, extraction, consuming iteration
  and exact-once cleanup. `pop` delegates tail retirement to `remove`; the
  standalone production `raw_take` check remains in place.
- Explicit `self <- val` effects conservatively carry an argument's external
  dependencies into a mutable receiver. `return <- self.external` forwards
  existing external sources when extracting a value, while a fresh element
  borrow still depends on container storage. Method, qualified and direct
  calls, assignment and source-hidden TKI preserve these contracts.
- PAL protects the real external owner while a holder remains live. Branch
  joins, return/propagation exits, break/continue cleanup, closure boundaries
  and guard binding use the facts of the reachable path. Failed calls and
  assignments restore their prior state.
- Vec storage fields are private. Raw construction, storage access, length
  mutation and ownership-forgetting methods require an explicit unsafe call.
  Standard-library adapters have been migrated to these boundaries.
- Scalar and owning `T | miss` values retain their payload's independence
  through initialization; borrowed payloads still carry their real sources.
  Shared view tests retire a holder before modifying its source and preserve
  rejection controls for mutation while that holder remains live.
- The active qualification, replay, draft and protected promotion workflows
  validate canonical `v0.11.N` labels and retain SHA, four-target report,
  archive, checksum and replay identity checks. Qualification dispatches
  remain evidence-only; tag publication creates a draft; protected promotion
  separately authorizes public release and Latest.

## Migration and compatibility

See [the Vec migration guide](migration_0_11_vec.md) for public-field, unsafe
API and dependency-contract changes. This is a planned public-contract change
from the `0.10.x` line, not a 1.0 API-stability declaration.

The accepted compiler semantics use TKI format `5` and compiler-interface
identity `0.9.9-38`; older interfaces and semantic caches must be regenerated.
Native layout and calling ABI were not changed by this integration or by the
public version update. Default compiler, CLI and SDK build metadata are
`0.11.0`, with the explicit build-version override retained.

## Remaining boundaries

- Unknown sources and unknown projections remain conservatively rejected.
- `remove` and `clear` may conservatively retain an old element dependency;
  they do not promise precise dependency deletion.
- Unsafe callers maintain allocation compatibility, initialization ranges,
  slot retirement, aliasing and cleanup responsibility. The compiler does
  not automatically prove arbitrary raw container algorithms.
- Existing legal `Vec<&T>` paths remain supported. This package does not
  promise every new borrowing-element path, generic constraints, E elision,
  a PAL redesign or unrelated RFC implementations.
- Release qualification must be obtained for the new integration SHA on
  Linux x64/ARM64 and macOS x64/ARM64. Earlier Vec tests and `v0.10.0` reports
  do not qualify this candidate.
