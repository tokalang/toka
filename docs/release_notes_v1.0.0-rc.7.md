# Toka v1.0.0-rc.7 Release Candidate Notes

These notes describe the intended RC7 contents. They are not qualification or
publication evidence. Candidate identity, hosted jobs, asset digests, replay
receipts, and release state belong in the
[RC7 qualification audit](release_audits/v1.0.0-rc.7.md).

## Language baseline

- **Safe nullable removed:** migrate optional domain values to explicit
  `Option<T>` or a nominal domain enum. Use `T | miss` only for operation
  misses. `.await?` remains the async cancellation-result syntax.
- **Raw pointer zero-address contract:** `*T` is non-zero and `nul *T` is the
  may-zero system/FFI form. Use a guard or `.unwrap()` to narrow.
- **Miss outcomes:** `T | miss` values are produced only by a function return
  (`return value` or `return miss`) and have no default construction.
- **Option unchanged:** RC7 does not migrate Option APIs to miss syntax.

## Safety and runtime corrections

- Generic consuming `Option::unwrap`, `Result::unwrap`, and
  `Result::unwrap_err` now preserve declared return dependencies without
  borrowing owned scalar/resource payloads.
- Lock guards and borrowed Option/Result payloads can no longer escape or be
  moved while their active borrow remains live. Source-less `.tki` replay
  retains the same checks.
- HashMap resize no longer double-frees replaced Vec buffers.
- Context cancellation owns its error payload and retains shared state across
  detached timer/propagation frames.
- WebSocket binary/control payloads transfer ownership into async frames.
- Nested dependent generic shapes defer materialization until outer type
  parameters are substituted, while constrained Send/Sync proofs remain
  structural and fail closed.

## Interface cache boundary

RC7 uses compiler-interface key `0.9.9-12` with `.tki` format `2`. RC6 `.tki`,
object, and semantic-cache artifacts must be discarded and rebuilt.

After publication, install RC7 explicitly rather than relying on the stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.7
```
