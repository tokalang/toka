# Public Semantic Evidence v1

`toka evidence --json path/to/source.tk` and
`tokac --semantic-evidence=json path/to/source.tk` emit Toka's public
semantic-decision protocol.  The historical
`--dump-semantic-evidence=json` spelling remains supported as a compatibility
alias, but new tools must use `--semantic-evidence=json`.

The output is one JSON document on standard output. Diagnostics remain on
standard error and the compiler exit code remains authoritative for overall
compilation success. The exact v1 schema is
[`schemas/toka.semantic-evidence.v1.schema.json`](../schemas/toka.semantic-evidence.v1.schema.json).

```json
{
  "schema": "toka.semantic-evidence",
  "version": 1,
  "records": [
    {
      "rule": "PAL-BORROW-001",
      "operation": "ExclusivePayloadBorrow",
      "decision": "Reject",
      "reason": "ActiveExclusiveBorrow",
      "subject": "request.body",
      "origin": "request.body",
      "primary_location": {"file": "main.tk", "line": 22, "column": 5},
      "origin_location": {"file": "main.tk", "line": 18, "column": 5}
    }
  ]
}
```

## Contract

- `schema` and `version` identify this protocol. Consumers must reject an
  unknown version rather than infer compatibility.
- `records` are sorted and exact duplicates removed before serialization.
  Repeating the same compilation therefore produces byte-identical output.
- `rule`, `operation`, `decision`, and `reason` are stable compiler
  identifiers. Tools must not recover semantic meaning by parsing diagnostic
  prose.
- `subject` names the path or value whose operation was considered. `origin`
  names the relevant source of an alias, dependency, transfer, or interface
  fact when one exists.
- locations are one-based source positions. A missing origin is represented by
  an empty file and zero line/column; it is never represented by a null or an
  omitted field.
- `Allow`, `Reject`, and `ConservativeReject` describe the compiler decision,
  not a runtime proof. The stream intentionally reports decision points, not a
  complete execution trace.

Public v1 covers PAL interference, ownership transfer and `cede` obligations,
return/member dependencies, async execution boundaries, interface replay,
unsafe public boundaries, and error propagation where the corresponding
semantic rule records a decision. New categories require a protocol-version
decision; tools must tolerate additional records from existing categories.

## AI and tooling use

The protocol answers *why* a candidate edit was accepted or rejected. A safe
edit loop is:

```text
semantic index -> candidate edit -> check/diagnostics -> semantic evidence -> tests
```

For an error, use the record's rule/reason and origin location to select a
repair. For a sensitive successful edit, retain relevant `Allow` records as
evidence that its permission, transfer, or aliasing decision came from a
declared source rather than a use-site upgrade.

Evidence is compiler-derived and observational: enabling it does not change
object output or TKI contents. Source-less replay tests verify that imported
interface facts produce equivalent caller-side decisions. Evidence itself is
not trusted when supplied by an interface or external tool.

## Compatibility and verification

Public v1 is additive to the ordinary compiler interface. Its deterministic
ABI gate validates direct compiler output, the SDK wrapper, the legacy alias,
positive Allow records, negative Reject records, and the JSON field contract.
Run:

```sh
python3 tools/scripts/test_public_semantic_evidence.py
tools/scripts/test_semantic_replay.sh
```
