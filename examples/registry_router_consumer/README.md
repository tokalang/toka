# Public registry router consumer

This is the retained cross-repository consumer fixture for
`official/router@0.1.0`. It intentionally resolves the package through the
public registry rather than a monorepo-relative path.

Run it with an installed SDK:

```text
toka build
./target/debug/registry_router_consumer
```

The committed lock records the immutable `0.1.0` release. Replaying it with
`TOKA_OFFLINE=1 toka build` must produce the same executable after the archive
has been fetched once.
