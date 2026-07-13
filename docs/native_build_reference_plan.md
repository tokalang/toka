# Toka Native Build Reference Application Plan

Status: `InProgress`

This document governs the long-running reference application used to qualify
Toka for 1.0. The application is the existing Toka-native incremental build
orchestrator rooted at `lib/build.tk`. It is production-shaped validation, not
a new language feature and not a replacement for minimized regression tests.

The work belongs to `FZ-5` hardening. It must not introduce syntax, change the
meaning of accepted programs, or expand the frozen 1.0 language surface.
Semantic ambiguity discovered here requires an explicit owner decision.

## 1. Purpose

The reference application must answer whether a non-trivial Toka program can
be understood, changed, built repeatedly, failed, recovered, and maintained
without weakening ownership, PAL, effects, async, or same-version TKI rules.

It deliberately exercises:

- multi-module compilation and source-less same-version interface replay;
- owned containers, iterators, closures, generic code, and explicit transfer;
- typed errors, cleanup, filesystem and process boundaries;
- dependency graphs, persistent manifests, cache invalidation, and recovery;
- deterministic plans, diagnostics, and repeated incremental execution.

The reference application remains intact when it exposes a compiler defect.
After the defect is fixed, a minimized fixture is added to the normal suite so
the application does not become an opaque collection of special cases.

## 2. Non-Goals

- No language feature is added merely to make the application shorter.
- No backend attribute or performance experiment is promoted by this work.
- Network services, package ecosystem breadth, self-hosting, and Windows
  parity are not part of this first reference application.
- Refactoring `lib/build.tk` is not itself evidence of usability. Evidence
  comes from sustained, stateful workloads and reviewable failure reports.

## 3. Work Ledger

Only `Pending`, `InProgress`, `Blocked`, `Complete`, and `Deferred` are used.

| Stage | Status | Deliverable | Exit evidence |
| --- | --- | --- | --- |
| `NB-0` | `Complete` | Freeze current behavior and establish module boundaries | The incremental suite passed before and after extracting `build/internal/support.tk` with unchanged public imports and plan results |
| `NB-1` | `Complete` | Convert the orchestrator into a maintained multi-module application | The facade, project declarations, codec, and support modules compile independently; public imports are unchanged and source-less facade replay passes |
| `NB-2` | `Complete` | Build a deterministic 30-100 module qualification workspace | The generated 31-module dependency tree covers clean/no-op builds, body changes, graph add/remove/cycles, missing output, version mismatch, compile failure, and recovery |
| `NB-3` | `Complete` | Add a fixed-seed sustained mutation runner | Seed `100098` completed 100 cycles with 10 committed incremental builds, 90 clean restores, native/Python plan equivalence, and equal incremental/clean runtime output |
| `NB-4` | `InProgress` | Run reliability and usability qualification | Local macOS qualification and deterministic repeat pass; the dedicated Linux/macOS workflow must produce the second platform-family report |

Each stage is independently reviewable. A stage is complete only after its
evidence is committed with it.

## 4. Intended Module Boundaries

The exact split follows dependencies found during implementation. The target
shape is deliberately small:

- support: path normalization, hashing, shell quoting, command capture;
- model: manifest, module snapshot, outputs, rebuild plan;
- codec: deterministic manifest and plan JSON;
- graph: dirty propagation and rebuild planning;
- project: `Executable` and `Library` declarations;
- executor: compiler, C/C++ and linker orchestration;
- facade: the existing public imports and `run_build` overloads.

The first extraction is the support layer because it does not relocate public
types or alter build decisions. Later splits must preserve the facade unless a
separate compatibility decision is approved.

The implemented split keeps `lib/build.tk` as the facade and executor,
re-exports `Executable` and `Library` from `lib/build/project.tk`, and places
reflection decoding and platform support in `lib/build/internal`. Exact
`pub(path)` grants preserve private fields across those implementation modules.

## 5. Sustained Scenarios

The qualification workspace must cover at least these transitions:

1. clean build followed by a no-op rebuild;
2. private implementation change with an unchanged public interface;
3. public interface change and dependent rebuild propagation;
4. module and dependency addition, removal, and cycle introduction;
5. missing output, missing source, malformed interface, stale interface, and
   compiler/interface version mismatch;
6. compile failure followed by source repair and successful recovery;
7. clean-build output compared with incremental-build output after each
   mutation;
8. repeated execution from source and from valid same-version `.tki` state.

Generated reports must record the schema version, seed, module and cycle
counts, mutation counts, and every required stage result. The CI artifact binds
the deterministic report to a compiler revision and platform.

The current deterministic report intentionally omits timing and physical work
paths. It records the fixed seed, module/cycle counts, committed/restored
mutations, planner equivalence, recovery scenarios, and clean-build runtime
equivalence. Revision and platform identity remain properties of the uploaded
workflow artifact rather than bytes inside the replayable report.

## 6. Qualification Findings

The first sustained run found and closed two native build defects:

- build mode ignored the parsed `-m/--manifest` path while plan mode honored
  it; both paths now use the same manifest;
- raw POSIX `system()` wait status escaped from compile/link failures, so child
  exit 1 could become build-executable exit 0 after truncation; failure paths
  now return a stable nonzero status.

The process-boundary follow-up replaced the builder's shell command assembly
and shared stdout capture file with structured argv, normalized status, and
independent stdout/stderr capture. The focused process fixture and the full
incremental suite pass with the new boundary. The remaining shell-oriented
tool callers are classified in `process_command_boundary.md` and are not
silently included in this builder qualification.

The workload also confirmed that module dependency cycles are currently legal
when declarations resolve. Qualification therefore requires deterministic
cycle handling and recovery, not a new rejection rule.

The final full-suite audit also exposed a pre-existing trusted-evidence
collision: on 64-bit targets, `usize` and `u64` methods can share a codegen
name while retaining different source summaries. Evidence generation now
omits only the ambiguous symbol instead of selecting a summary or failing the
entire interface build. TKI round-trip and trusted-evidence gates cover the
conservative fallback.

No PAL, ownership, effects, async, visibility, or TKI language rule changed.
The split uses the frozen `pub import` and `pub(path)` contracts.

## 7. Finding Classification

- Compiler crash, verifier failure, miscompile, resource safety failure, or
  source/TKI semantic divergence: `FZ-5` blocker.
- Language or ownership semantic ambiguity: `Blocked` pending owner decision;
  implementation must not choose a new rule silently.
- Determinism or stale-cache acceptance defect: `FZ-5` blocker.
- Missing library operation or awkward but correct expression: record as an
  ergonomics finding; address within frozen semantics or defer to 1.x.
- Performance: record measurements; block 1.0 only when behavior is
  pathologically slow or prevents the qualification workload from completing.

## 8. Stop Conditions

This direction stops when all `NB-*` stages are complete and two consecutive
clean runs of the fixed workload, on different supported platform families,
produce passing deterministic reports. Further workload growth then requires
either a newly observed defect class or a separate 1.x goal.

The work also stops immediately for owner review if progress requires new
syntax, a changed ownership/PAL/effects rule, or a broader 1.0 public surface.

## 9. Commands And Evidence

- `tools/scripts/test_native_build_reference.sh`: same-version source-less
  replay of the multi-module facade and project API.
- `tools/scripts/qualify_native_build.py`: deterministic 31-module sustained
  workload; default is 100 fixed-seed cycles.
- `tools/scripts/test_incremental_build.sh`: existing incremental contract plus
  a three-cycle reference-application smoke.
- `.github/workflows/native-build-reference.yml`: manual 100-cycle Linux x64
  and macOS arm64 qualification with uploaded JSON evidence.

Local evidence on the implementation revision: the source-less replay passed,
the incremental suite including reference smoke passed, two equal fixed-input
short runs produced byte-identical reports, and the full 100-cycle report passed.
The fixed-seed 82-case parser/Sema/interface reliability audit also passed.
