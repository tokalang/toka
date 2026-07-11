# TKI Semantic Cache Invalidation Tests

These cases verify that changing compiler-visible semantic facts invalidates an
existing cached interface. For every case the runner proves old-interface
acceptance, `SourceHashMismatch` fallback, source-side rejection, and rejection
through a freshly generated interface after the provider source is hidden.

Each case contains:

```text
before.tk
after.tk
main.tk
fail_main.tk
```

Run with:

```bash
tools/scripts/test_semantic_cache_invalidation.sh
```
