# Toka LSP support

`tokalsp` is a standard-input/standard-output language server. Editors should
launch it without arguments and use `Content-Length` framed JSON-RPC 2.0.

The server supports full document synchronization, compiler diagnostics,
hover, go to definition, references, completion, rename, signature help,
document and workspace symbols, formatting, and the standard
initialize/shutdown/exit lifecycle.

`tokalsp` links the reusable compiler front end and keeps an in-process
`AnalysisSession`. Open document contents are in-memory source overlays, so an
editor never needs to save a file before receiving compiler diagnostics or
semantic results. Changed modules invalidate their reverse-dependency closure;
unchanged, already-checked ASTs are reused.

Hover, definition, references, completion, rename, signature help, and symbol
queries consume compiler `SemanticIndex` identities. This makes shadowing,
cross-module calls, generic instances, and trait implementation methods
scope- and type-aware. Rename rejects a same-scope collision and can return a
multi-file workspace edit. LSP positions are UTF-16 even though compiler source
ranges are byte-based internally.

Formatting delegates to the `tokafmt` shipped beside `tokalsp` and returns a
single full-document edit without modifying the open source file. The custom
read-only request `toka/analysisStats` exposes revision, invalidation, reuse,
recheck, and elapsed-time data for qualification and troubleshooting.

Run `python3 tools/scripts/test_lsp_protocol.py` for the protocol, semantic,
overlay, incremental invalidation, UTF-16, and formatting gate.
