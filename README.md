[Website (tokalang.dev)](https://tokalang.dev) | [Try Toka Online (Playground)](https://tokalang.dev/playground) | [Read the Paper](https://arxiv.org/abs/2606.01974) | [中文](README_zh.md)

# Toka Programming Language

**Toka is a modern systems programming language without GC. It combines C-style performance and low-level control with Rust-inspired memory-safety discipline, while aiming for a clean and productive developer experience reminiscent of scripting languages.**

📖 **Academic Paper:** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## 🚀 "Show, Don't Tell"

Say goodbye to the cognitive load of explicit `<'a>` lifetime annotations and complex CMake setups. In Toka, high-performance systems code is designed to stay explicit, readable, and disciplined:

```toka
// Everyday Toka: physical-level concurrency, minimalist RAII, and native error propagation
import std/io::println
import std/net::TcpStream
import stdx/net/websocket

// `async` explicitly colors the function; `stream#` lowercase variable with `#` indicates its underlying state will be mutated
fn handle_client(stream#: TcpStream) -> async Result<(), String> {
    // Deep automatic deallocation, no implicit copies, goodbye GC pauses
    auto ws_conn# = websocket::accept_async(stream).await!
    
    // The `!` in `.await!` is Toka's native error propagation operator.
    // Yield control if blocked (await), bubble up instantly on error (!), eliminating verbose `if err` checks.
    ws_conn#.write_text_async("Hello from Toka!").await!
    
    // As it leaves scope, the RAII system silently and safely destroys all sockets and memory.
    return Ok(())
}
```

> **Try it instantly in 1 second:**
> ```bash
> curl -fsSL https://tokalang.dev/install.sh | bash
> ```

---

## 🎯 Is Toka For You?

To respect your time, please verify if Toka fits your needs before diving in:

**✅ It is an excellent choice if:**
*   You need extreme runtime performance and minimal memory footprint (e.g., high-concurrency gateways, game backends, system-level tools).
*   You are tired of Go/Java's GC pauses (STW), but also exhausted by **endlessly fighting the compiler** in Rust, or being forced to take detours just to implement what should be a completely natural logic flow.
*   You want a productive development experience while keeping low-level safety and control close to the machine.

**❌ It might NOT be for you yet if:**
*   Your production environment mandates pure Windows Server deployment (Native Windows support is under active development; WSL2/macOS/Linux are currently recommended).
*   You require massive out-of-the-box business-level ecosystem frameworks (like Spring Boot) — Toka's current ecosystem is more tailored toward systems-level tooling and infrastructure.

---

## 💎 Core Pillars

### 🛡️ Explicit Resource Safety Without Lifetime Annotations
Pioneering the **Single-Hat Principle** and the **PAL (Pointer Aliasing & Lifecycle) Checker**, Toka makes ownership, borrowing, pointer identity, and mutation intent visible in the source language. The goal is strong compile-time resource safety without explicit lifetime parameters.

### ⚡ Zero-Cost Abstraction & Native C Interop
Built on **LLVM 20**, Toka has **No Garbage Collection (No GC)**. Its native layout and FFI-oriented design are intended to make interop with the existing C ecosystem (like SQLite or OpenGL) direct and predictable.

### 🚦 Transparent Concurrency & Error Flow
No implicit "black magic schedulers." Toka makes function costs physically explicit: standard functions and `async` (yielding) functions have clear color boundaries. Paired with the native `!` error propagation operator, error handling and async scheduling stay visible in the function signature and call site.

### 📦 Integrated Modern Tooling
Toka includes the `toka` CLI for project workflows such as `toka run`, `toka build`, and package resolution based on `package.tk` / `build.tk`, keeping everyday build steps close to the language.

---

## 🌟 Deep Dive: Attribute Tokens & Soul-Identity Duality

Toka eliminates hidden states by making memory properties explicit through **orthogonal suffix tokens**. Developers can instantly understand the underlying memory structure and lifecycle just by glancing at a variable definition.

### Key Innovations

**1. Syntactic RAII (Native Smart Pointers)**
Toka elevates **Smart Pointers to Core Syntax**. "Lifecycle Management" is internalized as the language's breath.
*   **^T (Unique)** and **~T (Shared)** exist as native grammatical constructs, making RAII the zero-visual-cost default pattern.

**2. Soul-Identity Duality**
Toka philosophically separates an object into its **Soul (Content/Value)** and **Identity (Extension/Address)**.
*   **Precise Semantics**: Access the Soul via Borrow (`&`) and manipulate the Identity via Pointer (`*`), completely eliminating the semantic ambiguity between "reference assignment" and "content modification" found in traditional languages.

**3. Orthogonal Decoupling of Mutability and Nullability**
Say goodbye to the keyword soup of `const volatile unsigned`. Toka strictly defines attribute ownership:
*   **Mutability is a property of the variable (`#`)**: Represents identity transfer or content modification.
*   **Nullability is a property of the type (`?`)**: Represents the potential non-existence of the entity in memory.
*   **Visual Formula**: declarations such as `auto ^p = new Node(...)`, `auto x# = 1`, or the rare full form `auto nul ^#node#: Node? = null` expose ownership, mutability, and nullability at the syntax level.

**4. @encap Explicit Encapsulation & Resource Safety**
Toka makes normal `shape` members transparent by default, but heavily restricts resource-holding structs (like Files or Sockets) using `@encap`.
*   **Permission & Lifecycle Binding**: Inside an `impl Type@encap` block, you must explicitly expose interfaces (`pub`). More importantly, this is the *only* legal place to define lifecycle methods like `drop`, fundamentally preventing resource tampering and memory leaks at the contract level.

**5. Explicit Control Flow & Error Propagation**
Toka favors explicit control-flow contracts: `match` is checked for exhaustive handling, `guard` makes null and none checks visible, and the postfix `!` operator propagates `Result` / `Option` failures without hiding the early-return path.

### Token System Table

| Token | Meaning (Value/Content) | Meaning (Identity/Address) |
| :--- | :--- | :--- |
| `#` | **Writable**: Can modify fields | **Swappable**: Can point elsewhere |
| `?` | **Nullable**: Value is `none` | **Nullable**: Identity is `null` |
| `!` | **Writable & Nullable** | **Swappable & Nullable** |
| `^` | - | **Unique Pointer** (Ownership) |
| `~` | - | **Shared Pointer** (Ref Counted) |
| `&` | **Borrow**: View into Value | **Reference**: View into Soul |
| `*` | - | **Raw Pointer** (No Ownership) |

---

## ✅ Project Status (Roadmap)

Toka currently possesses a complete language core and standard library. We are actively building the compiler's self-hosting capabilities.

### 🚀 Completed Core Features
- [x] **Native Cross-Platform Support** 
    - [x] **Linux / macOS**: First-class support fully established.
    - [x] **Windows**: Newly added support for MinGW/MSYS2 ABI, with core system and filesystem calls stabilized.
- [x] **Compiler Infrastructure** (Lexer, Parser, LLVM 20 IR CodeGen)
- [x] **Type System** (Primitive Types, Five-State Shape System, ADTs, Pattern Matching)
- [x] **Memory Management** (Unique/Shared Smart Pointers, Move Semantics, Deep Drop, Pointer Rebinding)
- [x] **Object-Oriented Features** (`impl` blocks, Trait System)
- [x] **Control Flow Expressions** (Value-yielding loops, targeted `break`/`continue` labels)
- [x] **Modules & Visibility** (Physical/Logical imports, fine-grained `pub` modifiers)
- [x] **Semantic Analysis (Sema)**
    - [x] Strict Mutability Enforcement (`#` Check)
    - [x] Ownership & Borrowing Verification (Move Semantics)
    - [x] Explicit Null Safety (`is` operator)
    - [x] Resource Safety Analysis (Enforced `@encap/drop` for resources)
- [x] **Advanced Features**
    - [x] **Generics / Templates** (Rigid and Morphic type deduction)
    - [x] **Concurrency** (OS Threads, Mutex, MPSC Channels, `async`/`await`)
    - [x] **Standard Library** (Sys I/O, Sockets, and core containers `String`/`Vec`/`Option`/`Result`)
- [x] **Developer Experience (DX) & Tooling**
    - [x] **Built-in Build System** (`toka run`, `toka build`)
    - [x] **Language Server (LSP)** (Official `tokalsp` integrated with VS Code for real-time diagnostics and highlighting)
    - [x] **Package Manager** (`toka` CLI with `package.tk` dependency resolver and global registry gateway)
    - [x] **Native Kebab-case Identifiers** (Syntactic fusion of Lisp-like naming with strict spacing rules)

### 🚧 Next Steps & Future Roadmap
- [ ] **Windows Platform Parity** (Native IOCP async networking stack support)
- [ ] **Compiler Self-Hosting** (Rewriting the current C++ frontend entirely in Toka)

## 🌟 Ecosystem & Community

We are thrilled to see the community building awesome things with Toka! Check out some of these community-driven projects:

- [**toka-book**](https://github.com/lumicore-dev/toka-book) ([Read Online](https://lumicore-dev.github.io/toka-book)): A comprehensive, community-driven guide to learning Toka, inspired by "The Rust Book".

*(Built something cool with Toka? Open a PR to add your project here!)*

## 📚 Documentation & Resources

For the latest **Installation Guides**, **Tutorials**, **Code Examples**, and **API References**, please visit the official Toka language website:

👉 **[tokalang.dev](https://tokalang.dev)**

> Note: To ensure documentation consistency and prevent outdated information, all usage instructions, examples, and deep-dive technical articles have been centralized on our official website.

---

## Inspirations & Lineage

Toka is a modern systems programming language built on the shoulders of giants. It was born not to reinvent the wheel, but to explore the optimal balance between **high performance** and **developer experience**, building upon the wisdom of its predecessors.

### 1. C & C++ (Modern C++)
**Core Inspiration: Systems Control, Smart Pointers & RAII**
Toka's memory management mechanism is a direct descendant of Modern C++ (C++11/14+). Toka bakes the semantics of `std::unique_ptr` and `std::shared_ptr` directly into its **core prefix grammar** (`^T` / `~T`), making RAII a zero-visual-cost default standard.

### 2. Rust
**Core Inspiration: The Borrowing Discipline & Memory Safety**
Rust proved that memory safety without garbage collection is achievable. Toka is inspired by Rust's strict borrowing discipline, but it explores a different surface syntax: explicit resource morphology, PAL checks, and dependency routing instead of user-written lifetime parameters such as `<'a>`.

### 3. Haskell / ML Family
**Core Inspiration: Typeclasses & Orthogonality**
Toka's Trait system spiritually inherits from Haskell's Typeclasses. We champion the complete decoupling of "Data Definition" (`shape`) from "Behavior Implementation" (`impl`).

### 4. Swift / Kotlin / C#
**Core Inspiration: Null Safety**
To address "The Billion Dollar Mistake," Toka adopts **Explicit Null Safety**. Using the suffix modifier (`?`), null pointer checks are enforced at the type system level.

### 5. Python
**Core Inspiration: Readability & Productivity**
Toka strives to bring the developer experience of scripting languages to systems programming, aiming to make systems code as readable and approachable as high-level scripts.

### 6. Extended Gratitude
Programming language design is an endless exploration across a vast universe of ideas. Toka embraces the collective wisdom of the entire open-source community and maintains the utmost reverence for all pioneers who have pushed the boundaries of computer science forward.

---

## Citation

If you reference the design of the Toka language, including its explicit Hat-Soul resource model, PAL borrowing discipline, compile-time reflection facilities, and Shape-based data model, please cite both our paper and repository as follows:

### Academic Paper (arXiv)

```bibtex
@article{yi2026toka,
  title={{Toka}: A Systems Programming Language with Explicit Resource Semantics},
  author={Yi, Zhonghua},
  journal={arXiv preprint arXiv:2606.01974},
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
  note         = {Version 0.9.8-06}
}
```
