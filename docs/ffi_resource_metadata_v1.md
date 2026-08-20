# FFI Resource Metadata v1

Status: **Implemented machine-readable package metadata**

This is a package contract for opaque native resources. It introduces no Toka
source syntax, no ABI change, no generated wrapper, and no automatic runtime
enforcement. Its purpose is to make the resource facts available to tools,
audits, AI-assisted changes, and later diagnostics.

Native packages may list resource contracts inside `native`:

```toka
native = (
    required = true,
    ffi_resources = (
        (
            name = "Window",
            acquire = "toka_gui_macos_create_window",
            release = "toka_gui_macos_destroy_window",
            ownership = "owned",
            nullable = false,
            thread_affinity = "ui",
            send = false
        )
    )
)
```

Every record has seven required facts:

- `name`: stable resource label within the package;
- `acquire`: native symbol that creates or returns the handle;
- `release`: release symbol for `owned`, or exactly `"none"` for `borrowed`;
- `ownership`: `"owned"` or `"borrowed"`;
- `nullable`: whether acquire may produce physical address zero; Safe Toka
  exposes such an acquire result as raw `nul *T`, never as a nullable owning
  handle;
- `thread_affinity`: `"any"` or `"ui"`;
- `send`: whether the handle may cross a task/thread boundary.

Validation is fail-closed: unknown ownership/affinity values are rejected, an
owned resource needs a release symbol, and a UI-affine resource cannot claim
`send = true`. The resolver emits validated records in the target-specific
native build plan and the build fingerprint covers them.

`official/gui` is the first live record: `Window` is owned, non-null,
UI-affine, non-`Send`, and released by its Objective-C bridge. The existing
Toka `App`/`Window` non-`@Send` types remain the actual language enforcement;
this metadata describes the native boundary rather than replacing it.
