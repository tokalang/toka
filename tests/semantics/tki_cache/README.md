# TKI Semantic Cache Invalidation Tests

These cases verify that changing only a public semantic annotation invalidates
an existing cached interface. The runner also compiles a discriminating
consumer after fallback, proving that the new constraint is enforced rather
than merely reported in the dependency manifest.

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
