# Toka package replication guide

This guide is for a developer or coding agent helping grow the Toka ecosystem
by porting a useful library. It is not a language tutorial and it is not the
compiler-maintainer guide in `AGENTS.md`.

The goal is a small, reviewable package whose public behavior, unsupported
surface, resource boundary, and qualification command are all explicit. Start
with one useful capability rather than a wholesale compatibility claim.

## 1. Prove the SDK before editing

Install a stable SDK normally. After `v1.0.0-rc.3` is published, test that
release candidate with its exact tag:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.3
toka doctor
toka search regex
```

For a source checkout, build with CMake, add `build/bin` to `PATH`, set
`TOKA_LIB` to the checkout's `lib` directory, and run the same two commands.
Do not begin a port when `toka doctor` is not clean.

The retained public-registry consumer is the shortest end-to-end reference:

```sh
git clone https://github.com/tokalang/toka.git
cd toka/examples/registry_regex_consumer
toka build
./target/debug/registry_regex_consumer
```

It resolves `official/regex` from the public catalog and records the exact
archive digest in `package.lock`.

## 1.1 Give the coding agent the release-matched language card

Before the first Toka edit, provide the coding agent with the installed
[AI Completion Card](docs/ai_completion_card_v0.1.md). It gives compact,
versioned syntax and semantic patterns for mutation, borrowing, shared field
authority, and `cede`; it does not override the compiler or a package contract.

Use this order for a focused edit:

1. load the release-matched completion card once;
2. inspect the relevant declaration with `toka index --json`;
3. make the smallest edit;
4. use `toka check --json` and a focused evidence command when ownership or
   permissions are involved; and
5. run the package qualification command.

For a continuing project, retain at most one or two relevant, compiler-verified
code examples. Do not keep raw failed compiler output or an unbounded chat
history as the agent's memory.

## 2. Define a narrow package boundary

Before writing implementation code, record:

1. the upstream library and license to be respected;
2. one supported, user-visible capability;
3. explicit non-goals for this first release;
4. the safety, native, or platform boundary; and
5. one repeatable qualification command.

Use [`examples/official_package_v1`](examples/official_package_v1) as the
starting shape. Its static `package.tk` and `AI_CONTRACT` are the template for
an ecosystem package. The full package contract is
[`docs/official_package_v1.md`](docs/official_package_v1.md).

Do not add a second manifest format, dynamic manifest code, or an unreviewed
native build command. `package.tk` remains static data.

## 3. Work in small compiler-verified steps

After each meaningful edit, run the narrowest relevant compiler query before
running the project test:

```sh
toka check --json path/to/file.tk
toka index --json path/to/file.tk
toka evidence --json path/to/file.tk
toka cede-obligations --json path/to/file.tk
toka capabilities --json path/to/file.tk
```

Use `index` to read the declared API contract before changing a caller. Use
`evidence`, `cede-obligations`, or `capabilities` only when the failure concerns
ownership, transfer, borrowing, or a requested mutable call. They are facts
reported by the compiler, not permission to bypass a rejected program.

`toka test` is optional Preview feedback only; it is not the package's
qualification command. Run the package's documented focused test command and,
before release, verify a consumer can resolve the lock offline:

```sh
toka test # optional Preview scanner
# package's documented qualification command
toka build
TOKA_OFFLINE=1 toka build
```

## 4. Four mistakes that invalidate a port

- In Toka, an ordinary name is a payload view and a hat exposes handle
  identity. `*p` is not C-style dereference syntax.
- A declared `cede` contract is an obligation on both sides: the caller must
  transfer explicitly, and the callee must consume, store, forward, or return
  that transfer.
- A use-site `#` cannot exceed the permission declared by a parameter, field,
  or receiver. Inspect the contract instead of adding markers until it compiles.
- A clean compile is not a release. Keep the capability, non-goals, native
  assumptions, tests, and consumer import path consistent with one another.

## 5. Publish through the current public path

`toka publish` creates a package archive in the current project. It does not
upload an arbitrary package to the public registry.

The v1 public path is intentionally reviewable:

1. tag the exact package source in its own GitHub repository;
2. create a GitHub Release containing the archive from `toka publish`;
3. compute and record the archive SHA-256; and
4. submit the reviewed static catalog entry to
   [`tokalang/toka-registry`](https://github.com/tokalang/toka-registry).

Finally create a clean consumer project, run `toka add <name>`, build it, and
replay it with `TOKA_OFFLINE=1`. A registry entry is not complete until this
consumer path succeeds.

## 6. Ask for help with evidence

Open an issue using the repository's developer-experience or bug template when
the documented flow fails. Include the exact SDK version, host and architecture,
the command, a minimal source or manifest, and the output of `toka doctor`.
For changes to Toka itself, follow [`CONTRIBUTING.md`](CONTRIBUTING.md).
