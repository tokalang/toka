# Toka Compiler Technical Debt & TODOs

This document tracks unresolved core issues, known bugs, and technical debt that require deep compiler architecture refactoring.

## High Priority Compiler Architecture Issues (Discovered during Stdlib Hardening)

### 1. PAL Precision and Local Control-Flow Audit
**Status**: Partially resolved / precision audit in progress (Sema Phase)
**Severity**: Medium
**Description**:
The PAL checker is no longer only a linear environment tracer. The current Sema
pipeline snapshots and merges `InitMask`, `Moved`, and `PALChecker` state for
`if`, `guard`, `match`, `loop`, `for`, `break`, and `continue`. The original
mutually-exclusive-branch false-positive class has representative coverage.

The remaining work is a precision audit, not a redesign of the 1.0 safety
contract. The checker should keep rejecting hard-to-prove cases, but it needs
direct tests for higher-order local-control combinations such as labeled
`break` / `continue`, nested loop exits, branch-carried borrowed fields, and
async capture / suspension boundaries. Ordinary `fn` closure escape with
implicit borrow captures is now rejected through the same lifetime-dependency
return path used for other borrow-like values.
**Impact**:
Without this audit, PAL may remain correct but overly conservative or may have
unlocked control-flow paths that are not directly regression-tested.
**Proposed Fix**:
Keep the local `AnalysisState` model and extend the test matrix around
remaining PAL edge combinations. Only introduce a fuller CFG representation if
the existing state merge model cannot express a concrete safe program that
Toka 1.0 should accept.

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
