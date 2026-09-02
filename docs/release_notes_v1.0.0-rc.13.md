# Toka v1.0.0-rc.13 Release Candidate Notes

RC13 is the consolidated product-stabilization candidate after the RC10–RC12
black-box trials. It bundles the release-blocking and onboarding repairs found
by those trials into one candidate. It does not add syntax, change ownership
rules, alter language semantics, or expand the standard library/package scope.

Publication is permitted only after the exact candidate revision passes the
four-target release gate, clean unpublished-archive replay, the unchanged
ten-agent protocol, and supplemental human trials. Until promotion completes,
RC12 remains the latest public prerelease and its recorded no-go result remains
in force.

## Relocatable and truthful SDKs

- Basename `argv[0]` invocation now resolves the real executable through
  `PATH` without retaining a temporary-string pointer. A moved SDK can discover
  its sibling compiler, standard library, and package helper with `TOKA_LIB`,
  `TOKAC`, and `TOKA_PATH` unset.
- `toka doctor`, `new`, `run`, `check`, and `evidence` share the corrected SDK
  root in the real fresh-shell sequence, including after `package.lock` exists.
- Source builds and packaged tools report `1.0.0-rc.13`; the compiler-interface
  key remains unchanged.

## Compiler and agent-tooling consistency

- Ordinary direct calls to a declared `cede` parameter now elaborate the same
  implicit transfer during semantic checking and CodeGen for generic owning
  values such as `Vec<i32>`. The previous check-success/CodeGen-internal-error
  split is covered by compile, link, runtime, move-state, and explicit-spelling
  regressions ([#50](https://github.com/tokalang/toka/issues/50)).
- A redundant `unwrap()` on a non-nullable intermediate value is now rejected
  during semantic checking instead of being silently accepted and failing in
  CodeGen. Valid nested Result/Option method chains and split-statement forms
  remain supported and are covered by check, CodeGen, and runtime regressions
  ([#52](https://github.com/tokalang/toka/issues/52)).
- JSON semantic commands remain JSON-only even when LLVM IR would otherwise be
  emitted, preserving machine-readable agent workflows.

## Installation and project workflow

- `install.sh` downloads the release `SHA256SUMS`, verifies the exact archive
  with `sha256sum` or `shasum`, and fails before extraction or activation on a
  missing, malformed, or mismatched digest
  ([#44](https://github.com/tokalang/toka/issues/44)).
- `toka run <file>` now uses the locked project dependency graph and structured
  process arguments, matching project-aware `check` and `evidence`
  ([#46](https://github.com/tokalang/toka/issues/46)).
- The packaged Python helper parses on Python 3.9 and exits with a concise
  Python 3.10+ requirement instead of a type-annotation traceback.
- `toka new` and `toka init` create a warning-clean starter. The canonical
  TokaKV ten-minute tutorial is also warning-clean while retaining explicit
  mutable-call markers ([#45](https://github.com/tokalang/toka/issues/45)).

## Qualification and CI discipline

- Documentation-only pull requests use a lightweight diff gate. Compiler, SDK,
  workflow, and unclassified changes fail closed to the Linux x64, Linux arm64,
  macOS arm64, and Windows/MSYS2 qualification jobs.
- Manual release qualification retains four unpublished candidate archives as
  workflow artifacts without creating a tag or GitHub Release. Public release
  creation remains a separate, protected promotion step.
- Installer checksum behavior, project-aware single-file execution, starter
  diagnostics, Python 3.9 parsing, relocation, and generic implicit `cede` are
  ordinary release-gate regressions rather than post-publication spot checks.

## Platform and preview boundary

Published archives target Linux x86_64, Linux aarch64, macOS x86_64, and macOS
aarch64. Windows/MSYS2 remains a source-build dogfood target without a published
SDK archive. Toka and TokaKV remain Public Preview software; RC13 is not a 1.0
stability declaration.

RC13 keeps `.tki` format `3`, place-yield ABI schema `1`, and compiler-interface
compatibility key `0.9.9-16`. RC10 through RC13 share the frozen interface
boundary, so this stabilization candidate does not force cache invalidation.

After publication, install RC13 explicitly:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.13
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```
