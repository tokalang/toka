# FZ-4 Public Contract Freeze

Status: `Complete`

`FZ-4` aligns the public 1.0 specification, freeze decisions, diagnostics,
platform positioning, and minimum runtime contract. It introduces no syntax,
does not reinterpret an accepted program, and does not establish a stable
binary or cross-version interface ABI.

## Source Semantics And Physical ABI

Ordinary parameters retain PAL's logical in-place capture semantics. This is
now explicitly separated from target lowering: registers, pointers, aggregate
storage, closure representations, and return conventions are physical compiler
choices and cannot create an implicit source copy or change a borrow rule.

`tests/pass/g05_logical_capture_abi_boundary.tk` executes mutable scalar and
shape calls in the same fixture. Both mutate the caller-visible payload despite
their different likely machine representations. Generated layout, calling
conventions, `.tki`, and build-cache formats remain compiler-, target-, and
format-version-bound.

## Stable Exclusions And Diagnostics

Two public diagnostics used temporary wording even though the closure ledger
already classified both constructs as Post1.0:

- `E04547` now states that String/str format specifiers are outside Toka 1.0
  and directs users to plain `{}`;
- `E0744` now states that global destructuring is outside Toka 1.0 and directs
  users to bind one global value and destructure it inside a function.

Their codes and rule identities are unchanged. Focused negative tests lock both
codes. A diagnostic-code uniqueness audit found no duplicate published code in
`DiagnosticDefs.def`. The dyn-trait specification was also changed from
temporary wording to an explicit 1.0 object-safety boundary without changing
compiler behavior.

## Compatibility And Runtime Boundary

The English and Chinese specifications now state the same 1.x policy:

- frozen 1.0 source semantics remain stable through 1.x;
- additive features and relaxed conservative rejections may be compatible;
- safety and miscompile fixes may reject source that depended on unsound
  behavior and must be recorded as safety fixes;
- diagnostic wording may improve, but a published code is not reused for a
  different rule;
- `.tki`, cache data, generated layout, and binary ABI do not promise
  cross-version compatibility.

The minimum core runtime contract is also explicit. Every live owned value is
cleaned exactly once on normal exits; move and `cede` transfer that obligation.
`panic` is non-returning process termination, not a catchable exception or a
stack-unwinding guarantee. Cleanup after a panic point is not promised by 1.0.

## Platform Positioning

The README now matches the release decision: Linux and macOS are supported 1.0
release platforms. Windows/MSYS2, WSL2, and WASI may remain available or
experimental and do not block 1.0. Windows parity and self-hosting are later
work rather than current language-freeze requirements.

## Verification Snapshot

Local platform: macOS arm64.

- compiler build: passed;
- positive suite: 318 passed, 0 failed;
- negative suite: 237 passed, 0 failed;
- warning suite: 1 passed, 0 failed;
- semantic source/source-less replay: 11 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- TKI metadata/cache, path behavior, unsafe revalidation, excluded syntax,
  semantic evidence, trusted memory evidence, and incremental build: passed;
- diagnostic-code uniqueness audit: passed;
- ambiguous `not yet supported` diagnostic-text scan: no remaining compiler
  diagnostic wording;
- diff whitespace validation: passed.

## Decision

`FZ-4` is complete. A user can distinguish frozen source semantics from target
ABI choices, understand the minimum normal-cleanup and panic boundary, rely on
stable diagnostic identities, and see the supported-platform policy without
reading compiler implementation. No FZ-4 language-design question remains.

The separate `FZ-3-P01` supported-platform execution blocker remains open and
must pass before 1.0 can freeze. `FZ-5` may now build the unified release gate
and RC evidence path without changing any language contract.

Milestone commit subject: `docs: freeze Toka 1.0 public contracts`.

## Authorized Iterator Addendum

After the original RC, the user explicitly reopened the 1.0 surface for the
synchronous iterator closure. `syntax.md`, `syntax_zh.md`, the freeze decision
list, and the semantic rule matrix now freeze `@Iterable`, `@Iterator`, and
`@BorrowIterator` consistently. Focused diagnostics, PAL lifetime tests,
hidden-cursor drop execution, and source/source-less replay are recorded in
`iterator_protocol_closure.md`. Local verification is 320/320 pass, 242/242
fail, 1/1 warn, and 12/12 replay, so FZ-4 remains complete; FZ-5 is reopened
only for replacement supported-platform evidence.

## Authorized Callable Addendum

The user subsequently authorized the callable development-experience closure.
Toka now freezes one `@Callable` protocol whose permission is expressed by
`self`, `self#`, or `cede self`, with corresponding `fn`, `fn#`, and `cede fn`
types. Capture ownership remains separate from invocation permission. The
implementation, diagnostics, exact-drop behavior, generic and iterator
composition, thread callback migration, and source-less replay are recorded in
`callable_protocol_closure.md`. Local verification is 322/322 pass, 246/246
fail, 1/1 warn, and 13/13 replay. This addendum keeps FZ-4 complete and leaves
FZ-5 `InProgress`; it intentionally creates no new RC or tag.

## Authorized Error Propagation Addendum

The user subsequently authorized the typed error-propagation closure. Postfix
`!` now has a frozen consuming and cleanup contract. Different error types use
one explicit `@ErrorInto<Target>` implementation instead of ordinary type
compatibility or raw union copying, and parameterized trait bounds make the
contract usable in generic functions. `ErrorContext<E>` preserves typed source
errors; synchronous, async, generic, diagnostic, and source-less evidence is
recorded in `error_propagation_closure.md`. `dyn error`, conversion chains, and
`main -> Result` remain outside 1.0. This addendum keeps FZ-4 complete and
requires the eventual FZ-5 replacement revision to include this change. Local
verification is 324/324 pass, 251/251 fail, 1/1 warn, 14/14 replay, and 12/12
semantic cache invalidation.
