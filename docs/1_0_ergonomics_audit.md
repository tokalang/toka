# Toka 1.0 Ergonomics Audit

Status: `InProgress`

This audit closes source-level friction that makes ordinary Toka programs
needlessly expose compiler mechanics. It is an `FZ-5` release-quality task,
not a license to weaken PAL, ownership, effects, or explicit transfer rules.

## 1. Acceptance Principle

The shortest unsurprising spelling should compile when all of the following
hold:

- the compiler already has one unambiguous expected type or view;
- the operation requires no allocation, clone, ownership transfer, or lossy
  conversion;
- accepting the spelling does not change the meaning of an already accepted
  program; and
- the same rule can be explained, tested, and replayed across module paths.

Consequently, owned text may be compared with a text literal through its
read-only `@delegate` view, and an integer literal in a `u64` parameter context
does not repeat `:u64`. Toka must not silently allocate a `string`, clone a
resource, insert `cede`, or guess between multiple viable conversions merely
to shorten source.

## 2. Work Ledger

Only `Pending`, `InProgress`, `Blocked`, `Complete`, and `Deferred` are valid
states.

| ID | Status | Area | Required evidence |
| --- | --- | --- | --- |
| `ERG-1` | `Complete` | Equality across the zero-cost owned text view | `string == str` and `str == string`, positive runtime coverage, no allocation or consumption, QSLite CLI and vertical use |
| `ERG-2` | `Complete` | Contextual numeric literals | Calls, methods, constructors, returns, assignments, comparisons, negative literals, overflow rejection, and stable diagnostics |
| `ERG-3` | `InProgress` | Text and byte API friction | Audit avoidable `as_str()`, `string::from()`, and temporary bindings in the native builder and QSLite; classify every retained conversion |
| `ERG-4` | `Pending` | Ownership-facing expression composition | Direct member chains, container resource clone, `cede` capture, Result/Option propagation, and cleanup must work without hiding transfer |
| `ERG-5` | `Pending` | Iterator, closure, and async composition | Real algorithms must accept natural closures and iterator values without adapter noise or lost dependency facts |
| `ERG-6` | `Pending` | Diagnostic ergonomics | Rejections identify the ownership, type, effect, or ambiguity that requires explicit source and show a viable spelling where one exists |
| `ERG-7` | `InProgress` | Real-program normalization | Native builder and QSLite contain no known avoidable type repetition, view conversion, allocation-only comparison, or workaround temporary |

An implementation gap may be fixed directly when it follows a frozen rule.
Any new implicit allocation, ownership action, conversion choice, or change to
an accepted program's meaning is `Blocked` until the project owner decides it.

## 3. Explicit-Type Classification

The audit does not seek to remove every literal suffix. Each occurrence is
classified as one of:

- `RequiredIntent`: no unique expected type exists, as in a standalone inferred
  binding or an ABI/bit-width constant.
- `Boundary`: the spelling deliberately documents an FFI, layout, overflow, or
  narrowing boundary.
- `LegacyNoise`: the compiler already infers the type; remove it from public
  code and examples.
- `CompilerGap`: the expected type is unique but inference is inconsistent;
  add a focused test and repair the compiler.

The same classification applies to explicit text/view conversions and
workaround temporaries.

## 4. Current Evidence

The first slice closes `ERG-1` and establishes the audit mechanism:

- `string == str` and `str == string` normalize to the core `as_str()`
  read-only projection; arbitrary user `@delegate` implementations remain
  ineligible for implicit equality.
- Contextual integer calls and comparisons accept unsuffixed positive and
  negative literals across the core integer widths. Methods, shape fields,
  returns, and assignments use the same rule; `E04598` rejects positive,
  negative, signed, and unsigned overflow instead of truncating.
- QSLite uses direct command/header/result comparison and removes redundant
  numeric suffixes where an operand or parameter already fixes the type.
- The native builder uses direct text equality for CLI, platform, manifest,
  and cache facts.
- Focused execution passes, the complete positive corpus is 328/328 after
  running its two local-socket cases outside the restricted sandbox, and the
  negative corpus is 254/254. Warn is 1/1 and semantic replay is 14/14.
- QSLite passes 100 sustained operations, 10 corruption classes, its vertical
  test, and source-less/incremental/package replay. The native builder passes
  source-less facade/process replay and a 30-module, 20-cycle qualification.

## 5. Verification

Each completed item must include focused positive execution. Where rejection
is part of the boundary, it also needs a negative fixture with a stable error
code. Cross-module behavior must be exercised through source-less `.tki` when
the rule depends on an imported signature, trait, delegate, or dependency
fact.

The native incremental builder and QSLite remain the sustained acceptance
programs. Micro-fixtures explain a rule; they cannot by themselves close an
ergonomics item.

## 6. Stop Conditions

This audit stops when:

- `ERG-1` through `ERG-7` are `Complete` or explicitly `Deferred`;
- every suspicious suffix, view conversion, allocation-only comparison, and
  workaround temporary in the two reference programs is classified;
- both programs pass sustained runtime, source-less TKI, incremental, and
  package paths using their normalized source;
- no accepted convenience performs a hidden allocation, clone, move, `cede`,
  or unsafe conversion;
- the full pass/fail/warn and semantic replay suites pass; and
- remaining requests are preference-level shorthand or require new language
  design rather than repair of an obvious inconsistency.

After these conditions hold, further convenience work is post-1.0 unless it
fixes a correctness issue or a newly demonstrated blocker in a sustained
program.
