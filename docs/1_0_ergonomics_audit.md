# Toka 1.0 Ergonomics Audit

Status: `Complete`

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
read-only `@Delegate` view, and an integer literal in a `u64` parameter context
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
| `ERG-3` | `Complete` | Text and byte API friction | Audit avoidable `as_str()`, `string::from()`, and temporary bindings in the native builder and QSLite; classify every retained conversion |
| `ERG-4` | `Complete` | Ownership-facing expression composition | Direct member chains, container resource clone, `cede` capture, Result/Option propagation, and cleanup must work without hiding transfer |
| `ERG-5` | `Complete` | Iterator, closure, and async composition | Real algorithms must accept natural closures and iterator values without adapter noise or lost dependency facts |
| `ERG-6` | `Complete` | Diagnostic ergonomics | Rejections identify the ownership, type, effect, or ambiguity that requires explicit source and show a viable spelling where one exists |
| `ERG-7` | `Complete` | Real-program normalization | Native builder and QSLite contain no known avoidable type repetition, view conversion, allocation-only comparison, or workaround temporary |

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
  read-only projection; arbitrary user `@Delegate` implementations remain
  ineligible for implicit equality.
- Contextual integer calls and comparisons accept unsuffixed positive and
  negative literals across the core integer widths. Methods, shape fields,
  returns, and assignments use the same rule; `E04598` rejects positive,
  negative, signed, and unsigned overflow instead of truncating.
- QSLite uses direct command/header/result comparison and removes redundant
  numeric suffixes where an operand or parameter already fixes the type.
- The native builder uses direct text equality for CLI, platform, manifest,
  and cache facts.
- A `string` argument is projected to `str` when a function or method has that
  unique expected view. The projection calls the core `as_str()` contract and
  does not allocate, clone, consume, or insert `cede`.
- Explicit and automatic `as_str()` expressions now replay member dependencies
  through source and source-less `.tki` paths. A returned view keeps
  `owned.buf` borrowed, while methods returning non-borrowing values and
  completed `return` statements clear their temporary dependency facts.
- The two reference programs went from 51 explicit `.as_str()` calls to 6.
  QSLite retains none. The native builder retains six calls after
  `Vec<string>::get_ref()`, whose result is a pointer rather than an owned
  `string`. Compiler-synthesized projections mark their receiver as already
  checked, so consuming expressions such as `Result::unwrap()` are checked
  exactly once. The six retained calls are explicit raw-pointer boundaries,
  not view API requirements. The 1.0 decision keeps `*string -> str` explicit:
  raw pointers are outside PAL, so the compiler does not hide dereference,
  provenance, or nullability behind an automatic safe-view projection. These
  six calls are classified as `Boundary`, not unresolved ergonomics noise.
- The obsolete `implicit_deref_err` fixture was removed: it rejected the now
  frozen `string -> str` view and did not exercise resource copying. Actual
  resource-copy rejection remains covered by destructuring, spread, closure,
  and source-less resource fixtures; non-`cede` shape parameters remain
  observational and may accept a reference to the same soul.
- Ownership-facing expression composition is closed for the frozen surface:
  `Vec<Resource>::clone()` deep-clones elements, pointer-return member chains
  such as `rows.get_ref(0).key` lower correctly, `Result/Option::unwrap()` can
  feed a view parameter or direct member access, and `cede` closure capture,
  branch return, error propagation, async suspension, and frame cleanup each
  drop resources exactly once. None of these paths inserts a hidden clone,
  transfer, or `cede`.
- Iterator/callable/async composition is closed for the frozen eager protocol.
  Generic algorithms accept shared, exclusive, and consuming callables; cede
  capture remains separate from invocation mode, and inferred generic `cede`
  arguments are checked exactly once. An owned `Vec` and exclusive closure can
  cross `.start` through the existing two-sided cede contract, then iterate and
  invoke across `.await` without adapter values or lost frame state.
- Local closures with implicit captures now commit their dependency paths to
  PAL. A returned `fn` dependency is preserved by `effects: return <- source`,
  emitted in `.tki`, and replayed source-less, so moving the source while the
  callable is live is rejected consistently.
- ERG-5 did not add lazy adapters, consuming iteration, or async iteration.
  Those remain post-1.0 design work rather than hidden extensions of the eager
  iterator contract.
- Shape identity now follows the lexically resolved declaration rather than a
  process-global bare name. A local `Counter` and `std/vec`'s private `Counter`
  coexist in one program, and same-spelled public shapes from different modules
  remain distinct nominal types. `E04571` qualifies both module names when the
  unqualified spelling would be ambiguous. Source-backed and source-less `.tki`
  paths accept and reject the same struct and enum cases. Enum construction also
  lowers from the resolved declaration rather than reparsing a bare callee name.
- ASan exposed and closed one implementation defect in that lookup: a strong
  record alias in the lexical type namespace could be mistaken for a shape
  declaration. The lookup now validates declaration identity against the
  module's registered shapes. The former intermittent `g04_small.tk` crash is
  stable across 30 ASan and 50 normal repeated compilations.
- The unified gate exposed one remaining bare-name lowering path after semantic
  analysis: enum variant construction reparsed `Type::Variant` and ignored the
  resolved declaration. Duplicate `TokenKind` declarations in `tokafmt` made
  that path allocate a null LLVM type. Enum lowering now uses `ResolvedShape`;
  normal and ASan tool compilation pass, and the replay case includes distinct
  same-name enum variants.
- Focused execution passes, the complete positive corpus is 331/331 after
  running its two local-socket cases outside the restricted sandbox, and the
  negative corpus is 255/255. Warn is 1/1 and semantic replay is 17/17.
- QSLite passes 300 sustained operations, 313 process-level reopens, 10
  corruption classes, deterministic bytes, and source-less/incremental/package
  replay. The native builder passes source-less facade/process replay and a
  31-module, 20-cycle qualification.
- Two consecutive clean qualifications produced the same QSLite database and
  lock summaries and passed two native-builder 31-module, 20-cycle runs without
  a new workaround. This satisfies the bounded-audit stop rule.
- A final local macOS arm64 unified gate passes all 12 stages: 331 positive, 255
  negative, one warning fixture, 17 semantic replay cases, 12 cache invalidation
  cases, incremental build, a 31-module 100-cycle native qualification, QSLite,
  six async fixtures, 82 sanitizer cases, and 12 release-package checks. The
  report is local dirty-worktree evidence, not a replacement for clean RC CI.

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

### 6.1 Bounded Audit Budget

`ERG-6` is the final active discovery pass for 1.0 source ergonomics. `ERG-7`
may normalize and requalify the two reference programs, but it must not open a
new category of convenience work. After `ERG-6` and `ERG-7` are closed:

- no new ergonomics item is admitted from speculative API review or isolated
  spelling preference;
- a new item may block 1.0 only when the native builder, QSLite, or the unified
  release gate demonstrates that ordinary frozen-language code cannot be
  expressed without a workaround, compiler-internal detail, hidden cost, or
  misleading rejection;
- two consecutive clean sustained qualifications with no newly observed
  ergonomics workaround end this audit; and
- work that needs new syntax, a new implicit conversion, an ownership-policy
  choice, or broader library design moves to the post-1.0 ledger instead of
  extending this audit.

The exit decision is therefore evidence-based: once the known ledger is
closed and the two real programs stop producing new release-blocking friction,
the project returns to the remaining FZ-5 release work.

This decision has now been reached. Further source-convenience work is stopped
for 1.0 unless a sustained application or the release gate demonstrates a new
correctness issue or ordinary-code blocker under the criteria above.

This stop does not claim that same-name generic template identity has been
audited. Generic instantiation and cache keys retain their previous behavior and
are tracked separately as `FZ-3-R02`; that bounded correctness item must not
reopen the ergonomics discovery ledger.
