# Toka v0.9.8-08-RC Release Notes

Toka v0.9.8-08-RC is a release candidate for the language and compiler
contracts intended for Toka 1.0. It consolidates the PAL, ownership, effects,
async, interface replay, and runtime safety work completed since v0.9.8-07.
This is still a release candidate, not the final 1.0 release.

## Highlights

- Closed the public safety contract across payload and handle permissions,
  borrowing, explicit `cede` transfer, resource cleanup, effects, visibility,
  generics, patterns, dynamic traits, and async execution boundaries.
- Froze the existing async model around `fn -> async T`, `.await`, `.wait`,
  and `.start`, including suspension-state preservation and detached-task
  restrictions for borrowed or hidden dependencies.
- Made same-version source and source-less `.tki` imports replay the same
  safety-relevant facts. Missing, stale, malformed, or mismatched interface
  evidence now fails closed or falls back to source regeneration.
- Added deterministic semantic evidence, memory summaries, and trusted memory
  contracts for compiler analysis and future backend consumers.

## Compiler Correctness And Safety

- Hardened move, drop, closure-capture, branch, loop, and async-frame cleanup
  so transferred resources are not copied, dropped twice, or reused after
  move.
- Closed selective-import visibility leaks across functions, externs,
  globals, shapes, aliases, and traits in both source and `.tki` paths.
- Improved parser and Sema recovery for malformed grouped expressions, named
  initializers, excluded array construction, raw-pointer unwraps, and unsafe
  interface boundaries.
- Fixed Linux reactor handling to use the native `epoll_event` layout rather
  than an x64-specific packed layout, restoring async networking on Linux
  ARM64.

## Interfaces And Experimental Analysis

- Interface files and caches remain bound to the same compiler and format
  version; cross-version `.tki`, cache, and binary ABI compatibility are not
  promised. Rebuild generated interfaces when upgrading.
- Experimental `nocapture` and `readonly` contracts remain opt-in and
  non-default. `writeonly` and `noalias` are not part of this release's
  optimization contract.

## Release Engineering

- Added one fail-closed ten-stage release gate covering build, positive and
  negative suites, warnings, semantic replay, cache invalidation, incremental
  builds, async execution, ASan/UBSan auditing, and packaged-delivery smoke.
- Release archives now include the same-version incremental build driver and
  are tested after extraction through direct compilation and `toka new/run`.
- Published native archives for Linux x64, Linux ARM64, macOS x64, and macOS
  ARM64.

## Validation

All four supported release targets passed from the same clean revision:

- positive suite: 318 passed, 0 failed;
- negative suite: 237 passed, 0 failed;
- warning suite: 1 passed, 0 failed;
- semantic replay: 11 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- focused async execution: 6 passed, 0 failed;
- fixed-seed ASan/UBSan reliability audit: 82 passed, 0 failed;
- packaged-delivery smoke: 8 passed, 0 failed.

Linux and macOS are the supported release-candidate platforms. Native Windows,
MSYS2, WSL2, and WASI remain available or experimental where applicable, but
they are not blocking targets for this release candidate.

Full changes: https://github.com/tokalang/toka/compare/v0.9.8-07...v0.9.8-08-RC
