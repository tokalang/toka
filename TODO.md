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
closure / async capture boundaries.
**Impact**:
Without this audit, PAL may remain correct but overly conservative or may have
unlocked control-flow paths that are not directly regression-tested.
**Proposed Fix**:
Keep the local `AnalysisState` model and extend the test matrix around
remaining PAL edge combinations. Only introduce a fuller CFG representation if
the existing state merge model cannot express a concrete safe program that
Toka 1.0 should accept.

### 2. LLVM PhysReg Copy CodeGen Crash
**Status**: Unresolved / Mitigated via SOA (CodeGen Phase)
**Severity**: Critical
**Description**: 
When instantiating complex or heavily-nested generic types (e.g., `Option<Entry<'K, 'V>>` or generic `Bucket` structs) and attempting to pass or return them by value, the LLVM CodeGen backend fatally crashes with `LLVM ERROR: Cannot emit physreg copy instruction`.
**Impact**: 
Toka currently fails to correctly map the ABI (Application Binary Interface) calling conventions for large multi-generic structures. Instead of implicitly elevating large structural returns to `byval` or pointer mechanisms (`alloca` / `sret`), it tries to map them to physical registers, causing LLVM assertion failures.
**Mitigation**:
Standard Library components (like `HashMap`) are currently circumventing this by adopting a Structure of Arrays (SOA) flat layout.
**Proposed Fix**:
Investigate `src/CodeGen_Type.cpp` and `src/CodeGen_Expr.cpp` to correctly lower complex product shapes to valid LLVM memory types and strictly enforce ABI parameter size attributes for returned compounds.


### 4. Windows Native Standard Library Support (lib/sys/windows.tk)
**Status**: Deprioritized / Strategic Shift to WSL2
**Severity**: Low (Community Contribution Recommended)
**Description**:
While the Toka compiler can be compiled on Windows (via MSYS2/MinGW), porting the POSIX-based standard library to native Win32 API is an immense effort with low immediate ROI.
**Strategic Decision**:
Native Windows support is deprioritized. Windows developers are officially recommended to use Toka via **WSL2 (Windows Subsystem for Linux)**, which offers native POSIX compatibility, matching the experience of Node, Go, and Rust ecosystems.
**Proposed Fix**:
Update official documentation to clearly state WSL2 as the primary supported method for Windows users. Maintain MSYS2 compilation purely for cross-compilation toolchains if necessary.
