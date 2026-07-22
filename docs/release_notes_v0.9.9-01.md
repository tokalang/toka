# Toka v0.9.9-01 Release Notes

Toka v0.9.9-01 is a release-closure candidate for the language and compiler
contracts intended for Toka 1.0. It deliberately retains a 0.x version while
the maintainers collect current-revision release evidence. It does not add a
new language surface and is not the final 1.0 release.

## Developer tooling

- Added an installable SDK workflow with `tokac`, `toka`, `tokafmt`, and
  `tokalsp`, plus basic DWARF source, function, and local-variable metadata.
- Replaced token-guessing editor behavior with a compiler semantic index and a
  persistent in-process analysis session.
- Added semantic hover, definition, references, completion, rename, signature
  help, document/workspace symbols, formatting, and UTF-16 LSP positions.

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

## Release status

Linux x64/arm64 and macOS x64/arm64 remain the supported 1.0 target families.
The schema-version-2 release gate now qualifies the complete compiler corpus,
installed SDK, semantic index, LSP, AI interfaces, persistent-tooling scale,
sustained applications, sanitizer audit, and packaged delivery in 13
fail-closed stages.
The v0.9.9-01 tag and release archives are created only after the candidate
revision passes the unified release gate. Interface files and caches remain
compiler-version-bound and must be regenerated when upgrading.

Full changes: https://github.com/tokalang/toka/compare/v0.9.8-09-RC...v0.9.9-01
