# Toka v0.9.9-01 Release Notes

Toka v0.9.9-01 is the qualified engineering-closure release for the language
and compiler contracts intended for Toka 1.0. It deliberately retains a 0.x
version while the maintainers validate adoption. It does not add a new
language surface and is not the final 1.0 release.

## Highlights

- A complete installable SDK loop with the `tokac` compiler, `toka` project
  manager, `tokafmt` formatter, and `tokalsp` language server.
- Compiler-backed semantic indexing and a persistent analysis service for
  editor, automation, and AI-assisted programming workflows.
- Versioned JSON diagnostics, validated machine-applicable edits, deterministic
  semantic context, and a checked-in AI coding-task baseline.
- Qualification at realistic scale through a 6,024-line tooling project, two
  sustained reference applications, fixed-seed mutation testing, sanitizers,
  and packaged-delivery smoke tests.
- Completion of the frozen 1.0 engineering contract without publishing a 1.0
  version, tag, or release.

## Developer tooling

- Added an installable SDK workflow with `tokac`, `toka`, `tokafmt`, and
  `tokalsp`, plus basic DWARF source, function, and local-variable metadata.
- Replaced token-guessing editor behavior with a compiler semantic index and a
  persistent in-process analysis session.
- Added semantic hover, definition, references, completion, rename, signature
  help, document/workspace symbols, formatting, and UTF-16 LSP positions.
- Added basic DWARF source, function, and local-variable metadata.

## AI-oriented interfaces

- Added versioned structured diagnostics with primary and related spans and
  validated machine-applicable edits.
- Added `toka check --json`, `toka explain`, and bounded deterministic semantic
  context output.
- Added a checked-in AI coding task baseline covering compile, diagnostic,
  repair, edit-precision, context, and cost proxies.

## Scale and reliability

- Added a 21-module, 6,024-line Toka reference project and a 100-edit
  fixed-seed language-server soak.
- Added guarded root-only AST reuse, safe fallback when imports or disk
  dependencies change, and semantic-index path/source-line caching.
- Added CI gates for stale results, crashes, clean/incremental disagreement,
  warm p95 latency, and peak memory.
- Isolated same-name generic template identities across modules, including
  source-less interface replay and cache regeneration.
- Fixed an uninitialized code-generation symbol state found by GCC 11 UBSan.
- Fixed explicit runtime-object discovery in clean and packaged builds.
- Unified release qualification into one deterministic, fail-closed 13-stage
  entry point shared by local and supported-platform CI.

## Compatibility and boundaries

- The intended 1.0 source-language surface is frozen. This release accepts
  blocking correctness, safety, platform, package, and documentation fixes,
  but no further language-surface expansion.
- Toka 1.x is intended to preserve source semantics. `.tki` interface files,
  build caches, and binary ABI remain compiler-version-bound and must be
  regenerated when upgrading.
- Linux x64/arm64 and macOS x64/arm64 are the supported target families.
  Windows/MSYS2, WSL2, and WASI remain non-blocking or experimental targets.
- Standard-library breadth and third-party package availability are adoption
  work, not correctness claims made by this candidate.

## Qualification evidence

Revision `ca8181129c6d726f1295f5546171e18360b05bcb` passed all 13 stages on
Linux x64/arm64 and macOS x64/arm64 in
[release-gate run 29910583851](https://github.com/tokalang/toka/actions/runs/29910583851).
All four reports recorded `source_dirty: false` and `result: pass` with:

- 317 positive programs, 254 negative diagnostic cases, and 1 warning case;
- 18 source/source-less semantic replays and 13 cache-invalidation cases;
- 6 tooling suites, 56 checks, 5 AI evaluation tasks, and 100 scale edits;
- 31 native-build modules and 100 fixed-seed mutation cycles;
- 300 QSLite operations, 10 corruption cases, and 6 toolchain stages;
- 6 focused async cases, 82 ASan/UBSan reliability cases, and 12 package-smoke
  checks.

## Availability

v0.9.9-01 is published as the latest GitHub Release on the 0.9.9 line. Release
archives for Linux x64/arm64 and macOS x64/arm64 are attached only after the
same 13-stage gate passes on their native runners. The public version remains
0.9.9-01; this publication does not create or imply a 1.0 tag.

Qualified changes:
https://github.com/tokalang/toka/compare/v0.9.8-09-RC...ca8181129c6d726f1295f5546171e18360b05bcb
