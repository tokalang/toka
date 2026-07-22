# Toka LSP support

`tokalsp` is a standard-input/standard-output language server. Editors should
launch it without arguments and use `Content-Length` framed JSON-RPC 2.0.

The 1.0 baseline supports full document synchronization, compiler diagnostics,
hover, go to definition, references, completion, rename, and the standard
initialize/shutdown/exit lifecycle. The server discovers `tokac` beside its own
executable first and then on `PATH`, so an installed SDK remains self-contained.

Diagnostics come from `tokac --check-json`. Editing features currently use a
UTF-16-aware lexical index over all open documents. This makes them predictable
and useful for the first release, but rename and reference results are not yet
scope- or type-aware. The next LSP milestone is to replace that index with a
stable compiler semantic-query interface without changing the LSP protocol.
