# Phase 2 Cache And Replay Closure

This report records the completion evidence for the semantic cache and replay
closure phase. It does not introduce new language syntax or semantics.

## Closed Invariant

For every frozen semantic fact covered by this phase, a consumer must receive
the same acceptance decision and primary diagnostic from provider source and
from a generated source-less `.tki`. When the provider source changes, an old
cache must report `SourceHashMismatch`, fall back to source, and a newly emitted
interface must replay the new decision after the source is removed.

## Requirement Evidence

| Requirement | Direct evidence |
| --- | --- |
| Private resource structure and field morphology | `resource_private_field`, `own_resource_001_private_field` |
| `drop` and explicit `clone` method facts | `clone_method_removed`, `own_resource_001_private_field` |
| Generic function bounds | `generic_function_bound` |
| Generic impl `where` constraints | `generic_impl_where` |
| Trait prerequisites | `trait_prerequisite` |
| Associated type bindings | `associated_type_binding` |
| Dyn object safety | `dyn_object_safety` |
| Member-specific effects and swapped routing | `member_effect_swap`, `eff_member_001_return_deps` |
| Selective member dependency transfer and release | `eff_member_001_return_deps` |
| Generic private-resource spread and copy capture | `own_resource_002_spread_generic` |

Cache cases are under `tests/semantics/tki_cache/cases`. Replay cases are under
`tests/semantics/tki_replay/cases`.

## Harness Guarantees

`tools/scripts/test_semantic_replay.sh` now runs every consumer twice: once
against provider source with an isolated empty build cache, and once against
the generated interface after the provider source is hidden.

`tools/scripts/test_semantic_cache_invalidation.sh` proves four states for every
case:

1. The old generated interface accepts the discriminating consumer.
2. The changed source produces `SourceHashMismatch` and source fallback.
3. The changed source rejects the consumer with the expected diagnostic.
4. A fresh interface still rejects it after the changed source is hidden.

## Verification Snapshot

- Semantic replay: 9 passed, 0 failed.
- Semantic cache invalidation: 12 passed, 0 failed.
- Compiler negative suite: 218 passed, 0 failed.
- TKI cache validation: all cases passed.
- Incremental build suite: all cases passed.
- General positive suite: 310 passed; the same three environment/runtime
  network cases failed (UDP bind and two async network scheduler runs), with no
  new compilation regression.

## Deliberate Boundary

Borrow preservation across suspension inside an async function is not part of
this closure. Its rule is not frozen and requires a separate language-design
decision. This phase changes no suspension semantics.

Subsequent status: `FZ-1` froze the existing suspension model without adding
syntax. Its completion evidence is recorded in
`fz1_async_suspension_closure.md`; this paragraph remains the historical
boundary of Phase 2.
