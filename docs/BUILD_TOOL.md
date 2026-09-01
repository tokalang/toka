# Toka build tool (`toka`)

This is the current 1.0 RC guide for Toka's project manager. Older references
to `Project.tk`, LLVM 17, or `build/src` are obsolete. The public installation
guide is also available at [tokalang.dev](https://tokalang.dev/installation/).

## Install or build the SDK

For a released macOS or Linux SDK, install a stable release with:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash
```

Use the explicit RC11 public-preview tag for a repeatable install:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.11
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```

Prebuilt SDK runtime prerequisites are Python 3.10+, a native C linker, and
OpenSSL link inputs. On Ubuntu/Debian:

```sh
sudo apt-get install clang lld python3 pkg-config libssl-dev
```

To build from this repository, install CMake, a C++17 compiler, and LLVM/LLD
20, then run:

```sh
cmake -S . -B build
cmake --build build
export PATH="$PWD/build/bin:$PATH"
export TOKA_LIB="$PWD/lib"
toka doctor
```

`toka doctor` is the supported first diagnostic. It verifies the compiler,
standard library, formatter, Python build helper, native linker, and Linux
OpenSSL link inputs.

## Start a project

```sh
toka new my_app
cd my_app
toka run
```

This creates:

```text
my_app/
├── src/main.tk
├── package.tk
└── build.tk
```

Use `toka new my_lib --lib` for a library package. `toka init` creates the
same layout in the current directory.

`package.tk` is static package metadata. `build.tk` is the project's build
orchestration entry point. `Project.tk` is accepted only as a deprecated
compatibility name.

## Common commands

```sh
toka build             # resolve locked packages and build the project
toka run               # build, then run the configured executable
toka test              # Preview only; use the package's documented test command
toka fmt               # format project source
toka check --json src/main.tk
toka doctor
```

For detailed machine-readable compiler interfaces, see
[AI tooling](ai_tooling.md).

## Packages

Search and add a package from the verified public catalog:

```sh
toka search regex
toka add regex
```

`toka add` records a dependency in `package.tk`, resolves it, and writes the
exact archive digest to `package.lock`. The resolver uses
`https://pkg.tokalang.dev` by default. `TOKA_REGISTRY_URL` is only for local
or test registries.

`toka publish` creates a release archive; it does not upload it to a public
registry. The current public path is a tagged GitHub Release followed by a
reviewed static catalog entry. Package replicators should follow
[AGENTS-USER.md](../AGENTS-USER.md).

## Environment

Released archives configure `PATH` and `TOKA_LIB` through the installer. A
source build needs the two exports shown above. `TOKA_CLANG` can select a
specific compatible native linker driver, and `TOKA_PATH` can add global
third-party import roots. Both are optional for a normal released SDK.

On Windows, use an MSYS2 UCRT64 or MinGW64 shell with LLVM/Clang 20 on
`PATH`. Windows is currently a source-build dogfood path; it does not yet ship
a blocking-release SDK archive.

## Troubleshooting

- Run `toka doctor` first; it identifies a missing compiler, standard library,
  formatter, or native linker.
- If a source build cannot resolve standard-library modules, export
  `TOKA_LIB` to this repository's absolute `lib` directory.
- If native linking fails, make sure the selected `clang` is compatible with
  LLVM 20 and set `TOKA_CLANG` only when the automatic selection is wrong.
- For a package-resolution failure, run `toka search <name>` and inspect the
  exact catalog and digest error before changing `package.lock`.
