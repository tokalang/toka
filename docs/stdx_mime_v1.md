# `stdx/net/mime` v1

`stdx/net/mime` parses and formats HTTP-style media types as an owned `Mime`
value: normalized `type/subtype` tokens and owned parameters. `Mime::parse`,
`essence`, `parameter`, and `to_string` are the v1 API.

The parser accepts RFC token values and quoted-string parameter values with
backslash escapes, rejects malformed delimiters/control bytes, and rejects
duplicate parameter names for deterministic lookup. Type/subtype and parameter
names are normalized to ASCII lowercase; parameter values preserve their bytes.

This module is deliberately independent of `HttpRequest` and `HttpResponse`.
HTTP callers may use it for `Content-Type`, while non-HTTP protocol libraries
can reuse the same value type. Multipart parsing, media-range matching, and
structured suffix policy remain out of v1.
