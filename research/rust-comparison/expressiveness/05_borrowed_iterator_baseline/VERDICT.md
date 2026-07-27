# Verdict: borrowed iterator baseline

## Observed result

Both programs compile and run while leaving the source collection available
after traversal.  Toka's `VecIterator::next_ref` returns an `Option<&i32>` with
an explicit `<- self` dependency; Rust's slice/`Vec` iterator borrows elements
through its ordinary iterator API.  Both examples explicitly turn each borrowed
element into an owned integer before adding it to the longer-lived sum: Toka
uses a local `copied: i32`, while Rust uses `.copied()`.

## Fair conclusion

This is intentionally a tie case.  It disproves the broad claim that Toka lacks
ordinary borrowed iteration or must copy elements to iterate.  The source-level
tradeoff is different: Toka exposes the dependency in the iterator protocol,
while Rust normally infers it from iterator types and borrow checking.

This case does not establish equivalence for consuming iteration, lazy adapters,
or lending higher-order adapters; those are separate questions.
