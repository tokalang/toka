# Toka v0.9.8-09-RC Release Notes

Toka v0.9.8-09-RC is the next release candidate for the language and compiler
contracts intended for Toka 1.0. It carries the explicitly authorized language
closures and application-driven reliability work completed after
v0.9.8-08-RC. It is still a release candidate, not the final 1.0 release.

## Language Contract Closure

- Added the frozen iterator protocol with value and borrow-preserving
  iteration, source-lifetime tracking, hidden iterator cleanup, stable
  diagnostics, and source-less TKI replay.
- Formalized one `@Callable` protocol for shared, mutable, and consuming
  closures. Generic functions, iterator algorithms, async tasks, and threads
  now share the same callable contract and exact-drop behavior.
- Added one-step typed error conversion through `@ErrorInto<Target>`, including
  context preservation and synchronous/asynchronous cleanup. Exception syntax,
  `dyn error`, and `main -> Result` remain outside the 1.0 surface.

## Runtime And Compiler Reliability

- Closed detached coroutine destruction and async-context ownership paths,
  removed the unsafe pre-1.0 task-group cancellation experiment, and made
  reactor registration failure resume with an error instead of suspending
  forever.
- Added a structured process-command boundary with explicit argv, status,
  stdout, and stderr handling instead of shell-composed execution.
- Corrected resource-element cloning inside generic containers and fixed
  pointer-return member chains such as `rows.get_ref(0).key`.
- Made synthesized closure identities module-, source-, and concrete-function
  specific, preventing separate objects and TKI replay from merging unrelated
  capture layouts.
- Preserved trusted standard-library declaration provenance through generic
  specialization while retaining fail-closed validation for untrusted or
  forged TKI input.
- Corrected imported global linkage for source-less multi-object programs.

## Build And Package Supply Chain

- Added a Toka-native incremental build orchestrator and qualified it against
  the Python reference planner through fixed-seed mutations, dependency-graph
  changes, compile failures, clean rebuilds, and cache recovery.
- Reworked package execution around structured subprocess results, real
  SHA-256 content identities, deterministic lockfiles, offline replay, and
  path-safe atomic archive extraction.
- Added package dependency replay and packaged-delivery checks for local,
  registry, and cached artifacts.

## Sustained Reference Applications

- Added QSLite, a bounded single-file ordered key/value store written in Toka.
  Its qualification performs 300 deterministic operations, 313 process-level
  reopens, corruption rejection, source-less TKI execution, incremental
  rebuilds, locked package builds, and offline replay.
- Promoted the native incremental builder and QSLite into the unified release
  gate. These applications exercise combined language, runtime, interface,
  package, and build behavior beyond isolated fixtures.

## Release Engineering

- Expanded the unified RC gate from ten to twelve stages by adding native-build
  and QSLite qualification before async and sanitizer validation.
- Manual workflow dispatch can validate a release label without creating a
  tag or GitHub Release. Publication occurs only for a tag push or an explicit
  `publish_release` request.
- Release-package smoke tests verify that both `tokac --version` and
  `toka --version` match the requested release label.

Linux x64/arm64 and macOS x64/arm64 remain the supported release targets.
Windows/MSYS2, WSL2, and WASI remain non-blocking or experimental. Interface
files, caches, and binary ABI remain compiler-version-bound and must be rebuilt
when upgrading.

Full changes: https://github.com/tokalang/toka/compare/v0.9.8-08-RC...v0.9.8-09-RC
