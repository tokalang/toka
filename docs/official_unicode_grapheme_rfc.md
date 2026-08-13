# RFC: `official/unicode` Grapheme Segmentation v1

Status: **released standalone as `v0.1.1`; compiler-facing contract retained**

## Decision

Toka's fixed `core` remains responsible only for UTF-8 scalar decoding and
explicit scalar-index/byte-offset conversion. Full user-perceived-character
segmentation is an independently versioned `official/unicode` package,
not a partial `core` helper and not a platform ICU binding.

The canonical source, generated data, qualification, and releases live in
[`tokalang/unicode`](https://github.com/tokalang/unicode). The compiler
repository retains this boundary document, not a second package source.

The first qualified profile targets **Unicode 17.0.0, UAX #29 revision 47,
extended grapheme clusters**. The package version and generated data metadata
must name that Unicode version. A later Unicode upgrade is a deliberate
package-data update with conformance evidence, never an invisible change in a
Toka compiler or a host OS.

Normative reference: [Unicode Standard Annex #29: Unicode Text
Segmentation](https://www.unicode.org/reports/tr29/).

## Why this is outside `core`

`core/str` can decode a scalar with no external data. Extended grapheme
segmentation instead depends on versioned `Grapheme_Cluster_Break`,
`Indic_Conjunct_Break`, and `Extended_Pictographic` property data plus the
ordered UAX #29 rules. An abbreviated table that happens to handle a few
combining marks or emoji would silently give incorrect cursor behavior for
other scripts. That is worse than exposing the current explicit scalar unit.

The package may evolve with its data and has no runtime, native-library, or
platform dependency. Its public identity is `official/unicode`, matching the
rule that official packages are independently versioned ecosystem
contributions rather than additions to `std`.

## Input validity

`str` and `string` are length-delimited storage types. Their ordinary text
constructors produce UTF-8, but `push_byte_raw`, raw FFI constructors, and
protocol code can deliberately create invalid byte sequences. Every public
grapheme entry point therefore validates its `str` input during its forward
scan and returns a structured `UnicodeError` on invalid UTF-8. It must never
replace malformed input, invent a boundary, or panic.

This preserves a useful split:

```text
core/str: scalar/byte primitives and explicit validation
official/unicode: validated UAX #29 segmentation
GUI: a package adapter may map scalar state only at qualified grapheme boundaries
```

## v1 API

```toka
import official/unicode::{UnicodeError}

pub fn grapheme_count(text: str) -> Result<usize, UnicodeError>
pub fn grapheme_byte_offset(
    text: str,
    grapheme_index: usize
) -> Result<Option<usize>, UnicodeError>
pub fn grapheme_index_at_byte_offset(
    text: str,
    byte_offset: usize
) -> Result<Option<usize>, UnicodeError>
pub fn grapheme_slice(
    text: str,
    start: usize,
    end: usize
) -> Result<Option<str>, UnicodeError>
```

`grapheme_byte_offset` returns `Option::None` for a valid, out-of-range
grapheme index; `grapheme_index_at_byte_offset` returns it for a valid byte
offset that is not a grapheme boundary. `grapheme_slice` returns it for a
valid out-of-range endpoint or `start > end`; it returns `Some("")` for equal
valid grapheme boundaries. `Result::Err` always means malformed UTF-8. All
reported offsets are physical UTF-8 byte offsets; all indexes are extended
grapheme indexes. None of these methods allocates. They scan forward in
`O(input_bytes)` time and use bounded state.

The first API does not expose a host-dependent locale, perform normalization,
offer word/sentence/line breaking, shape text, or promise editor layout. UAX
#29's direct handling of non-NFD text is sufficient for default boundaries;
normalization stays a separate future profile.

## Data and reproducibility contract

The canonical package vendors the Unicode 17.0.0 source files used by its
generator under
[`data/17.0.0/`](https://github.com/tokalang/unicode/tree/main/data/17.0.0),
including their upstream version and checksums. Its checked-in generator emits
compact, sorted, non-overlapping Toka range tables under
`lib/official/unicode/generated/`. Qualification must fail when regeneration
changes generated output or when the source-data checksums do not match the
lock metadata. Building a consumer never downloads Unicode data and never
depends on ICU, CoreFoundation, or the local system Unicode version.

## Required qualification

1. Run the complete UAX #29 `GraphemeBreakTest.txt` corpus for Unicode 17.0.0.
2. Add focused fixtures for CR/LF, Hangul syllables, combining marks,
   regional-indicator pairs, emoji modifier sequences, emoji ZWJ sequences,
   prepend, and Indic conjunct rules.
3. Verify malformed UTF-8 returns `UnicodeError` without a panic or partial
   boundary result.
4. Qualify a locked offline package consumer on each supported platform; its
   output must be identical because segmentation is pure Toka data/code.
5. A GUI adapter may claim grapheme-boundary behavior only after this corpus is
   green. The released `official/gui@0.1.0` adapter satisfies that dependency
   gate; it does not establish shaping, interactive IME, or CJK end-to-end
   qualification.

The standalone `v0.1.1` release and
[`toka-examples/registry_unicode_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_unicode_consumer)
retain the package and exact-lock archive-only evidence. The immutable release
archive SHA-256 is
`c68569e6efbd9eb9bf85226eca68de3a0187d4300e320aeb13857be73b5ad28a`.

## Non-goals

- changing Toka ownership, async, `str` ABI, or the standard library module
  hierarchy;
- silently treating scalar indexes as grapheme indexes;
- a partial hand-maintained Unicode range list;
- normalization, case folding, collation, bidi layout, line breaking, font
  shaping, or IME policy.
