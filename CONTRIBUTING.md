# Contributing to Toka

Toka 1.0 RC welcomes three kinds of contribution: a minimal compiler or SDK
fix, a documentation/diagnostic improvement, and a narrowly scoped ecosystem
package replication. Please keep a change aligned with one of those outcomes;
an RC is not the time to reopen frozen surface syntax or runtime design.

## Start with a working SDK

For a source checkout, install CMake, a C++17 compiler, and LLVM/LLD 20, then:

```sh
cmake -S . -B build
cmake --build build
export PATH="$PWD/build/bin:$PATH"
export TOKA_LIB="$PWD/lib"
toka doctor
```

Run `toka doctor` from the same shell in which you will reproduce a problem.
It is more useful in an issue than a screenshot of an incomplete build.

## Choose the right path

- **Compiler, SDK, diagnostics, or documentation:** open an issue first for a
  behavior change, then make the smallest reproducible patch. Read
  [`AGENTS.md`](AGENTS.md) for repository engineering rules.
- **AI-assisted ecosystem replication:** follow
  [`AGENTS-USER.md`](AGENTS-USER.md). A package release is deliberately
  independent of a compiler pull request.
- **Question or developer-experience failure:** use the GitHub issue forms and
  include an exact command, host/architecture, `toka doctor`, and the smallest
  source or manifest that demonstrates the result.

## Validate the change you made

Build the affected SDK tools and run the narrowest relevant check before
opening a pull request:

```sh
cmake --build build
python3 tools/scripts/test_developer_experience.py
```

For a compiler semantic change, add a focused pass/fail regression under
`tests/` and run it. For a package-manager change, include a resolved and an
offline replay. For documentation-only changes, check every command and link
against the current CLI rather than copying historical instructions.

The maintainers run the full multi-platform release qualification separately.
Do not weaken a release gate or update a baseline merely to hide a regression.

## Pull requests

Describe the user-visible problem, the minimal behavioral change, and the
verification command in the pull request. Keep unrelated formatting, refactors,
generated build products, and local lockfiles out of the diff. If your patch
changes an external package contract, state the compatibility and release
impact explicitly.
