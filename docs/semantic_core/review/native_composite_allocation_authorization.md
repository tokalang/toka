# Composite native-owner cleanup — approval boundary

This is an implementation-authorization question, not another RFC or acceptance
request. The existing accepted storage design already requires Once/WaitGroup
to compose field-specific native witnesses. Automatic review has nevertheless
rejected applying their composite allocation/cleanup implementation twice.
Neither rejected patch was applied, and no alternate tool was used to apply it.

## Applied versus not applied

- Applied: non-authorizing aggregate recipes retain the actual initializer's
  child-native recipes and require closed non-native fields. These recipes do
  not give Once/WaitGroup a witness or CodeGen cleanup authority.
- Not applied: recognizing such an exact prepared composite as an allocation
  cleanup source; generating its typed field cleanup plan; sealing or consuming
  that plan. Once/WaitGroup remain unfinished.

## Exact requested authority

Allow the existing native owner allocation plan to cover **a complete, live,
prepared local composite**, with these mandatory checks:

1. Identify it through actual local initializer/return recipes and exact resolved
   declaration/field identities, not the names Once/WaitGroup or `hasDrop(name)`.
2. Every native-owning field has its own existing factory/storage/cleanup recipe;
   every other admitted field is a proven closed primitive. Missing, unknown,
   conflicting or partial fields fail closed. Custom composite Drop is excluded
   until represented by an independently validated contract.
3. Preserve pre-allocation liveness, exact source/destination, PAL, permission,
   definition-completion, and assignment-rejection rollback checks.
4. The temporary target carrier is empty, proven per field. Failure cleans the
   **prepared composite's typed live fields** once and frees only the empty
   partial target allocation. CodeGen consumes the sealed field plan and never
   infers ownership from a name or raw address.
5. No language syntax, ABI, capture representation, reference-count algorithm,
   new public trait, or implicit unique-to-shared conversion is introduced.
   Existing unique `make()` signatures remain; explicit shared wrappers use the
   already accepted native factory/managed allocation protocol.

Required verification remains the original deliverable: Once executes one
initializer across its worker attempts; WaitGroup accounts for all workers,
waits and joins correctly; source/lifetime failures and missing/mismatched
plans reject without artifacts; allocation/native-operation failures and final
cleanup are exact-once. Passing that local matrix is still not the final
thread/sync integration acceptance or full relative-baseline audit.

## Why the earlier proposals were rejected

The first proposal extended the snapshot selection list to Once/WaitGroup and
used general Drop-query fallback for a composite. Review identified possible
name/fallback-based admission. That proposal was not applied.

The revised proposal used the actual prepared local recipe and an exact
native-field list instead of the name list/Drop fallback. Review still rejected
it as a retry of composite owner admission without new explicit user approval.
The implementation is therefore paused at this boundary, not worked around.
