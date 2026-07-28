# `stdx/serde/toml` v1

`stdx/serde/toml` is an owned, deterministic configuration reader. It parses
single-line TOML configuration into a `TomlDocument`; table members are exposed
through dotted keys such as `server.port`. `TomlDocument::get` returns an owned
`TomlValue`, so callers retain no view into parser input.

The v1 value domain is intentionally small: basic and literal strings,
booleans, decimal `i64` values (with `_` separators), and nested arrays of
those values. It also supports comments, bare dotted keys, and bare-key table
headers. Duplicate keys are rejected deterministically.

This is not an assertion of full TOML 1.0 coverage. Floats, date/time values,
inline tables, arrays of tables, quoted keys, multiline strings, and document
serialization are explicitly out of v1. Those features require separate
contracts rather than silently falling back to stringly typed configuration.
