# Toka Compiler Technical Debt & TODOs

This document tracks unresolved core issues, known bugs, and technical debt that require deep compiler architecture refactoring.

## High Priority Compiler Architecture Issues (Discovered during Stdlib Hardening)

### 1. PAL Precision and Local Control-Flow Audit
**Status**: Resolved for PAL 1.0 core / post-1.0 precision watchpoints remain (Sema Phase)
**Severity**: Medium
**Description**:
The PAL checker is no longer only a linear environment tracer. The current Sema
pipeline snapshots and merges `InitMask`, `Moved`, and `PALChecker` state for
`if`, `guard`, `match`, `loop`, `for`, `break`, and `continue`. The original
mutually-exclusive-branch false-positive class has representative coverage.

The PAL 1.0 core is frozen as a local, path-anchored checker. Function calls are
checked as simultaneous temporary borrow groups; payload arguments are implicit
PAL borrows, not invisible value copies. PAL also distinguishes ordinary
payload writes from exclusive mutations and invalidating transfers, so a shared
borrow protects validity without becoming a global freeze promise.

Remaining work is precision and feature-consumer coverage, not a redesign of
the 1.0 safety contract. The checker should keep rejecting hard-to-prove cases.
`FZ-1` now locks the existing async suspension boundary with dedicated local
borrow, move, init, branch, loop, `break`, `continue`, and source/TKI dependency
tests. Future audits can still add higher-order local-control combinations,
new task/thread consumers, and richer diagnostics. Ordinary `fn` closure escape
with implicit borrow captures is rejected through the same lifetime-dependency
return path used for other borrow-like values.
**Impact**:
PAL 1.0 now has regression coverage for its core safety contract. Remaining
gaps may make the language conservative or leave newer consumers under-tested,
but they are no longer known holes in the frozen single-thread safe subset.
**Proposed Fix**:
Keep the local `AnalysisState` model. Post-1.0 work should extend tests around
new PAL consumers and only introduce a fuller CFG representation if the
existing state merge model cannot express a concrete safe program that Toka
should accept.

### 2. Large Generic Aggregate ABI Lowering
**Status**: Resolved / regression-covered (CodeGen Phase)
**Severity**: Low
**Description**: 
Older compiler revisions could crash LLVM with `LLVM ERROR: Cannot emit physreg
copy instruction` when complex or heavily-nested generic aggregates (for
example `Option<Entry<'K, 'V>>` or generic bucket-style structs) were passed or
returned by value.
**Impact**: 
The current CodeGen path lowers large structural returns through `sret` and
passes captured aggregate parameters through memory/pointer-backed ABI paths
instead of forcing them into physical registers.
**Regression Coverage**:
`tests/pass/g07_sret_generic.tk`, `tests/pass/g08_sret_closure.tk`,
`tests/pass/g08_sret_option_entry.tk`, and the HashMap iterator tests cover
nested generic structural returns, dyn-call structural returns,
`Option<Entry<'K, 'V>>`, large tagged payloads, and aggregate argument
forwarding.
**Remaining Watchpoint**:
If future call paths are added, especially new async/dyn/extern lowering paths,
they must reuse the same aggregate ABI classification rather than constructing
raw LLVM call signatures independently.

### 4. Windows Native Standard Library Support (lib/sys/windows.tk)
**Status**: Deprioritized / Strategic Shift to WSL2
**Severity**: Low (Community Contribution Recommended)
**Description**:
While the Toka compiler can be compiled on Windows (via MSYS2/MinGW), porting the POSIX-based standard library to native Win32 API is an immense effort with low immediate ROI.
**Strategic Decision**:
Native Windows support is deprioritized. Windows developers are officially recommended to use Toka via **WSL2 (Windows Subsystem for Linux)**, which offers native POSIX compatibility, matching the experience of Node, Go, and Rust ecosystems.
**Proposed Fix**:
Update official documentation to clearly state WSL2 as the primary supported method for Windows users. Maintain MSYS2 compilation purely for cross-compilation toolchains if necessary.
