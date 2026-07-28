# `official/regex` v1

Status: **prototype implementation; package qualification is not yet closed**.

`official/regex` is Toka's official regular-expression package. Its package
identity and public import path are `official/regex`; its manifest short name
is `regex`.

## Design boundary

v1 is a byte-oriented **RE2 syntax profile**. Its completed matcher will use a
Thompson-style NFA, never recursive backtracking. Matching state is bounded by
the compiled pattern and worst-case work is `O(pattern_bytes * input_bytes)`,
rather than exponential work from adversarial input. Pattern compilation has a
fixed size limit; malformed or oversized patterns return structured errors
containing the first relevant byte position. v1 limits patterns to 4096 bytes
and parenthesis nesting to 128 levels.

The public API is:

```toka
import official/regex::{Regex, RegexError}

auto compiled = Regex::compile("ab+c")
if compiled.is_ok() {
    assert(compiled.unwrap().is_match("abbbc"))
}
```

`Regex::compile` returns `Result<Regex, RegexError>`. `Regex::is_match` tests
whether any substring matches; `Regex::find` returns byte offsets for the first
match; and `Regex::find_all` returns all leftmost, non-overlapping matches.
Empty matches are reported once at each search boundary and then advance one
byte, avoiding an infinite scan. The API owns compiled pattern data and never
returns a view into a temporary input.

## v1 syntax profile

- literal bytes and escapes for metacharacters;
- `.` for one non-LF byte (no dotall flag in v1);
- concatenation, grouping `(...)`, and alternation `|`;
- postfix `*`, `+`, `?`, and counted repetitions `{m}`, `{m,}`, `{m,n}`;
- ASCII byte classes such as `[abc]`, `[a-z]`, and `[^0-9]`;
- `^` and `$` anchors.

The public target is this RE2-compatible regular subset, not a claim of full
RE2 compatibility. The implementation slice enables literals, escaped metacharacters,
`.`, `^`, `$`, grouping, alternation, postfix `*`, `+`, `?`, and counted
repetitions, plus ASCII
character classes, ranges, and negated classes. Escapes are intentionally
limited to literal metacharacters, `\n`/`\r`/`\t`, `\xNN`, and ASCII
`\d`/`\D`/`\w`/`\W`/`\s`/`\S` outside bracket classes. Matching operates on
UTF-8 string bytes; it does not promise Unicode character classes, grapheme
boundaries, or Unicode case folding.

Counted repetitions accept bounds through 1000. Compilation also rejects a
pattern that would expand beyond 32,768 NFA states; this preserves the v1
bounded-resource contract even when a short pattern contains a large group.

## Explicit non-goals

Backreferences, look-around, recursive patterns, replacement templates,
capture extraction, and Unicode property classes are outside v1. They either
need a separate bounded design or would weaken the package's predictable
resource contract.

## Qualification

Run the qualification from this package root:

```text
tokac -I ../../../lib -I lib tests/regex_v1.tk -o /tmp/regex_v1 && /tmp/regex_v1
```

The qualification suite covers accepted syntax, malformed-pattern byte
positions, anchors/classes/quantifiers, and adversarial non-match cases that
would make a backtracking engine exponential.

The v1 package must not be released until this suite passes in full. At present,
the corrected literal-order assertion is valid, but the grouped quantified case
`a(b|c)+d?` does not yet match `ac`; this is an open matcher qualification
defect, not an unsupported syntax carve-out.
