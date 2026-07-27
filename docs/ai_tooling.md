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

`tokac --semantic-index=json` emits declaration contracts for callable and
field symbols. A contract records the declared morphology, transfer mode
(`cede`, borrow, shared, and so on), H/P permissions, nullability, async
effect, and return borrow dependencies. It is the machine-readable source of
truth for API intent.

An AI edit workflow must distinguish three layers: declaration capability,
use-site intent, and PAL's current alias/interference decision. In particular,
a `#` written at a call site is not authority to upgrade a parameter, field,
or receiver beyond its declaration contract. Read the contract before proposing
an ownership or permission-changing edit, then verify the edit with `toka
check --json`.

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
