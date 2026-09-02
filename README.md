[Website (tokalang.dev)](https://tokalang.dev) | [Quick Start](#quick-start) | [RC12 Public Preview](docs/release_notes_v1.0.0-rc.12.md) | [Discussions](https://github.com/tokalang/toka/discussions) | [Support](SUPPORT.md) | [AI Completion Card](docs/ai_completion_card.md) | [AI Package Replication Guide](AGENTS-USER.md) | [Read the Paper](https://arxiv.org/abs/2606.01974) | [中文](README_zh.md)

# Toka systems programming language

**Toka is a no-GC systems programming language built around predictable resource costs, static safety, and AI-verifiable semantics. It makes real systems boundaries explicit for both programmers and tools.**

The canonical project name is **Toka systems programming language**, maintained
at `tokalang/toka`. It is unrelated to the historical Toka Forth projects,
Tokelang, and other similarly named Toka or Toke projects.

Deterministic cleanup · PAL static checks · explicit `cede` transfer · versioned JSON semantic protocols

## Design Goal

### Baselines

Toka starts from three baselines:

- **No-GC, predictable resource costs:** low-level representation and resource costs should remain predictable, without a GC or hidden runtime layer becoming the default answer.
- **Strong static safety:** dangerous paths should be explicit enough for the compiler to check, with safety treated as a requirement rather than a convenience feature.
- **AI-verifiable semantics:** important compiler conclusions should be available as stable, machine-readable facts. An AI-assisted repair must be able to identify the relevant contract, make a minimal change, and verify the result against the compiler and targeted tests.

With those three baselines kept in place, Toka asks how close systems code can get to simple, direct, and maintainable expression without hiding the semantics that make systems programming hard. Source code should match the programmer's intent and the program's actual behavior: ownership transfer, mutation, rebinding, nullability, error propagation, async suspension, and low-level representation should be visible where they matter, statically checkable, and still compact in everyday code.

Toka does not pursue minimal syntax for its own sake. It pursues the minimum surface needed to expose real systems semantics. Each piece of surface complexity should earn its place by making a boundary visible that would otherwise become implicit, distant, or harder to audit.

This is also an AI-assisted development requirement: the same ownership,
capability, async, and interface boundaries that programmers read should be
available to tools as deterministic compiler evidence. Toka does not treat AI
as a text generator outside the language; it treats AI-assisted editing as a
consumer of the language's semantic contracts.

### The Tension

In today's mainstream language landscape, this combination often looks like an impossible triangle:

- near-machine performance
- strong safety
- clear, maintainable expression

Toka is an attempt to challenge that tension and move as close as possible to the ideal point. Its compactness is not meant to erase low-level behavior; it is meant to keep that behavior explicit at the point where it matters.

A possible extra payoff is optimization visibility. When resource flow, aliasing, and representation are explicit, the compiler may see opportunities that are harder to expose in conventional C or Rust code. In some performance-sensitive workloads, that can mean better register use, clearer alias boundaries, and the possibility of outperforming an equivalent hand-written formulation. This is a potential consequence of the design, not a blanket performance claim.

### The Approach

Toka combines several mechanisms toward that goal. The design tries to keep everyday code readable while still giving resource, representation, and safety boundaries a precise place in the language:

- explicit resource semantics and deterministic cleanup
- PAL (Path-Anchored Ledger) static checking for borrow validity and resource-contract safety
- compact markers for mutation, rebinding, transfer, nullability, and handle identity
- integrated project tooling instead of large external build-system setup
- a payload/handle distinction where ordinary names operate on object payloads, while hats such as `&`, `*`, `^`, and `~` expose or preserve handle identity only when the code genuinely needs that layer

The hat syntax is one consequence of this design, not the goal itself. It exists because Toka needs a compact, consistent way to distinguish payload operations from handle operations.

**Design-lineage note.** Toka's hat syntax was developed independently; related
mechanisms in C, Cforall, and Alusus are acknowledged as precedents. Toka does
not claim the individual glyphs, multi-level references, or explicit handle
selection as inventions. Its design focus is the integration of hat forms with
payload/handle selection, ownership, borrowing, rebinding, and resource
contracts. See [Design lineage and evidence](docs/design_lineage.md).

Toka therefore explores a position between C, Rust, Go, and Zig: close to the machine, statically disciplined, and designed to keep everyday systems code readable without turning important systems boundaries into convention.

**Paper:** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## Quick Start

Toka is currently a public preview. Use an exact published release candidate
for a repeatable install:

```bash
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.12
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```

The prebuilt SDK still uses host tools for project orchestration and native
linking. It requires Python 3.10+ and a C linker. On Ubuntu/Debian, install the
complete runtime prerequisites with:

```bash
sudo apt-get install clang lld python3 pkg-config libssl-dev
```

`toka doctor` checks these runtime requirements before declaring the SDK ready.

Before replacing the tag, check the
[GitHub releases page](https://github.com/tokalang/toka/releases). The bare
installer intentionally follows GitHub's stable-release selector and is not
the recommended public-preview path.

Build from source to contribute to the compiler or test unreleased changes.
This requires CMake, a C++17 compiler, and LLVM 20:

```bash
git clone https://github.com/tokalang/toka.git
cd toka
cmake -S . -B build
cmake --build build
export PATH="$PWD/build/bin:$PATH"
export TOKA_LIB="$PWD/lib"
```

Create a project and add [TokaKV](https://github.com/tokalang/tokakv), the
official embedded key-value engine:

```bash
toka new tokakv_hello
cd tokakv_hello
toka add tokakv
```

Replace `src/main.tk` with this complete example:

```toka
import std/io::{println}
import official/tokakv::{TokaKvEngine}

fn main() -> i32 {
    auto db = TokaKvEngine::open(string::from("hello.tokakv")).unwrap()
    db.put(string::from("language"), string::from("Toka")).unwrap()

    auto value = db.get(string::from("language")).unwrap().unwrap()
    println("{}", value)

    db.close().unwrap()
    return 0
}
```

Then run it:

```bash
toka run
```

The expected final output is `Toka`. This flow was verified from a clean RC10
SDK against the public `tokakv` package. The example uses `unwrap()` to stay
compact; production code should handle storage and I/O errors explicitly.

The resolver uses `https://pkg.tokalang.dev` by default and verifies the
catalog-recorded SHA-256 before extracting an archive. Set `TOKA_REGISTRY_URL`
only to use a local or test registry.

If your goal is to help grow the ecosystem with an AI-assisted port, start
with the [AI Package Replication Guide](AGENTS-USER.md). It defines the
supported package-release path and the compiler checks to run after each edit.

## The Mental Model

Toka is easiest to understand if you separate two layers:

| Layer | What It Means | Typical Syntax |
| :--- | :--- | :--- |
| Payload / Soul | The object content you read, write, pass, or pattern-match | `x`, `x.field`, `x = value` |
| Handle / Representation | The way an object is reached, owned, shared, borrowed, or rebound | `&x`, `*x`, `^x`, `~x`, `*x = *y` |

This is the opposite of the C habit that treats `*p` as "the value behind p". In Toka:

| Intention | Toka Form | Meaning |
| :--- | :--- | :--- |
| Read or write the object | `p` / `p = value` | Operate on the payload |
| Inspect or move the pointer-like identity | `*p`, `^p`, `~p`, `&p` | Operate on the handle |
| Rebind a handle | `*p = *q` | Make the handle point somewhere else |
| Mutate a payload binding | `x#` | The payload can be written |
| Rebind a handle binding | `*#p`, `^#p` | The handle itself can be replaced |
| Transfer a resource contractually | `cede x` | The caller gives up the resource path |

Function parameters follow the same rule. For ordinary object parameters, Toka uses logical in-place capture: write `x: T` or `x#: T` when the function wants the payload view. Add a hat to a parameter only when the function genuinely needs the handle itself, for example to inspect, forward, or rebind that handle.

```toka
shape Resource(val: i32)

fn keep(cede r: Resource) -> Resource {
    return cede r
}

fn main() -> i32 {
    auto r = Resource(val = 42)
    auto moved = keep(cede r)

    if moved.val != 42 {
        return 1
    }
    return 0
}
```

The `cede` in the signature is not just permission. It is an obligation: the function body must explicitly consume, forward, store, return, or otherwise complete that resource transfer.

## Why Toka Exists

Most mainstream systems languages combine three concerns in different ways:

| Language Family | Strength | Tradeoff Toka Tries To Address |
| :--- | :--- | :--- |
| C | Direct representation control and predictable ABI | Safety depends heavily on convention |
| C++ | RAII, generic abstraction, low-level control | Many ownership and aliasing rules remain implicit |
| Rust | Strong memory safety without GC | Lifetime and borrow reasoning can become a major surface concern |
| Go / Java / C# | Productive everyday development and large ecosystems | GC and runtime model reduce low-level predictability |
| Zig / Odin | Simple systems-oriented control | Resource and aliasing discipline is largely programmer-managed |

Toka's bet is that **access representation deserves its own source-level dimension**. Instead of hiding pointer identity, ownership, sharing, borrowing, nullability, mutability, and transfer intent behind one overloaded variable notation, Toka gives these concepts small orthogonal markers and lets the compiler enforce the resulting contracts.

## Why Toka When Rust Already Exists?

Rust demonstrated that a practical systems language can provide strong memory
safety without making garbage collection the default. Toka builds on that
achievement rather than treating Rust as a failed design or positioning itself
as a drop-in replacement.

The two languages explore different ways of distributing systems-programming
complexity. Rust provides a general lifetime and trait system capable of
expressing advanced borrowing patterns. Toka instead favors local path-based
inference, narrower cross-boundary dependency contracts, and conservative
rejection when PAL cannot establish safety locally.

| Area | Rust | Toka |
| :--- | :--- | :--- |
| Borrow relationships | Lifetimes are often inferred or elided; named lifetime parameters express relationships that cannot be omitted | PAL infers local relationships; escaping borrowed values use path-oriented `<-` / `effects:` contracts rather than named lifetime variables |
| Safety tradeoff | Can express highly general borrowing patterns, sometimes with substantial type-level complexity | Intentionally accepts fewer difficult borrowing patterns in exchange for a smaller ordinary source surface |
| Access representation | References, raw pointers, and owning pointer types are distinct types and commonly participate in dereference coercion | Payload operations and handle identity are separate source-level dimensions expressed through hat morphology |
| Ownership transfer | Moves follow Rust's ownership, type, and value-context rules | Transfer is governed by owning morphology and declaration contracts such as `cede`; a resolved `cede` formal is an ownership boundary even when caller-side `cede` spelling is omitted |
| Async address stability | `Pin` provides the low-level address-stability contract used by futures, while ordinary async code often hides it | PAL state remains active across suspension and ordinary source has no `Pin` construct; shape-internal self-reference is not supported in Toka 1.0 |
| Machine tooling | JSON diagnostics, Cargo metadata, rust-analyzer, and a mature tools ecosystem | Additional domain-specific protocols expose PAL decisions, transfer obligations, capabilities, and lifecycle contracts, but these interfaces and their ecosystem are much younger |
| Maturity | Stable language, large ecosystem, extensive production experience, and established formal and certification work | Public Preview, young ecosystem, evolving implementation, and substantially less production evidence |

Toka does not claim that lifetime relationships, address stability, aliasing, or
resource transfer can be made free. Its design moves some of that burden into
local compiler analysis, expresses some of it through different contracts, and
rejects some patterns that Rust can represent.

Toka may be worth evaluating when compact ownership-oriented source, explicit
payload/handle separation, predictable no-GC resource behavior, or
machine-readable semantic evidence are primary requirements. Rust remains the
stronger default when ecosystem breadth, long-term stability, certification,
platform coverage, or proven production deployment matters more than
experimenting with a different language model.

## Core Ideas

### Explicit Resource Semantics

Toka has no garbage collector. Managed resources are controlled with deterministic destruction, move/transfer semantics, `@Encap` lifecycle boundaries, and explicit `clone` / `drop` contracts. Resource transfer across an ownership boundary is written with `cede`.

### Payload-Handle Separation

The language distinguishes the object being used from the handle by which it is reached. This makes code such as `*p = *q` meaningful as handle rebinding, while plain `p = q` remains payload assignment.

### PAL (Path-Anchored Ledger) Static Checking

PAL (Path-Anchored Ledger) is Toka’s compile-time resource-safety checker. It records borrow, ownership-transfer, and invalidation facts against source-level storage paths, and rejects operations that would make an active borrow or ownership contract invalid. The goal is compile-time safety without user-written lifetime parameters such as Rust's `<'a>`.

PAL is governed by four core rules:
1. **Unique ownership is exclusive:** A `^` resource is owned by one valid handle at any time.
2. **Transfer is explicit:** Ownership handoff must be syntactically visible. Direct hatted unique-handle moves are visible transfer syntax; `cede` is required for declared cede contracts and explicit cede handoff paths, and any transfer obligation must be fulfilled.
3. **Borrow validity is protected:** Operations that can invalidate an active borrow (such as moves, `cede`, drops, handle rebinding, or reallocations) are rejected.
4. **Exclusive mutation requires exclusive permission:** Exclusive/mutable borrows conflict with other overlapping active borrows. A standard immutable borrow is intended as a read-only view for that borrow path rather than a global freeze promise; the current checker remains conservative around overlapping payload writes until mutation classes are fully separated.

PAL is intended to be conservative before it is permissive. Toka does not trade safety away for a lighter surface; when an ownership or borrowing relationship cannot be proven locally without exposing a full lifetime calculus to the user, the compiler should reject the pattern or require a more explicit structure. This may accept fewer extreme-but-safe borrowing programs than Rust, but it keeps the safety line high while reducing the proof burden in ordinary code.

### Orthogonal Surface Markers

Toka uses a compact token system:

| Token | Role |
| :--- | :--- |
| `#` | Mutability for a payload binding or rebinding authority for a handle binding |
| `?` | Removed nullable payload spelling (E0484) |
| `&` | Borrow/reference handle |
| `*` | Non-zero raw pointer handle |
| `nul *` | Raw pointer handle whose physical address may be zero |
| `^` | Unique owning handle |
| `~` | Shared owning handle |
| `'T` | Morphic generic parameter that preserves handle shape |

### Explicit Control Flow And Error Propagation

Control-flow costs are intended to stay visible. `async` marks yielding functions, `.await` marks suspension points, and postfix `!` propagates `Result` / `Option` failure paths without hiding the early return.

### Native and AI Tooling

The `toka` CLI supports project workflows such as `toka new`, `toka run`, `toka build`, package resolution, and build orchestration through `package.tk` / `build.tk`. The compiler also emits dependency metadata used by the incremental build path. `tokalsp` provides diagnostics, hover, definition, references, completion, and rename over standard LSP transport; see [LSP support](docs/lsp.md). The [Toka VS Code extension](https://github.com/tokalang/toka-vscode) is maintained in its own repository.

Beyond diagnostics, Toka exposes versioned machine-readable semantic protocols.
They currently cover public compiler decisions, `cede` transfer obligations,
TaskHandle lifecycle contracts, and H/P call-capability decisions. These facts
let tools distinguish a missing transfer from an unconsumed parameter, explain
why a payload write is denied, and select lifecycle redlines for an async edit.

```text
semantic context -> compiler evidence -> minimal edit -> compiler re-check -> targeted redline tests
```

```bash
toka evidence --json main.tk
toka cede-obligations --json main.tk
toka capabilities --json main.tk
```

Machine-facing diagnostics, semantic evidence, and bounded context are
documented in [AI tooling](docs/ai_tooling.md). The protocols are explanation
and verification interfaces, not a promise that any particular model will
write correct code without review.

## RC12 Status And Boundaries

Toka `v1.0.0-rc.12` is a published **Public Preview** release candidate, not a
stable 1.0 compatibility promise. The 1.0 language semantics are frozen during
this stabilization phase: current work is documentation, ecosystem adoption,
qualification, and bug fixing rather than new language features.

| Platform | RC12 status |
| :--- | :--- |
| Linux x86_64 | Published Tier 1 SDK archive |
| Linux aarch64 | Published Tier 1 SDK archive |
| macOS x86_64 | Published Tier 1 SDK archive |
| macOS aarch64 / Apple Silicon | Published Tier 1 SDK archive |
| Windows / MSYS2 | Source-build and dogfood path; no RC12 SDK archive |
| WSL2 / WASI | Available or experimental; not a 1.0 blocking release target |

Known boundaries:

- RC12 is a prerelease; source, package, and interface compatibility may still
  change before stable 1.0.
- The language is not yet self-hosted, and the package ecosystem is young.
- TokaKV is an embedded, single-process preview engine. Its current compaction
  scope is L0-to-L1; deeper levels, distributed replication, and a Redis
  protocol server are not included.

The repository currently contains:

- A C++ compiler frontend with an LLVM 20 backend.
- Semantic analysis for mutability, moves, borrows, null access, resource safety, morphic generics, and related diagnostics.
- A standard library with core containers and system-level modules.
- The `toka` project manager / build tool, `tokafmt`, and `tokalsp`.
- Incremental build metadata and TKI interface cache validation.
- Linux and macOS as the supported 1.0 release platforms.

The immediate priority is to make the frozen RC12 surface easier to evaluate:
clear documentation, reproducible examples, ecosystem proof such as TokaKV,
and release qualification. Windows parity and eventual self-hosting remain
later work.

## Is Toka A Good Fit?

Toka may be interesting if you want:

- No-GC systems programming with deterministic resource cleanup.
- Low-level representation control without making raw pointers the default API style.
- Static resource-flow checks without explicit lifetime parameters.
- A language where mutation, rebinding, nullability, and resource transfer are visible at the call site.
- A compact toolchain for experiments, systems tools, runtimes, and infrastructure code.
- AI-assisted systems development where ownership, authority, and lifecycle constraints must be compiler-explainable and independently verifiable.

Toka may not be the right choice yet if you need:

- A large production ecosystem comparable to Rust, Go, Java, or C++.
- Long-term language stability guarantees.
- Turnkey native Windows production deployment.
- A drop-in replacement for an existing C/C++/Rust codebase.

## Documentation And Resources

- Website: [tokalang.dev](https://tokalang.dev)
- Website source: [tokalang/toka-web](https://github.com/tokalang/toka-web)
- Paper: [arXiv:2606.01974](https://arxiv.org/abs/2606.01974)
- Syntax reference: [docs/syntax.md](docs/syntax.md)
- AI completion card: [docs/ai_completion_card.md](docs/ai_completion_card.md)
- Build tool notes: [docs/BUILD_TOOL.md](docs/BUILD_TOOL.md)

## Ecosystem And Community

- [**toka-book**](https://github.com/lumicore-dev/toka-book) ([Read Online](https://lumicore-dev.github.io/toka-book)): A comprehensive, community-driven guide to learning Toka.

*(Built something cool with Toka? Open a PR to add your project here!)*

## Relationship To Other Languages

Toka is influenced by C and C++ for representation control and deterministic resource management, by Rust for compile-time memory-safety discipline, by ML-family languages for algebraic data and trait-style abstraction, and by scripting languages for concise everyday syntax. Its distinctive contribution is not claiming to replace those languages wholesale; it is the explicit separation of payload semantics from handle semantics as a source-level design principle.

---

## Citation

If you reference the design of the Toka language, including its explicit Hat-Soul resource model, PAL borrowing discipline, compile-time reflection facilities, and Shape-based data model, please cite both our paper and repository as follows:

### Academic Paper (arXiv)

```bibtex
@misc{yi2026toka,
  title={{Toka}: A Systems Programming Language with Explicit Resource Semantics},
  author={Yi, Zhonghua},
  year={2026},
  eprint={2606.01974},
  archivePrefix={arXiv},
  primaryClass={cs.PL},
  doi={10.48550/arXiv.2606.01974}
}
```

### Software Repository

```bibtex
@misc{toka_language,
  author       = {Yi, Zhonghua and {Toka Language Contributors}},
  title        = {{Toka} Programming Language},
  howpublished = {GitHub repository},
  url          = {https://github.com/tokalang/toka},
  year         = {2025--2026},
  note         = {Version 1.0.0-rc.12 public preview}
}
```
