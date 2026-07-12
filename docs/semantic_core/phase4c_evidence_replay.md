# Phase 4C Trusted Evidence Replay

Phase 4C closes the trusted-memory-evidence transport with adversarial replay.
It does not activate cached summaries for optimization.

The focused test constructs one provider and checks three import paths:

- source body: no cache evidence is consulted;
- compiler build cache: `.tki`, `.o`, and `.tke` validate as one entry; and
- ordinary source-less interface: copied evidence and object files do not
  establish cache trust.

All three paths preserve the same positive compilation and the same missing
`cede` rejection. The cache path also proves that the imported active summary
remains `signature_only` during Phase 4C.

The downgrade matrix independently covers:

- missing sidecar;
- malformed JSON;
- unsupported schema;
- compiler/interface/source/target identity mismatch;
- invalid or out-of-range summary records; and
- a backing object whose SHA-256 no longer matches.

Every failure produces a stable evidence status, attaches no partial records,
and continues through ordinary source-less semantics. Evidence export is also
repeated to require byte-identical sidecars for the same provider and object.

Run directly with:

```sh
python3 tools/scripts/test_trusted_memory_evidence.py
```

The suite is part of `tools/scripts/test_pass.sh` and complements the existing
TKI cache validation, unsafe-interface revalidation, semantic evidence, and
source-less replay suites.
