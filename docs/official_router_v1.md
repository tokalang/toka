# `official/router` v1 — Deterministic Route Recognition

Status: **implementation proposal; v1 stops at method/path recognition**.

## Role and boundary

`official/router` is an optional package above `stdx/net/http`. It accepts an
`HttpMethod` and HTTP request target and returns an application-owned route
name plus owned path parameters. It does not own sockets, request parsing,
response generation, handler execution, middleware, authentication, or
application state. Those boundaries already belong to `std`, `stdx`, and the
application respectively.

This narrow boundary makes it usable by the existing service-kit without
turning that reference application into a framework.

## v1 matching contract

- Patterns begin with `/`; `/` is the root route.
- A non-root route is a non-empty sequence of whole slash-delimited segments.
- Literal segments compare byte-for-byte. A `:name` segment captures one
  non-empty segment under an ASCII identifier name.
- Request query text beginning at `?` is excluded from selection. The router
  does not percent-decode, normalize slashes, interpret fragments, or support
  wildcard/catch-all segments.
- A literal segment is more specific than a parameter. If two same-method
  patterns can match the same target at equal literal specificity, registration
  rejects the ambiguity. Exact duplicate method/shapes and duplicate parameter
  names are also rejected.
- `recognize` returns `None` when no route has the requested method.
  `allowed_methods` lets the host distinguish 404 (empty) from 405
  (non-empty) and construct any response/header policy itself.

## Resource and error contract

The router stores owned pattern text and returns owned route/parameter text;
no request view escapes. Registration returns a typed `RouterError` for all
invalid definitions. Recognition never requires unsafe code, I/O, a runtime
reactor, or callback/closure lifetime rules.

## Stop boundary

v1 deliberately excludes a trie, handler registry, dynamic route removal,
middleware, query parsing, wildcards, regex segments, reverse routing, and
framework/server integration. A trie is an optimization only after evidence
shows that the bounded linear route table is insufficient.
