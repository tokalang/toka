# Verdict: scoped borrowed concurrency

## Observed current boundary

The Rust program runs using only `std::thread::scope`.  Its child borrows
`value`, and the lexical scope guarantees that the child is joined before
`value` can be destroyed.

The Toka program is intentionally a current-1.0 detached-task example.  It is
rejected with `E04583`: `.start` cannot carry `str` or other borrow-like
dependencies across the execution boundary.  This agrees with
[`docs/syntax.md`](../../../../docs/syntax.md), which freezes detached task and
thread handoff as non-borrowing/explicit-transfer only.

## Classification

This is a **current 1.0 boundary**, not a Toka model limitation.  Existing
`TaskScope` manages runtime task completion, but it does not give a child a
lexically bounded borrow of a parent scope.  The evidence does not establish
that Toka requires user-written lifetimes to add the capability.

## Toka-style design candidate

A future lexical task scope can be specified with these rules, without changing
ordinary detached `.start`:

1. A scoped child has a non-escaping scope dependency rather than detached task
   ownership.
2. The scope cannot finish until all scoped children have joined or cancelled
   and reached a terminal state.
3. Scoped task handles cannot be returned, detached, stored in an escaping
   aggregate, or transferred to an outer task.
4. PAL records the child dependency against the lexical scope anchor; borrowed
   captures are valid only while that anchor is live.
5. OS-thread variants additionally require the existing cross-thread safety
   constraints (`@Send` / `@Sync`); the scoped lifetime rule does not replace
   them.

The acceptance criterion is a real Toka program whose borrowed child reads a
parent-local value, whose scope joins it before exit, and whose escaping and
detachment variants are rejected.
