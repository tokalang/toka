# Toka 0.15.1 Release Notes

Toka 0.15.1 is a patch release addressing the TaskHandle lifetime escape defect across all escape channels in semantic analysis.

## Key Changes

### 1. TaskHandle Lifetime Escape Defect Resolution (THFIX-R1 .. THFIX-R7)
- **Recursive AST Proof for Task Consumers (THFIX-R1)**: Replaced unconditional sync function bypass (`EffectKind::None`) with recursive AST safety proof (`isSafeSynchronousTaskConsumer`). Verifies that consumed tasks are not started (`.start`), converted to background detached handles (`coro_handle`), stored into containers/receivers/globals, or returned. Strictly eliminates all function name and module name whitelists.
- **Aggregate Lifetime Dependency Propagation (THFIX-R2)**: Propagates task lifetime dependencies through both named and temporary record/struct expressions (`InitStructExpr`, `AnonymousRecordExpr`). Prevents aggregate-wrapped task escapes regardless of whether bound to a local variable or passed directly as an anonymous temporary.
- **Control Flow Recursion & Enum Constructors (THFIX-R4)**: Recursively analyzes control flow structures (`IfExpr`, `LoopExpr`, `ForExpr`, `GuardExpr`, `BlockStmt`) for safety, and correctly resolves enum constructor payload arguments (`Option::Some(cede t)`) without false positive rejection of owned tasks.
- **Match Arm Isolation & Fail-Closed Unknown Nodes (THFIX-R5)**: Enforces escape prevention inside match arms and implements strict fail-closed handling for any unknown or uninspected AST node types.
- **Recursive Sub-Expression Safety & Ancestor Scope State Recovery (THFIX-R6)**: Enforces recursive safety verification across sub-expressions (match target, guard-bind target, destructuring initializer, assignment RHS, nested call arguments) with tail fail-closed semantics, and supports full ancestor scope state restoration across match arms.
- **Assignment LHS Tracking Rejection (THFIX-R7)**: Forbids assignment LHS expressions referencing tracked tasks (rejecting detach in index expressions and direct clearing of coroutine handles) and removes unconditional `return true` in binary assignments.

### 2. Regression Gate & CI Wiring
- Regression probe suite in `tests/semantics/task_lifetime_escape/` with 28 negative and positive control probes.
- Registered as CTest gate `toka_task_lifetime_escape` wired into GitHub Actions core PR gates (Linux x64, Linux ARM64, macOS ARM64) and Windows Dogfood gate with Windows executable path compatibility.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Continuous Dogfood**: Windows validation is maintained continuously and independently.
- **Qualification Contract**: Fully conforms to `toka.release-platforms.0.15.v1` release policy with 13-stage Release Gate verification.
