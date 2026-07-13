# Process And Command Boundary Audit

Status: `Complete` for the Toka-native build reference application

This audit was triggered by the sustained native build workload. It hardens
the library and runtime boundary needed by that application without adding
language syntax or changing PAL, ownership, effects, async, or TKI rules.

## Contract

`std/process::Command` passes a program and each argument directly to the
operating system. It never interprets arguments as shell source. Embedded NUL
bytes are rejected before execution.

The structured operations are:

- `try_spawn()` returns either an owned `Child` or a `ProcessError`;
- `wait_status()` consumes the wait right exactly once and reports normal or
  signal termination;
- `try_status()` reports spawn/wait errors separately from child failure;
- `output()` captures stdout and stderr independently and drains both streams
  concurrently;
- `ExitStatus` exposes `code`, `signal`, `success()`, and a compatibility
  `legacy_code()`.

The old `spawn()`, `status()`, and `wait_exit()` entry points remain as
compatibility adapters. They return `-1` for boundary failure and normalized
child status otherwise.

`Child` drop remains non-blocking. Callers that spawn must explicitly wait;
automatic waiting would introduce hidden blocking into ownership cleanup.

Linux and macOS implement spawn, wait, status, and captured output. Windows,
which is not a Toka 1.0 release-blocking platform, currently implements
synchronous status only. Unsupported operations return `ProcessError` rather
than pretending to succeed.

## Native Builder Migration

`lib/build.tk` now uses structured commands for dependency installation,
Forge invocation, compiler planning, the Python incremental driver, C/C++
compilation, archive creation, and linking. Directory creation and object
cleanup use filesystem operations.

Consequences:

- child exit status is no longer exposed as raw POSIX `system()` wait bits;
- paths and arguments containing spaces or shell metacharacters stay literal;
- dependency-plan output no longer uses the shared
  `.toka/build/tmp_stdout.txt` file;
- concurrent planner invocations cannot overwrite one another's capture file;
- stdout and stderr remain distinguishable when dependency dumping fails.

The existing textual `TOKA_PKG_ARGS` and `ldflags` inputs retain their current
whitespace-separated token model. This audit does not redesign the public
project configuration surface.

## Evidence

`tests/pass/g10_process_command.tk` covers literal and empty argv entries,
separate stdout/stderr, ordinary and signal termination, spawn failure,
repeated wait rejection, embedded NUL rejection, and simultaneous 30 KB output
on both captured streams.

The native build facade source-less replay passes after importing the new
process contract. The incremental suite passes all planner comparisons and
the 31-module reference smoke, including compile failure recovery and equal
incremental/clean output.

## Remaining Inventory

The following shell-oriented callers are outside the native builder migration
and remain visible audit items:

- `tools/toka/src/main.tk`: user-facing run/test/doc/install/clean commands;
- `tools/toka/src/pkg_manager.tk`: Git, curl, tar, and publication commands;
- `tools/forge/src/scheduler.tk`: execution of scheduler command strings;
- `tools/tokalsp/main.tk`: compiler output capture through `popen`.

They should migrate independently because some currently accept command
strings as part of their own contract. Replacing those strings may require a
tool API decision, while the native builder could be migrated without changing
its public language or project model. Their existence does not weaken the
structured `std/process` contract, but they remain hardening work before Toka's
toolchain can claim a completely shell-free command boundary.

## Stop Decision

This audit stops once the runtime API, focused fixture, native builder
migration, source/source-less compilation, and sustained incremental workload
all pass. Further migration begins only against one of the remaining callers
above; it is not an open-ended reason to delay unrelated 1.0 work.
