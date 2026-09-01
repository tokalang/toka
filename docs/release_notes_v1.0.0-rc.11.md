# Toka v1.0.0-rc.11 Release Candidate Notes

**Status:** Candidate awaiting hosted qualification and protected promotion.

RC11 is a product-quality repair candidate for the Public Preview. It is based
on ten independent AI-agent black-box trials of the published RC10 SDK and the
TokaKV ten-minute tour. Those trials established that the agentic systems
programming path is independently reproducible, while also finding two
release-blocking developer-experience defects.

RC11 changes the SDK, CLI diagnostics, and documentation. It does not add
syntax, change ownership rules, or alter language semantics.

## SDK readiness checks

- `toka doctor` now executes the packaged Python build helper, so a missing or
  incompatible Python runtime fails before `add`, `build`, or `run` can fail.
- The helper requires Python 3.10 or newer.
- On Linux, `toka doctor` also verifies that `pkg-config` can resolve the
  OpenSSL linker inputs used by SDK projects.
- The installed-SDK developer-experience suite includes a negative Python
  runtime test, preventing the previous false-ready result from returning.

## Project-aware semantic tooling

- `toka check`, `context`, `evidence`, `cede-obligations`, `capabilities`,
  `hole-goals`, `conditional-facts`, `index`, and `query` now consume the
  locked project dependency graph on the published macOS and Linux SDK targets.
- A project using a public Registry dependency can run `toka check --json
  src/main.tk` and `toka evidence --json src/main.tk` directly, without guessing
  expanded package paths.
- Semantic evidence uses check-only compilation, keeping successful JSON output
  machine-readable instead of mixing it with generated LLVM IR.
- Installed-SDK regression tests verify dependency resolution and the evidence
  JSON schema.

## Diagnostic and package CLI cleanup

- The SDK's own `lib/build.tk` now uses explicit mutable-call markers and is
  warning-clean under the default compiler profile. User-source `W0408`
  diagnostics remain enabled and covered by regression tests.
- Top-level help now describes `add <package-or-url>`, and `toka add --help`
  explains the verified Registry name path with `toka add tokakv` as the
  concrete example.
- Failure to launch the Python package helper now names the missing runtime and
  directs the user back to `toka doctor`.

## Independent-trial evidence

The RC10 black-box trial produced these product-path results:

- 10/10 agents eventually installed the SDK and completed the TokaKV tour in
  under 15 minutes;
- 10/10 independently modified the example and repaired an intentional
  ownership diagnostic;
- no compiler crash, native crash, observed double-drop, or hang occurred;
- no trial required a Toka or TokaKV source build.

The complete evidence and its limitations are recorded in the
[Week 3 independent-agent dogfood report](https://github.com/tokalang/tokakv/blob/main/docs/week3-independent-agent-dogfood.md).
These AI-agent trials establish reproducible agentic-tooling behavior; they do
not replace future human usability research.

## Remaining non-blocking work

- Structured diagnostic provenance remains as the follow-up in
  [#41](https://github.com/tokalang/toka/issues/41); the repeated SDK-owned
  `W0408` noise is fixed in RC11.
- Resolved-version/checksum reporting after a successful package add remains in
  [#39](https://github.com/tokalang/toka/issues/39); help, helper discovery, and
  launch-failure reporting are covered in RC11.
- Scoped semantic-evidence volume remains in
  [#38](https://github.com/tokalang/toka/issues/38); project dependency
  resolution itself is fixed.
- Windows/MSYS2 remains a source-build dogfood path without a published SDK
  archive. Project-aware semantic commands were not expanded on Windows in this
  repair candidate.

## Interface cache boundary

RC11 keeps `.tki` format `3`, place-yield ABI schema `1`, and the
compiler-interface compatibility key `0.9.9-16`. RC10 and RC11 use the same
language/compiler interface boundary, so the DX-only candidate does not force a
new cache invalidation.

After publication, install RC11 explicitly rather than relying on the stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.11
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```
