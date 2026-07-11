# Phase 3B Decision Evidence

Date: 2026-07-11

Phase 3B gives structured semantic facts an explanation and replay consumer.
It does not change source syntax, acceptance rules, or the ordinary TKI
format.

## Internal Schema

`--dump-semantic-evidence=json` emits one internal JSON document:

- schema: `toka.semantic-evidence`,
- version: `1`, and
- records: a deterministically ordered, deduplicated array.

Each decision record contains:

- a frozen semantic rule ID,
- an operation class,
- `Allow`, `Reject`, or `ConservativeReject`,
- a stable reason category,
- canonical display paths for the subject and optional origin, and
- primary and origin source locations.

Rule, operation, decision, and reason values are compiler enums. Diagnostic
prose is not parsed to recover them. Process-local AST addresses and symbol
IDs are never emitted.

The internal schema is versioned so tests and compiler tools can reject an
unknown representation. It is not a public language or tooling ABI.

## Determinism

Locations are resolved when a record is created. Before output, records are
sorted and exact duplicates are removed. Repeating the same compilation must
produce byte-identical evidence.

The dump is opt-in and cannot be combined with other JSON or evaluation output
modes. Ordinary compilation does not allocate records or emit evidence.

## Causal Diagnostics

PAL ledger entries retain the source location that created a borrow. A PAL
conflict therefore reports the existing primary error and a related note at
the conflicting borrow.

Ownership and effects checks retain or recover corresponding origins:

- use-after-move points to the move,
- missing cede at a call points to the cede parameter,
- missing cede return points to the function declaration,
- forbidden resource copies point to the value or resource type,
- escaping-local and lifetime-depth failures point to the dependency, and
- missing return dependencies point to the relevant parameter or local.

These notes extend diagnostics without changing primary diagnostic codes or
the language decision.

## Source And Interface Equivalence

`tools/scripts/test_semantic_replay.sh` now compiles every consumer twice with
evidence enabled: once against source and once against a source-less TKI.

`tools/scripts/compare_semantic_evidence.py` filters records by the consumer's
primary source file and compares:

- rule,
- operation,
- decision,
- reason,
- subject, and
- origin,
- consumer primary line, and
- consumer primary column.

Provider source locations are intentionally excluded from equality. A source
declaration and its generated interface naturally occupy different files, but
they must cause the same caller-side semantic decision.

Replay cases declare required rules with `EXPECT_EVIDENCE`. This prevents two
empty record sets from passing as evidence of equivalence. Current replay
coverage exercises PAL call groups, cede parameters and returns, resource-copy
prevention, whole-return and member dependencies, and async start/suspension
contracts.

## Trust Boundary

Decision evidence is derived by the checking compiler. It is not serialized
into ordinary TKI files and an interface cannot supply or forge it. Source-less
comparison verifies decisions made from imported signature facts; it does not
turn body-derived optimizer claims into trusted interface data.

## Verification

Phase 3B is guarded by:

- deterministic dump and schema tests,
- positive and negative decision-record tests,
- causal diagnostic snapshots,
- rule-presence assertions in source/TKI replay,
- structural source/TKI evidence comparison, and
- the existing semantic cache, TKI validation, incremental, positive, and
  negative suites.

## Resulting State

The compiler can now answer, in a stable machine-checkable form, which frozen
rule made a decision, which operation was checked, why it was allowed or
rejected, and where the relevant cause originated. The same contract is
checked across source and source-less interface compilation.

Phase 3B does not add per-function memory summaries or LLVM optimization
attributes. Those require the separate Phase 3C soundness gates.
