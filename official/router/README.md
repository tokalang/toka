# `official/router` v1

`official/router` is a small deterministic HTTP method/path recognizer. It is
an optional package above `stdx/net/http`, not a web framework: it neither
owns TCP connections nor invokes handlers.

```toka
import official/router::{Router}
import stdx/net/http::{HttpMethod}

auto router# = Router::new()
router#.add(HttpMethod::GET, "/notes/:id", "notes.show").unwrap()

match cede router.recognize(HttpMethod::GET, "/notes/42?view=full") {
    auto Option::Some('matched) => {
        auto id = 'matched.param("id").unwrap()
        // dispatch `matched.route_name()` in application code
    }
    _ => {}
}
```

Route patterns use whole path segments: literals and `:ascii_identifier`
parameters. `/` is the only pattern with no segments; empty/trailing segments,
query/fragment markers, duplicate parameter names, duplicate route shapes, and
equal-specificity ambiguous overlaps are rejected at registration. Static
segments take precedence over parameters. A request query is excluded from
selection; percent decoding, slash normalization, wildcard matching, handler
invocation, middleware, and HTTP status generation remain application policy.

For a missing method match, call `allowed_methods(path)`: an empty result means
404, otherwise the host may return 405 and build its own `Allow` header.

## Qualification

Run from a Toka source checkout:

```text
TOKA_ROOT=/path/to/toka python3 tests/qualify_package.py
```

The same command works after extraction with an installed SDK by setting the
explicit toolchain triple instead:

```text
TOKA=/path/to/toka TOKAC=/path/to/tokac TOKA_LIB=/path/to/lib python3 tests/qualify_package.py
```

The qualification runs the deterministic router profile, then verifies a
locked local dependency, offline lock replay, and a public-import consumer
build and run.
