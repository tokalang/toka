# Toka 0.18.1 Release Notes

Toka 0.18.1 is a patch release on the `0.18` line resolving the false rejection of `str`-valued `match` expressions (`E04661` on local binding / initializer ascription and `E04658` on direct return) while strictly preserving borrow-escape and ownership-transfer safety across all compilation modes (`--check-only`, `-c`, `--emit-llvm`) and cross-module `.tki` interfaces.

（Toka 0.18.1 是 `0.18` 系列的补丁发布版本，专项修复产值型 `match` 表达式产生 `str` 视图时的假阳性误拒缺陷（绑定及显式类型归属路径下的 `E04661` 与直接返回路径下的 `E04658`），同时在全部三种编译模式（`--check-only`、`-c`、`--emit-llvm`）与无源码 `.tki` 跨模块重放中严格保持借用逃逸与所有权转移安全边界。）

## Key Changes

### 1. Complete Provenance & Borrow Transfer Facts for `str`-Valued `match` Expressions
- **Arm Scope Borrow Resolution (`checkMatchExpr`)**: Resolves arm-body `pass <expr>` borrow dependencies and provenance inside each arm's lexical scope before popping the arm scope, rewriting pattern-alias dependencies (`auto Enum::Variant(&field)` / `auto &alias`) back to the underlying match target root symbol.
- **Arm-Local Escape Detection**: Detects and rejects arm-local owner borrow escapes (`E0456` / `E0455`) and `match cede <target>` consumed-payload view escapes before arm scope teardown, preventing shadowed outer names from masking local escapes.
- **Multi-Owner Union & Complete Transfer Facts**: Aggregates proven borrow sources across all value-producing arms (`BorrowSourceKind::LocalSymbol` and `BorrowSourceKind::StaticStorage`), records multi-root borrow dependencies on `MatchExpr` (`MatchResolvedBorrowRoots`), and propagates them through local variable bindings, assignments, return statements, and `resolveNonCallValueTransferFacts` without false `IncompleteFacts` rejections.
- **Fail-Closed Conservation**: Preserves `IncompleteFacts` fail-closed rejection (`E04661` / `E04658`) whenever any value-producing arm has unproven/unknown provenance (such as unannotated source-hidden `.tki` functions) or when `cede` is applied to a borrowed `str` result.

### 2. Verification & 4-Platform CI Gate Wiring
- **26 Baseline Evaluation Probes**: All 26 probes in the immutable `valuelease-v018-20261019` evaluation set (`P01`–`P18` excluding history-only `P13`, plus `N01`–`N09`), including all 5 previously false-rejected `str` `match` expression probes (`P05`, `P12`, `P14`, `P16`, `P17`), pass 100% across `--check-only`, `-c`, `--emit-llvm`, and native execution.
- **Positive & Negative Regression Matrix (`P-A`..`P-F`, `N-A`..`N-F`)**: Registered as CTest suite `toka_str_match_expr_0181` (`tools/scripts/test_str_match_expr_0181.py`) and wired into `.github/workflows/ci.yml` (`linux-x64`, `linux-arm64`, `macos-arm64`) and `.github/workflows/windows-dogfood.yml` (`windows-x64`).

## Release & Platform Policy
- **Core Blocking Platforms (Tier 1)**: `linux-x64`, `linux-arm64`, and `macos-arm64`.
- **Continuous Dogfood Target (Tier 2)**: `windows-x64` (MSYS2 / MinGW-w64 Clang SDK path).
- **Qualification Contract**: Conforms to `toka.release-platforms.0.18.v1` with full 13-stage Release Gate verification and 142/142 CTest suites passing.
