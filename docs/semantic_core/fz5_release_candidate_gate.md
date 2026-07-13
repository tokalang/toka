# FZ-5 Release Candidate Gate

Status: `InProgress`

`FZ-5` establishes the release-candidate moratorium, one mandatory gate entry
point, deterministic evidence, and supported-platform CI. The gate machinery
is complete. The explicitly authorized late iterator protocol invalidated the
previous final-revision evidence. The later authorized callable protocol also
belongs to the replacement revision, as does the authorized typed
error-propagation closure. Package-supply-chain and QSLite fixes also belong to
the replacement revision. `v0.9.8-09-RC` preparation is active, but a
replacement four-target clean matrix is still pending. Preparation alone does
not create the tag.

## Unified Entry Point

`tools/scripts/release_gate.py` is the only qualifying RC gate entry point. It
runs these stages in fixed order and stops after the first failure while still
recording every later stage as `not_run`:

1. compiler, native runtime/shim, and `toka` build-manager build;
2. complete executable positive corpus;
3. complete negative diagnostic corpus;
4. warning corpus;
5. source/source-less semantic replay;
6. semantic cache invalidation;
7. incremental build behavior;
8. source-less and 100-cycle Toka-native build qualification;
9. QSLite sustained-state, corruption, TKI, incremental, lock, and offline
   qualification;
10. focused async execution;
11. a fresh ASan/UBSan build and fixed-seed reliability audit;
12. tool build, release packaging, extraction, and package smoke.

The gate rejects a tracked dirty worktree by default and still emits a failed
report with all stages marked `not_run`. `--allow-dirty` exists only for local
development validation; reports record `source_dirty: true` and cannot qualify
as final RC evidence.

## Deterministic Report

The output schema is `toka.release-gate`, version 1. Reports contain only the
revision, native target, caller-provided version label, source cleanliness,
fixed stage order, result, exit code, and stable counts. They contain no
timestamps, elapsed durations, temporary paths, or host names.

The original ten-stage implementation produced byte-identical reports in two
equal macOS arm64 development runs. The expanded `v0.9.8-09-RC` gate passed a
complete dirty-source development run with these principal counts:

- positive: 326 passed, 0 failed;
- negative: 252 passed, 0 failed;
- warning: 1 passed, 0 failed;
- semantic replay: 14 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- native build reference: 31 modules and 100 fixed-seed cycles;
- QSLite: 300 operations, 10 corruption cases, and 6 toolchain stages;
- focused async: 6 passed, 0 failed;
- ASan/UBSan reliability audit: 82 passed, 0 failed;
- packaged-delivery smoke: 12 passed, 0 failed.

The local runs deliberately record `source_dirty: true` because they validate
the uncommitted gate implementation. They prove implementation behavior and
report determinism, not a final clean-revision platform result.

## Package Closure

The package smoke test verifies all four binaries, checks that both version
commands match the requested release label, verifies the package helpers,
directly compiles and executes through the packaged compiler, runs `toka new`
and `toka run`, and replays a locked local dependency offline.

This exposed a real delivery gap: the native build module invoked the
same-version `toka_build.py` incremental driver, but release archives did not
contain it and searched only repository-relative paths. The driver is now an
explicit `lib/toolchain` package component. `lib/build.tk` first preserves
source-tree development lookup, then resolves
`$TOKA_LIB/toolchain/toka_build.py`, and fails with a direct error if neither
exists.

## Sanitizer Boundary

The gate always configures a fresh `-O1 -g` ASan/UBSan compiler and runs the
fixed-seed audit with a 30-second per-process hard limit. Optimization keeps
the instrumented compiler practical on native ARM64 without removing address
or undefined-behavior instrumentation. Audit failures distinguish timeout,
signal, and sanitizer diagnostics in deterministic text. macOS disables leak
checking because LeakSanitizer is unavailable and disables libc++ container
annotation checking because the linked LLVM 20 libraries are not ASan
instrumented. Linux receives the default sanitizer checks.

## Supported-Platform CI

`.github/workflows/release.yml` runs the unified gate on exactly the four 1.0
targets: Linux x64, Linux arm64, macOS x64, and macOS arm64. Every matrix row
uploads its JSON and stage logs. Test failures are no longer ignored. A release
archive is uploaded only after its complete row succeeds and the workflow was
started from a tag or with explicit `publish_release` authorization. Manual
dispatch can therefore qualify the `v0.9.8-09-RC` label without prematurely
creating its tag or GitHub Release.

Windows/MSYS2, WSL2, and WASI remain outside the blocking 1.0 matrix according
to the frozen platform decision.

## Supported-Platform Evidence

Release-gate run `29202522704` tested tag `v0.9.8-08-RC` at committed revision
`3ab00dff` on Linux x64/arm64 and macOS x64/arm64. All four rows report
`source_dirty: false` and `result: pass`. The Linux arm64 report records:

- positive 318/318 and negative 237/237;
- warning 1/1, semantic replay 11/11, and cache invalidation 12/12;
- focused async 6/6 and fixed-seed sanitizer audit 82/82;
- package smoke 8/8.

The other three rows passed the same fixed stage sequence at the same
revision. This historically closes both `FZ-5-P01` and `FZ-3-P01`, but it does
not cover the later iterator protocol. `FZ-5-P02` tracks the replacement
Linux/macOS x64/arm64 matrix required for the current revision.

The local expanded-gate run proves the `v0.9.8-09-RC` preparation is runnable,
including both sustained applications and version-matched package smoke. It
records `source_dirty: true` and revision `1662f4f1`, so it is development
evidence only. `FZ-5-P02` remains open until all four clean reports bind the
committed preparation revision to the `v0.9.8-09-RC` label.

No unresolved language-design question remains in the iterator, callable, or
typed error-propagation scope.
After its replacement RC is established, work again accepts only blocking
correctness, safety, platform, package, and documentation fixes; every such fix
invalidates prior final-gate evidence and requires all four rows again.

Preparation commit subject: `build: prepare v0.9.8-09-RC gate`.
