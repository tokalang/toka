# Machine-facing compiler interfaces

Toka 1.0 exposes deterministic compiler facts for editors and AI coding tools.
These interfaces report compiler semantics; consumers do not need to infer
symbols or repairs from rendered terminal text.

## Check and repair

```sh
toka check --json path/to/main.tk
toka explain E0438
toka explain E0438 --json
```

`toka check --json` emits one `toka.diagnostics` version 2 document. Each
diagnostic contains a stable code and severity, a primary range, related
ranges, and zero or more fixes. A fix marked `machine-applicable` contains
explicit file/range/new-text edits and can be applied without parsing the
message. The command exits nonzero when the program has an error.

`toka explain` describes the language rule behind a code and gives repair
guidance. Its JSON form uses the versioned `toka.diagnostic-explanation`
schema.

## Bounded semantic context

```sh
toka context path/to/main.tk \
  --query-file path/to/main.tk --line 12 --character 8
```

Positions are zero-based UTF-16 positions. The result identifies the resolved
symbol and returns its definition, references, and at most 20 visible symbols.
The `truncated` field tells a caller when the visible-symbol list was bounded.
Ordering and serialization are deterministic, so callers may cache or compare
responses directly.

For bulk navigation and refactoring queries, use the semantic-index interface
documented in [semantic_index.md](semantic_index.md). For a persistent editor
session, use [tokalsp](lsp.md).

## Public API contracts

`toka index --json path/to/main.tk` emits declaration contracts for callable
and field symbols. It is the SDK entry point for the compiler semantic index
(`tokac --semantic-index=json` is the equivalent low-level invocation). A
contract records the declared morphology, transfer mode (`cede`, borrow,
shared, and so on), H/P permissions, nullability, async effect, and return
borrow dependencies. It is the machine-readable source of truth for API
intent.

An AI edit workflow must distinguish three layers: declaration capability,
use-site intent, and PAL's current alias/interference decision. In particular,
a `#` written at a call site is not authority to upgrade a parameter, field,
or receiver beyond its declaration contract. Read the contract before proposing
an ownership or permission-changing edit, then verify the edit with `toka
check --json`.

For a public API change, locate the declaration with `toka query definition`
or inspect it through `toka index --json`, then run `toka query references` at
the declaration position before editing. The result is compiler-resolved,
complete symbol identity data rather than a text search. A deterministic AI
loop is therefore:

```text
index contract -> query references -> edit -> check --json -> project tests
```

The final project-test command remains project-defined; the compiler never
pretends that a source-only check proves runtime behavior.

## Ephemeral semantic diff

Before applying an AI-proposed edit, compare the original and candidate root
sources without writing either one:

```sh
toka preview --base /work/base/main.tk --candidate /work/candidate/main.tk
```

The [Ephemeral Semantic Diff Preview v1](semantic_diff_preview_v1.md) joins
the independently versioned diagnostic, semantic-index, and Public Semantic
Evidence protocols into one deterministic preview. It reports public API and
H/P contract deltas, root diagnostics, raw/unsafe contract surface, and the
compiler decisions introduced or removed by the candidate. It is explicitly
read-only and short-lived; persistent overlays and incomplete-edit support are
later phases rather than hidden behavior in this command.

For a failed ownership, borrowing, transfer, dependency, or execution-boundary
check, ask the compiler for the corresponding decision facts:

```sh
toka evidence --json path/to/main.tk
```

This emits the frozen [Public Semantic Evidence v1](semantic_evidence_v1.md)
protocol. The deterministic AI tooling evaluation includes this evidence path;
it proves compiler protocol availability and cost, not a claim about any
particular remote model.

For a focused ownership repair, request the companion cede-obligation view:

```sh
toka cede-obligations --json path/to/main.tk
```

[Cede Obligation Evidence v1](cede_obligation_evidence_v1.md) reports whether
the caller transfer, callee consumption, or `cede` return obligation was
fulfilled or omitted, together with both the triggering location and contract
location. It is deliberately narrower than Public Semantic Evidence: use it
to make the smallest ownership repair, then re-check and test the candidate.

For async ownership or cancellation edits, consult the machine-readable
[TaskHandle Lifecycle Contract v1](taskhandle_lifecycle_v1.md) before changing
`.start`, `.await`, cancellation, drop, or detach behavior. Its redline gate
turns the lifecycle promises into executable checks rather than a prose-only
runtime claim.

For a rejected or sensitive mutable call, request the bounded H/P explanation:

```sh
toka capabilities --json path/to/main.tk
```

[H/P Call Capability Pilot v1](capability_pilot_v1.md) shows the direct
declaration ceiling, inferred state, use-site request, signature requirement,
and actual grant for ordinary call arguments. It is an explanation protocol,
not a new inference rule.

## Regression evaluation

Run the interface contracts and the fixed AI-coding task set after building:

```sh
python3 tools/scripts/test_ai_tooling.py
python3 tools/scripts/evaluate_ai_coding.py
```

The evaluation records compile, diagnostic, repair, edit-precision, and
semantic-context success rates. It also reports source/output bytes, tool calls,
and repair rounds as reproducible cost proxies. CI compares these results with
`tests/tooling/ai_eval/baseline.json`; this evaluates the compiler protocol, not
the quality of any particular language model.

Model-versus-language studies are deliberately non-blocking and external to the
release gate: they must pin a task corpus, model revision, prompt/context,
tool budget, and captured Public Semantic Evidence artifacts. This prevents
provider or model drift from changing the compiler's release result.
