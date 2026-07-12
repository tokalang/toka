# FZ-5 Release Candidate Gate

Status: `Blocked`

`FZ-5` establishes the release-candidate moratorium, one mandatory gate entry
point, deterministic evidence, and supported-platform CI. The implementation
and local macOS arm64 validation are complete. The phase remains blocked until
the same committed revision has clean native reports from Linux x64/arm64 and
macOS x64/arm64.

## Unified Entry Point

`tools/scripts/release_gate.py` is the only qualifying RC gate entry point. It
runs these stages in fixed order and stops after the first failure while still
recording every later stage as `not_run`:

1. compiler build;
2. complete executable positive corpus;
3. complete negative diagnostic corpus;
4. warning corpus;
5. source/source-less semantic replay;
6. semantic cache invalidation;
7. incremental build behavior;
8. focused async execution;
9. a fresh ASan/UBSan build and fixed-seed reliability audit;
10. tool build, release packaging, extraction, and package smoke.

The gate rejects a tracked dirty worktree by default and still emits a failed
report with all stages marked `not_run`. `--allow-dirty` exists only for local
development validation; reports record `source_dirty: true` and cannot qualify
as final RC evidence.

## Deterministic Report

The output schema is `toka.release-gate`, version 1. Reports contain only the
revision, native target, caller-provided version label, source cleanliness,
fixed stage order, result, exit code, and stable counts. They contain no
timestamps, elapsed durations, temporary paths, or host names.

Two complete macOS arm64 development runs with identical source and arguments
produced byte-identical JSON. Both passed all ten stages with these principal
counts:

- positive: 318 passed, 0 failed;
- negative: 237 passed, 0 failed;
- warning: 1 passed, 0 failed;
- semantic replay: 11 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- focused async: 6 passed, 0 failed;
- ASan/UBSan reliability audit: 82 passed, 0 failed;
- packaged-delivery smoke: 8 passed, 0 failed.

The local runs deliberately record `source_dirty: true` because they validate
the uncommitted gate implementation. They prove implementation behavior and
report determinism, not a final clean-revision platform result.

## Package Closure

The package smoke test verifies all four binaries, both version commands,
direct compilation and execution through the packaged compiler, and `toka
new` followed by `toka run` from the extracted archive.

This exposed a real delivery gap: the native build module invoked the
same-version `toka_build.py` incremental driver, but release archives did not
contain it and searched only repository-relative paths. The driver is now an
explicit `lib/toolchain` package component. `lib/build.tk` first preserves
source-tree development lookup, then resolves
`$TOKA_LIB/toolchain/toka_build.py`, and fails with a direct error if neither
exists.

## Sanitizer Boundary

The gate always configures a fresh ASan/UBSan compiler. macOS disables leak
checking because LeakSanitizer is unavailable and disables libc++ container
annotation checking because the linked LLVM 20 libraries are not ASan
instrumented. Address and undefined-behavior checking of tokac remain active.
The compatibility setting is macOS-only; Linux receives the default sanitizer
checks.

## Supported-Platform CI

`.github/workflows/release.yml` runs the unified gate on exactly the four 1.0
targets: Linux x64, Linux arm64, macOS x64, and macOS arm64. Every matrix row
uploads its JSON and stage logs. Test failures are no longer ignored. A release
archive is uploaded only after its complete row succeeds and the workflow was
started for a tag or explicit release label.

Windows/MSYS2, WSL2, and WASI remain outside the blocking 1.0 matrix according
to the frozen platform decision.

## Remaining Stop Condition

`FZ-5-P01` requires all four native rows to pass for one identical committed
revision. Those clean reports also close `FZ-3-P01`. Until then:

- `FZ-5` remains `Blocked`;
- the master plan remains `InProgress`;
- compiler, interface, and package versions remain on the current pre-1.0
  values;
- no 1.0 freeze or release claim is permitted.

No language-design question remains in FZ-5. RC work accepts only blocking
correctness, safety, platform, package, and documentation fixes; every such fix
invalidates prior final-gate evidence and requires all four rows again.

Milestone commit subject: `build: establish Toka 1.0 release candidate gate`.
