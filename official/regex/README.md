# `official/regex` v1

`official/regex` is Toka's official regular-expression package. Its package
identity and public import path are `official/regex`; its manifest short name
is `regex`.

## Design boundary

v1 compiles a byte-oriented regular-expression subset to a Thompson-style NFA.
It never uses recursive backtracking. Matching uses bounded state proportional
to the compiled pattern and has `O(pattern_bytes * input_bytes)` worst-case
work, rather than exponential work from adversarial input. Pattern compilation
has a fixed size limit; malformed or oversized patterns return structured
errors containing the first relevant byte position.

The initial public API will be:

```toka
import official/regex::{Regex, RegexError}

auto compiled = Regex::compile("ab+c")
if compiled.is_ok() {
    assert(compiled.unwrap().is_match("abbbc"))
}
```

`Regex::compile` returns `Result<Regex, RegexError>`. `Regex::is_match` tests
whether any substring matches; `Regex::find` returns byte offsets for the first
match. The API owns compiled pattern data and never returns a view into a
temporary input.

## v1 syntax

- literal bytes and escapes for metacharacters;
- `.` for one byte;
- concatenation, grouping `(...)`, and alternation `|`;
- postfix `*`, `+`, and `?`;
- ASCII byte classes such as `[abc]`, `[a-z]`, and `[^0-9]`;
- `^` and `$` anchors.

Escapes are intentionally limited to literal metacharacters and byte escapes
documented by the implementation. Matching operates on UTF-8 string bytes;
it does not promise Unicode character classes, grapheme boundaries, or Unicode
case folding.

## Explicit non-goals

Backreferences, look-around, recursive patterns, replacement templates,
capture extraction, and Unicode property classes are outside v1. They either
need a separate bounded design or would weaken the package's predictable
resource contract.

## Qualification

Once implementation begins, run from this package root:

```text
tokac -I ../../../lib -I lib tests/regex_v1.tk -o /tmp/regex_v1 && /tmp/regex_v1
```

The qualification suite will cover accepted syntax, malformed-pattern byte
positions, anchors/classes/quantifiers, and adversarial non-match cases that
would make a backtracking engine exponential.
