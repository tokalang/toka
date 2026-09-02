# Toka v1.0.0-rc.12 Release Candidate Notes

**Status:** Published public prerelease on 2026-09-02.

RC12 is available from the
[GitHub release](https://github.com/tokalang/toka/releases/tag/v1.0.0-rc.12)
with SDK archives for Linux x86_64, Linux aarch64, macOS x86_64, and macOS
aarch64.

RC12 is a single-P1 developer-experience repair candidate. Ten independent
AI-agent black-box trials showed that RC11 passed installation, TokaKV WAL
recovery, independent modification, ownership repair, project-aware semantic
JSON, warning cleanup, and stability requirements. Two trials nevertheless
found that a moved SDK invoked by its `PATH` name could lose its library root
when `TOKA_LIB` was unset.

RC12 fixes that relocation defect. It does not add syntax, change ownership
rules, modify language semantics, or expand the standard library/package scope.

## Relocatable SDK discovery

- The `toka` manager now resolves a basename `argv[0]` through `PATH` before
  deriving sibling `tokac`, `tokafmt`, and SDK library paths.
- A complete release tree containing sibling `bin` and `lib` directories can be
  moved to an arbitrary directory and used through `PATH` without an explicit
  `TOKA_LIB` override.
- `doctor`, `new`, `build`, `run`, `check`, and `evidence` now share the same
  executable-derived SDK root.
- Package-helper discovery failures emit an actionable error instead of
  returning status 1 without output.

## Release regression coverage

The ordinary developer-experience suite now mirrors the release archive layout,
copies it to a new arbitrary location, prepends only its `bin` directory to
`PATH`, removes `TOKA_LIB`, invokes the manager with a basename `argv[0]`, and
requires a fresh project to print `Hello, Toka!`.

The resulting 28-check suite passed Linux x64, Linux arm64, macOS arm64, and
Windows/MSYS2 on `main` before this candidate was prepared.

## RC11 compatibility inherited unchanged

RC12 retains the RC11 product-quality repairs:

- `doctor` validates the packaged Python 3.10+ helper and Linux OpenSSL inputs;
- Registry-backed `check/evidence` consume the locked project dependency graph;
- semantic evidence stdout remains valid JSON;
- SDK-owned `W0408` noise is removed while user-source `W0408` remains visible;
- `toka add --help` documents the Registry package-name path and package-helper
  failures preserve an actionable cause.

## Remaining non-blocking work

- scoped semantic evidence: [#38](https://github.com/tokalang/toka/issues/38);
- structured diagnostic provenance: [#41](https://github.com/tokalang/toka/issues/41);
- installer checksum verification: [#44](https://github.com/tokalang/toka/issues/44);
- canonical TokaKV tutorial `W0401`: [#45](https://github.com/tokalang/toka/issues/45);
- project-aware `toka run <file>`: [#46](https://github.com/tokalang/toka/issues/46).

These P2/P3 items are intentionally not expanded in RC12.

## Interface cache boundary

RC12 keeps `.tki` format `3`, place-yield ABI schema `1`, and the
compiler-interface compatibility key `0.9.9-16`. RC10, RC11, and RC12 use the
same frozen language/compiler interface boundary; this DX-only candidate does
not require a new cache invalidation.

After publication, install RC12 explicitly:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.12
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```
