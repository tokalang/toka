# `stdx/data/csv` AI-library pilot

## Decision

`stdx/data/csv` is the first post-1.0 library pilot. It is a common
interoperability dependency, is implementable entirely in safe Toka, and
exercises byte-oriented parsing, owned values, error reporting, and standard
I/O adapters without adding language semantics or a native dependency.

It is deliberately not a claim that Toka has a general ecosystem already. The
pilot validates the machine-facing development loop on one real library before
we schedule larger or security-sensitive work such as TOML, regex, compression,
or JWT.

## Public core

The first release exposes a strict RFC 4180 core:

```toka
pub shape CsvError (
    line: usize,
    column: usize,
    message: string
)

pub fn parse_records(input: str) -> Result<Vec<Vec<string>>, CsvError>
pub fn write_records(records: Vec<Vec<string>>) -> string
pub fn read_record<'R: @Reader>(reader#: BufferedReader<'R>, max_record_bytes: usize)
    -> Result<Option<Vec<string>>, CsvError>
pub fn write_record<'W: @Writer>(writer#: BufferedWriter<'W>, cede record: Vec<string>)
    -> Result<(), CsvError>
```

`parse_records` owns every returned field. It must not expose a borrowed field
view whose backing input may be dropped. `write_records` emits CRLF row
terminators, quotes fields containing comma, quote, CR, or LF, and doubles a
quote inside a quoted field.

The synchronous streaming helpers build on the existing `BufferedReader` and
`BufferedWriter` contracts rather than introducing a second reader/writer
owner type. `read_record` accumulates exactly one logical record, enforces the
caller-provided byte limit, and returns owned fields. `write_record` emits one
record; flushing remains the caller's batching decision.

The parser accepts CRLF records, quoted fields, escaped quotes, empty fields,
and a final record without a trailing CRLF. It rejects malformed quoting,
unclosed quoted fields, bare CR/LF inside an unquoted field, and unequal field
counts after the first non-empty record. Error positions are one-based byte
line/column coordinates.

## Explicit non-goals

- No automatic type inference or schema derivation.
- No dialect guessing, delimiter selection, comments, or spreadsheet-specific
  escape rules.
- No zero-copy field API in the first release. A later borrowed-view reader
  must have an explicit owner-carrying contract rather than retaining raw
  pointers across a call or async suspension.
- No async reader in the first release. The synchronous adapter deliberately
  reuses `stdx/io/bufio`; async streaming remains a later, separate contract.

## Delivery slices

1. Add the public data contract and deterministic parser tests for the RFC
   lexical state machine.
2. Implement parse/write round-trip behavior and malformed-input tests.
3. Add `std/io` reader and writer adapters with bounded record-size limits.
4. Add corpus/property-style qualification, API-contract index assertions, and
   a package-style example.

Each slice must compile with `toka check --json`, pass its focused test file,
and leave `git diff --check` clean. The final slice additionally runs the
existing AI tooling and semantic-index gates. A failure to meet an RFC 4180
case remains a library bug; it must not cause a change to frozen ownership,
permission, or async semantics.

## AI workflow evidence

Before changing a public CSV declaration, an agent must use:

```text
toka index --json lib/stdx/data/csv.tk
toka query references lib/stdx/data/csv.tk --query-file ... --line ... --character ... --json
toka check --json <changed source>
```

The index supplies declaration capability facts, the query supplies the
compiler-resolved impact set, and `check --json` is the post-edit semantic
gate. Runtime behavior is proven separately by the focused CSV tests.

## Pilot closure evidence

The final slice adds `tests/pass/g14_stdx_csv_corpus_test.tk`, which checks a
deterministic valid/invalid RFC 4180 corpus and the parse/write canonicalization
property. `tools/scripts/test_ai_tooling.py` also checks the public semantic
contracts of `parse_records`, `read_record`, and `write_record`, including the
mutable buffered endpoint and `cede` record transfer.

[`toka-examples/csv-transform`](https://github.com/tokalang/toka-examples/tree/main/csv-transform)
is a minimal buildable project that copies records from an input file to an
output file through the public streaming API:

```text
cd toka-examples/csv-transform
toka build
./target/debug/csv_transform input.csv output.csv
```
