# Verdict: partial Result propagation

Rust permits `holder.result?`; it moves the field and tracks the surrounding
value's partial move state.  Toka 1.0 rejects `holder.result!` with `E04595`.
The current local move ledger records whole-binding propagation, not independent
field liveness; binding the Result field to a local first makes the transfer
explicit and replayable.

This is a **conservative surface/ergonomics difference**.  It does not show
that Toka cannot represent the program or that PAL needs a change.  Relaxing it
would require a separately audited partial-move and cleanup model, including
field drop state, branches, aggregates, `cede`, and source-less replay.
