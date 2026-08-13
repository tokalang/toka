# 0.x Exploration and Release Qualification Policy

**Status:** Active development policy. This document changes no source syntax,
runtime ABI, TKI schema, package format, or 1.0 commitment.

## Purpose

Toka keeps its ownership, PAL, cleanup, replay, and diagnostic discipline. In
0.x, however, an experiment must not become a public compatibility obligation
merely because its first implementation is useful. This policy separates rapid
design feedback from release qualification while keeping `main` continuously
green.

The platform and release gates remain those in
[`1_0_scope.md`](1_0_scope.md). This document decides when a change needs
which evidence; it does not lower any existing release requirement.

## Two work modes

| Mode | Purpose | Required evidence | Not required by default |
|---|---|---|---|
| **0.x experiment** | Test, revise, or remove an internal semantic/runtime design against a concrete program. | Compiler build; affected positive and negative regressions; one named dogfood scenario; `main` remains green. | New public syntax, stable TKI/ABI, source-less replay outside the affected interface, or the RC gate. |
| **Committed contract** | Make a source, interface, package, SDK, or release promise that users may depend on. | Feature RFC status, affected source/TKI/cache gates, diagnostics, and compatibility evidence. | The complete RC matrix unless the change is a release candidate. |
| **Release candidate** | Publish a platform-qualified SDK/package release. | The full multi-platform release gate and its recorded evidence. | Nothing in this policy waives an RC requirement. |

An experiment must say which mode it is in. A compiler/runtime-only helper may
remain private while it is evaluated; it is not a public ABI simply because the
compiler calls it. A change to an exported declaration, TKI meaning, package
format, or documented default behavior is a committed-contract change and must
run the relevant interface evidence.

## Mainline rule

`main` may not carry known failing tests. Fast feedback means selecting the
smallest gate that covers a change, not accepting regressions or keeping a
permanent failure budget.

For an ordinary exploration slice, the author records:

1. the concrete program and the observed pain before the change;
2. the intended source or runtime improvement and one before/after example;
3. focused pass/fail or runtime regressions that fail without the change; and
4. an exit decision: promote, revise, or remove the experiment.

There is no minimum line-count or application-size threshold. A small compiler,
embedded, or build-tool case can be decisive; an artificial large example is
not evidence. Conversely, a test-only convenience with no identifiable user
or product benefit must not acquire default language surface merely because it
is easy to implement.

## Dogfood veto set

The following are active product users, not just qualification artifacts:

| Dogfood user | What it can veto |
|---|---|
| `toka build` and the native builder | project creation/build, package resolution, incremental and source-less workflows |
| [`toka-examples/service-kit`](https://github.com/tokalang/toka-examples/tree/main/service-kit) | cancellation, cleanup, network I/O, and service shutdown behavior |
| `demos/gui-settings` | host-event integration, application packaging, and ordinary GUI ownership flow |
| Native Windows/MSYS2 project build | platform/toolchain installation and generated native executable behavior |

A proposed language or runtime change that cannot remove a specific pain in one
of these programs is normally lower priority than a demonstrated product
blocker. The set is deliberately small; adding a dogfood user requires a
reproducible command and a maintainer-owned scenario, not another synthetic
qualification suite.

The Windows/MSYS2 row is measured by the scheduled/manual
[`Native Windows Dogfood`](../.github/workflows/windows-dogfood.yml) workflow.
It builds the SDK and exercises installed `doctor`, `new`, compile, and `run`
flows. Its result is product feedback, not a new release-blocking platform
promise; promotion remains an explicit `1_0_scope.md` decision.

The compiler repository's normal PR gate runs the offline GUI settings build on
macOS. The independently maintained service-kit workflow runs its Linux
qualification from locked package releases. These are focused product checks,
not a replacement for the RC gate or an instruction to run every qualification
locally.

## Escalation and deletion

An experiment is promoted only after its benefit survives the named dogfood
scenario and its public boundary is deliberately specified. If the scenario
does not justify the complexity, the implementation is removed or retained as
an explicitly non-default experiment. It must not be preserved as a half-public
protocol in anticipation of a later feature.

Async TCB closure, semantic manifests, and similar long-horizon tracks remain
important qualification work. They advance when a recorded baseline invariant
is violated or when a separately accepted product capability needs them; they
are not a standing reason to delay unrelated 0.x language or tooling work.
