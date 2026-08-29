# Cede Obligation Evidence v1

`toka cede-obligations --json path/to/source.tk` and
`tokac --cede-obligations=json path/to/source.tk` emit a deterministic,
machine-readable view of the ownership obligations created by `cede`.
Diagnostics remain on standard error; the compiler exit status remains the
authoritative success result.  The frozen schema is
[`schemas/toka.cede-obligation-evidence.v1.schema.json`](../schemas/toka.cede-obligation-evidence.v1.schema.json).

After RC9 signature-driven call transfer activation, this option is an
explicit historical replay profile: it retains the RC8 caller-spelling rule
and may produce `E04570`/`MissingExplicitCede` for a call accepted by ordinary
RC9 compilation. Use `--cede-obligations=v2` to inspect the active language
semantics. The replay profile prevents the frozen v1 fields from silently
changing meaning.

```json
{
  "schema": "toka.cede-obligation-evidence",
  "version": 1,
  "records": [
    {
      "stage": "caller-transfer",
      "status": "violated",
      "reason": "MissingExplicitCede",
      "subject": "payload",
      "origin": "payload",
      "location": {"file": "main.tk", "line": 8, "column": 26},
      "contract_location": {"file": "lib.tk", "line": 4, "column": 22}
    }
  ]
}
```

## Contract

- `caller-transfer` says whether an argument supplied to a consuming parameter
  used the required explicit `cede` transfer.
- `callee-consumption` says whether a `cede` parameter was consumed on every
  required function-body path.
- `return-transfer` says whether a `cede` return contract was fulfilled.
- `fulfilled` and `violated` report the compiler's static conclusion for that
  obligation.  They do not claim that a runtime destructor has executed.
- `location` identifies the transfer, use, or return that triggered the fact.
  `contract_location` identifies the parameter or return contract against
  which it was checked.
- Records are sorted and exact duplicates removed.  A consumer must reject an
  unknown `schema` or `version` rather than guess compatibility.

This is intentionally a narrow companion to
[Public Semantic Evidence v1](semantic_evidence_v1.md).  The public protocol
continues to explain general compiler decisions.  This protocol answers the
repair-focused ownership question: which `cede` obligation exists, and was it
fulfilled or omitted?

## AI repair loop

For an ownership error, tools can run this protocol before proposing an edit:

```text
diagnostic -> cede obligation evidence -> minimal explicit transfer or consumption -> check -> tests
```

For example, `MissingExplicitCede` points at the caller expression and its
consuming parameter declaration; `UnconsumedCede` points at the callee
parameter contract.  A tool must not infer permission or ownership from prose
or fabricate a `cede`: it should use the source locations and then re-check
the proposed edit.

## Verification

`tools/scripts/test_cede_obligation_evidence.py` is the fail-closed ABI gate.
It checks schema identity, deterministic direct compiler output, the SDK
wrapper, fulfilled and violated facts for all three stages, and rejection of
ambiguous mixed JSON output modes.
