# Toka v1.0.0-rc.9 Release Candidate Notes

These notes describe the intended RC9 contents. They are not qualification or
publication evidence. Candidate identity, hosted jobs, asset digests, replay
receipts, and release state belong in the
[RC9 qualification audit](release_audits/v1.0.0-rc.9.md).

## Ownership and safety changes

- A selected `cede` formal now defines the call's ownership boundary. The
  caller may write either `consume(value)` or `consume(cede value)` for an
  admitted non-Copy place; both forms transfer the value and invalidate the
  source.
- Explicit argument-level `cede` remains supported. For a proven `@Copy`
  place, the bare form copies and keeps the source live, while the explicit
  form requests a destructive read and invalidates the source.
- Whole owning temporaries transfer directly to `cede` formals without a
  redundant caller marker. Borrowed and raw identities keep their independent
  dependency and execution-boundary checks.
- Ordinary, static, method, dynamic-trait, callable, indirect `fn`/`dyn fn`,
  generic, source-hidden, extern, async, `.start`, and thread handoff routes use
  the same selected-formal rule within their qualified place boundary.
- A local aggregate containing owning fields can no longer be implicitly
  duplicated by value. The compiler requires an explicit destructive local
  read, preventing two live cleanup owners and a possible double free.

## Atomicity and lowering

- Multi-argument ownership calls validate type, place, alias, borrow, and
  dependency facts before committing implicit source invalidations. A rejected
  call leaves every source live.
- Mixed explicit and implicit calls restore Sema and PAL state on failure
  across direct, indirect, static, callable, method, and dynamic-trait routes.
- CodeGen diagnostic `E0761` fails closed if a cleanup-liable named source
  reaches lowering without validated call-transfer elaboration.
- Owning plain-thread closures use the qualified state-box handoff and execute
  their capture cleanup exactly once.

## Evidence and migration tooling

- `--cede-obligations=v2` emits the selected formal, caller spelling, transfer
  disposition, and source disposition as versioned JSON evidence.
- `--cede-obligations=json` retains the frozen RC8 caller-spelling evidence v1
  contract for historical consumers.
- `--warn-implicit-call-move` reports implicit calls that invalidate a named
  place. The default policy remains `allow`; Copy, borrowed identity, and
  source-less temporary transfers do not warn.
- `--experimental-signature-driven-cede` remains accepted as a deprecated
  no-op so existing build scripts do not break when the behavior becomes the
  default.

## Migration from RC8

- Caller-level `cede` is no longer mandatory when the resolved formal is
  `cede`; retaining it is valid and may be required by project lint policy.
- A bare non-Copy argument to a `cede` formal is moved. Any later use of that
  source is rejected, just as after the explicit form.
- Local initialization, assignment, return, aggregate-field construction, and
  closure capture remain explicit destructive-read sites. RC9 changes call
  arguments, not those local operations.
- Existing accepted code that depended on implicitly duplicating an owning
  aggregate must use `cede` or restructure the ownership flow.

## Interface cache boundary

RC9 keeps `.tki` format `3` and place-yield ABI schema `1`, but advances the
compiler-interface compatibility key to `0.9.9-15`. RC8 `.tki`, object,
semantic-manifest, and semantic-cache artifacts must be discarded and rebuilt.

After publication, install RC9 explicitly rather than relying on the stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.9
```
