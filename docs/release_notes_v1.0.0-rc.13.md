# Toka v1.0.0-rc.13 Release Candidate Notes

## Current integrated candidate — not release-qualified

The accepted integration was fast-forwarded to local main at `fd935049`.
Local release preparation is recorded in
[the closeout log](semantic_core/review/rc13_pass_tail_closeout.md).
The current candidate includes the accepted ownership/explicit-cede and
whole-value generic migrations: the historical claims below of no language
changes and an unchanged `0.9.9-16` interface key do **not** describe this tree.
The current compiler-interface key is **`0.9.9-23`**; old TKI/cache/runtime
combinations must not be reused as if compatible.

Local macOS ARM64 prequalification at `71173047` passed a clean build and
123/123 CTest, but failed release conformance (288/325). One retained conformance
negative also reproduces compiler SIGSEGV during object generation. The SDK
archive passes its 18-check smoke test and separate relocation/QSLite consumers;
the full developer-experience suite is blocked by an old callable permission
fixture. These partial results do not qualify the archive for publication.
No new tag, push, GitHub qualification run or public release is claimed.

- `db4385dc` fixes source-visible, capture-free `fn` closure returns that could
  previously generate an uninitialized carrier and SIGBUS. Direct nonempty or
  unqualified thin-fn closure returns currently fail closed. This implementation
  limit is not a newly adopted language ban; other valid lifetime/representation
  cases still require analysis. The bounded source-hidden package is accepted
  at `55f3ab10`: retained implementation bodies are revalidated and executed
  locally with their required cleanup/helper dependencies. Declaration-only
  interfaces without the necessary proof remain rejected; this is not opaque
  binary qualification.
- Borrowed-referent name shadowing can still cause a false E0455 rejection,
  including on a direct return. This separately recorded P2 is not a new naming
  restriction and does not reopen the accepted owner-handoff safety correction.
- The separately recorded extern aggregate lowering limitation is not resolved
  or qualified by this local release-preparation run. E and `fn_` remain paused.
- Host mailbox imports move from `std/task` to `std/task_mailbox`; no compatibility
  forwarding layer is provided. GUI consumers need both import migration and
  qualification against the new SDK/version. Prior GUI qualification, including
  its old compiler-version check, is not evidence for an RC13 GUI combination.
  GUI migration is a separate consumer task, not a condition for accepting the
  `db4385dc` batch. This worktree has not modified or requalified the GUI repository.

## Historical RC13 candidate description

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
