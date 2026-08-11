# Public registry OpenAI-compatible consumer

This is the retained cross-repository consumer fixture for
`official/openai_compat@0.1.1`. It resolves the package through the public
registry rather than a monorepo-relative path.

Run it with an installed SDK:

```text
toka build
./target/debug/registry_openai_compat_consumer
```

The committed lock records the immutable `0.1.1` release. Replaying it with
`TOKA_OFFLINE=1 toka build` must produce the same executable after the archive
has been fetched once.
