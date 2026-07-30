# Conditional Native Dependency Contract v1

Status: **Implemented package/build contract**

One static `package.tk` is authoritative for every target. Native inputs are
declarative data, never compiler flags or shell fragments.

```toka
native = (
    required = true,
    sources = ("native/common.c"),
    macos = (
        sources = ("native/window.m"),
        frameworks = ("AppKit", "Metal")
    ),
    linux = (
        sources = ("native/reactor.c"),
        pkg_config = ("libpq", "openssl")
    ),
    windows = (
        sources = ("native/reactor.c"),
        system_libraries = ("ws2_32", "bcrypt")
    )
)
```

`sources` at the outer level are common inputs. Exactly one target block is
selected by the compiler target: `macos`, `linux`, or `windows`. A required
package that declares conditional blocks but lacks the selected one fails
closed. A non-empty package-level `targets` list is an additional fail-closed
declaration and must include the selected target.

Allowed entries are deliberately narrow:

- source paths are regular package-local `native/*.c` files; `.m` is macOS
  only;
- `pkg_config` names are logical package names; the legacy `libraries` field
  remains an alias for one compatibility cycle;
- `frameworks` are validated macOS framework names;
- `system_libraries` are validated logical linker-library names.

No custom C flags, linker flags, commands, scripts, include paths, binaries,
or arbitrary environment interpolation are accepted; unknown `native` fields
are rejected. `pkg-config` output is limited to `-L` and `-l` tokens.

The canonical `native-build-plan` records the selected target, selected
sources, logical dependencies, and FFI resource facts. The build fingerprint
includes the target triples, compiler identity/version, source bytes,
`pkg-config` resolution, linker requirements, and resource metadata. A target
or toolchain change therefore invalidates a prior native package build.

`toka build --plan` and `toka build` both delegate to the same incremental
driver. A plan is consequently a statement about every build input, rather
than a graph-only approximation that could miss a locked native package.
The native-build qualification links a minimal locked C package, changes the
effective C compiler identity, and verifies both the dirty plan and the
relinked program's native result before restoring the original identity.
Target-triple identity remains covered by the deterministic identity check;
actual cross-target rebuilding belongs to the corresponding target runner.

This v1 contract deliberately does not standardize C++, prebuilt artifacts,
cross-compilation sysroots, arbitrary flags, or a general build-script model.
