# FZ-5 Release Candidate Gate

Status: `Complete`

`FZ-5` establishes the release-candidate moratorium, one mandatory gate entry
point, deterministic evidence, and supported-platform CI. The gate machinery
and the historical `v0.9.8-09-RC` four-target matrix are complete. SDK,
semantic tooling, AI interfaces, and scale qualification were committed after
that tag, so `0.9.9-01` is now the active evidence candidate. It deliberately
does not change the public version to 1.0. Preparation alone does not create a
tag.

## Unified Entry Point

`tools/scripts/release_gate.py` is the only qualifying RC gate entry point. It
runs these stages in fixed order and stops after the first failure while still
recording every later stage as `not_run`:

1. compiler, native runtime/shim, and `toka` build-manager build;
2. release-eligible executable positive corpus;
3. release-eligible negative diagnostic corpus;
4. warning corpus;
5. source/source-less semantic replay;
6. semantic cache invalidation;
7. installed SDK, semantic index, LSP protocol, AI-facing contracts and coding
   baseline, plus the 6,024-line persistent-tooling scale/soak qualification;
8. incremental build behavior;
9. source-less and 100-cycle Toka-native build qualification;
10. QSLite sustained-state, corruption, TKI, incremental, lock, and offline
   qualification;
11. focused async execution;
12. a fresh ASan/UBSan build and fixed-seed reliability audit;
13. tool build, release packaging, extraction, and package smoke.

The positive and negative compiler stages use the checked-in
`spec/ci_quarantined_*_tests.list` files. These are explicit research-fixture
boundaries, not silent ignored failures; the same lists are used by normal CI.

The gate rejects a tracked dirty worktree by default and still emits a failed
report with all stages marked `not_run`. `--allow-dirty` exists only for local
development validation; reports record `source_dirty: true` and cannot qualify
as final RC evidence.

## Deterministic Report

The current output schema is `toka.release-gate`, version 2. Reports contain
only the revision, native target, caller-provided version label, source
cleanliness, fixed stage order, result, exit code, and stable counts. They
contain no timestamps, elapsed durations, temporary paths, or host names.

Schema version 2 adds the tooling stage and records only deterministic tooling
counts (suite/check/task size and scale fixture/edit size). Host-dependent
latency and memory measurements remain in the stage log and are still enforced
by the test, but are not copied into the deterministic release report.

The historical schema-version-1 gate produced byte-identical reports in two
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
instrumented. Linux keeps the default leak checks. The gate excludes UBSan's
`vptr` sub-check on every platform because the packaged LLVM libraries are not
built with matching vptr instrumentation; ASan and all other UBSan checks
remain enabled. Generic UBSan `runtime error:` output is treated as a
fail-closed sanitizer diagnostic.

## Supported-Platform CI

`.github/workflows/release.yml` runs the unified gate on exactly the four 1.0
targets: Linux x64, Linux arm64, macOS x64, and macOS arm64. Every matrix row
uploads its JSON and stage logs. Test failures are no longer ignored. A release
archive is uploaded only after its complete row succeeds and the workflow was
started from a tag or with explicit `publish_release` authorization. Manual
dispatch can therefore qualify a candidate label, including `v0.9.9-01`,
without prematurely creating its tag or GitHub Release.

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

The clean four-target matrix later bound revision `a39c6acd` to the annotated
`v0.9.8-09-RC` tag and closed `FZ-5-P02`. Those reports remain historical
evidence. `FZ-5-P03` is closed by release-gate run `29910583851`: frozen
revision `ca8181129c6d726f1295f5546171e18360b05bcb` produced clean passing
schema-version-2 reports on Linux x64/arm64 and macOS x64/arm64. Each target
passed all 13 stages with the same stable counts:

- positive 317/317, negative 254/254, and warning 1/1;
- semantic replay 18/18 and cache invalidation 13/13;
- 6 tooling suites, 56 checks, 5 AI evaluation tasks, and the 21-module,
  6,024-line, 100-edit scale workload;
- native build reference 31 modules and 100 cycles;
- QSLite 300 operations, 10 corruption cases, and 6 toolchain stages;
- focused async 6/6, ASan/UBSan reliability audit 82/82, and package smoke
  12/12.

The workflow was dispatched without release-publication authorization, so it
created no tag, GitHub Release, or release archive. The qualified public
candidate remains `0.9.9-01`, not 1.0.

No unresolved language-design question remains in the iterator, callable, or
typed error-propagation scope.
After this replacement candidate was established, work again accepts only
blocking correctness, safety, platform, package, and documentation fixes;
every such fix invalidates prior final-gate evidence and requires all four
rows again.

The completed qualification and next adoption phase are tracked by
`docs/0_9_9_release_plan.md`.
