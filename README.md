[Website (tokalang.dev)](https://tokalang.dev) | [Try Toka Online (Playground)](https://tokalang.dev/playground) | [v0.9.9-01 Release Notes](docs/release_notes_v0.9.9-01.md) | [Read the Paper](https://arxiv.org/abs/2606.01974) | [中文](README_zh.md)

# Toka Programming Language

**Toka is a no-GC systems programming language that keeps performance and static safety as baselines while making real systems semantics explicit and everyday code easier to read, write, and maintain.**

## Design Goal

### Baselines

Toka starts from two baselines:

- **Zero-cost performance:** low-level representation and resource costs should remain predictable, without a GC or hidden runtime layer becoming the default answer.
- **Strong static safety:** dangerous paths should be explicit enough for the compiler to check, with safety treated as a requirement rather than a convenience feature.

With those two baselines kept in place, Toka asks how close systems code can get to simple, direct, and maintainable expression without hiding the semantics that make systems programming hard. Source code should match the programmer's intent and the program's actual behavior: ownership transfer, mutation, rebinding, nullability, error propagation, async suspension, and low-level representation should be visible where they matter, statically checkable, and still compact in everyday code.

Toka does not pursue minimal syntax for its own sake. It pursues the minimum surface needed to expose real systems semantics. Each piece of surface complexity should earn its place by making a boundary visible that would otherwise become implicit, distant, or harder to audit.

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

Toka therefore explores a position between C, Rust, Go, and Zig: close to the machine, statically disciplined, and designed to keep everyday systems code readable without turning important systems boundaries into convention.

**Paper:** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## Quick Start

Install the latest release:

```bash
curl -fsSL https://tokalang.dev/install.sh | bash
```

Build from source:

Requires CMake, a C++17 compiler, and LLVM 20.

```bash
git clone https://github.com/tokalang/toka.git
cd toka
cmake -S . -B build
make -C build -j8
```

Check the compiler and project manager:

```bash
tokac --version
toka --version
```

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

## Core Ideas

### Explicit Resource Semantics

Toka has no garbage collector. Managed resources are controlled with deterministic destruction, move/transfer semantics, `@encap` lifecycle boundaries, and explicit `clone` / `drop` contracts. Resource transfer across an ownership boundary is written with `cede`.

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
| `?` | Nullable type state |
| `&` | Borrow/reference handle |
| `*` | Raw pointer handle |
| `^` | Unique owning handle |
| `~` | Shared owning handle |
| `'T` | Morphic generic parameter that preserves handle shape |

### Explicit Control Flow And Error Propagation

Control-flow costs are intended to stay visible. `async` marks yielding functions, `.await` marks suspension points, and postfix `!` propagates `Result` / `Option` failure paths without hiding the early return.

### Native Tooling

The `toka` CLI supports project workflows such as `toka new`, `toka run`, `toka build`, package resolution, and build orchestration through `package.tk` / `build.tk`. The compiler also emits dependency metadata used by the incremental build path. `tokalsp` provides diagnostics, hover, definition, references, completion, and rename over standard LSP transport; see [LSP support](docs/lsp.md). Machine-facing diagnostics, fixes, explanations, and bounded context are documented in [AI tooling](docs/ai_tooling.md).

## Current Status

Toka is under active development. The repository currently contains:

- A C++ compiler frontend with an LLVM 20 backend.
- Semantic analysis for mutability, moves, borrows, null access, resource safety, morphic generics, and related diagnostics.
- A standard library with core containers and system-level modules.
- The `toka` project manager / build tool, `tokafmt`, and `tokalsp`.
- Incremental build metadata and TKI interface cache validation.
- Linux and macOS as the supported 1.0 release platforms. Windows/MSYS2, WSL2, and WASI remain available or experimental and do not block 1.0.

The language is not yet self-hosted. The ecosystem is young. The immediate engineering priority is closing and verifying the 1.0 language, compiler, same-version interface, and core runtime contracts. Windows parity and eventual self-hosting remain later work.

## Is Toka A Good Fit?

Toka may be interesting if you want:

- No-GC systems programming with deterministic resource cleanup.
- Low-level representation control without making raw pointers the default API style.
- Static resource-flow checks without explicit lifetime parameters.
- A language where mutation, rebinding, nullability, and resource transfer are visible at the call site.
- A compact toolchain for experiments, systems tools, runtimes, and infrastructure code.

Toka may not be the right choice yet if you need:

- A large production ecosystem comparable to Rust, Go, Java, or C++.
- Long-term language stability guarantees.
- Turnkey native Windows production deployment.
- A drop-in replacement for an existing C/C++/Rust codebase.

## Documentation And Resources

- Website: [tokalang.dev](https://tokalang.dev)
- Playground: [tokalang.dev/playground](https://tokalang.dev/playground)
- Paper: [arXiv:2606.01974](https://arxiv.org/abs/2606.01974)
- Syntax reference: [docs/syntax.md](docs/syntax.md)
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
  note         = {Version 0.9.9-01}
}
```
