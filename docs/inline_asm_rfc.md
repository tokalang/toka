# RFC: Target-Scoped Inline Assembly and Portable CPU Primitives

- **Status**: Draft
- **Target release**: Experimental, post-1.0
- **Scope**: Language surface, compiler lowering, and standard-library layering

---

## 1. Decision

Toka will expose inline assembly as a target-scoped, unsafe language extension
with a direct, D-style source surface. It is not a function call, an FFI
declaration, or an LLVM/GCC constraint-string API.

```toka
auto value# = 4:i64

unsafe {
    asm aarch64 {
        mov x9, value
        add x9, x9, #1
        mov value, x9
    }
}
```

The primary public interface for CPU-specific behavior remains typed intrinsic
and standard-library APIs. Inline assembly is the target-specific substrate for
those APIs and a deliberately narrow expert escape hatch.

> **No Toka assembly dialect.** Inside an assembly block, instructions,
> registers, addressing forms, and labels use the selected target's established
> assembler dialect. Toka defines only the target selector, the mapping to
> in-scope Toka bindings, and the `unsafe` boundary; it does not introduce an
> operand-constraint language that users must learn.

This RFC does **not** add a general Zig-style compile-time programming system.
It adds only the target-selection facts needed to choose a target-specific
implementation at compilation.

## 2. Motivation

LLVM inline assembly needs complete information about inputs, outputs,
registers, and memory effects. Rust and Zig expose that backend model through
operand constraints. That model is precise, but it makes a simple transfer
between a Toka local and a fixed CPU register hard to read.

Toka should instead make the hardware data movement visible in source:

```toka
mov x9, value
mov value, x9
```

The compiler must recover the required lowering facts from this target-specific
syntax. Toka does not promise that a block using `x9` can compile for x86_64,
or that hand-written assembly is portable. Portability belongs at the API
layer, not inside an individual assembly block.

## 3. Non-goals

The first version does not provide:

- a cross-architecture virtual assembly language;
- arbitrary inline assembly accepted as an opaque string;
- user-written LLVM/GCC register constraints;
- calls, returns, stack-pointer changes, exception transfer, or jumps outside
  an assembly block;
- direct assembly support for resource owners, shape values, strings, slices,
  closures, or handle rebinding;
- runtime CPU-feature dispatch.

External native code remains a separate FFI/build capability. An external `.S`
file called through `extern fn` is not inline-assembly support and is not
specified here.

## 4. Surface syntax and lexical domain

### 4.1 Assembly statement

```text
AsmStmt := "asm" Architecture "{" AsmBody "}"
```

`asm` is a new keyword. `Architecture` is an architecture identifier such as
`aarch64` or `x86_64`. An `AsmStmt` is valid only within an existing
`unsafe { ... }` region.

The braces following `asm <architecture>` delimit an assembly lexical domain;
they are not a normal Toka statement block or a trailing closure. This is an
intentional, target-scoped exception to the ordinary meaning of a block. The
body uses the selected architecture's documented assembly dialect. The first
implementation uses AArch64 syntax:

```toka
unsafe {
    asm aarch64 {
        mov x9, value
        add x9, x9, #1
        mov value, x9
    }
}
```

The body is parsed only enough to identify instructions, registers, local
labels, operands, and Toka-local references; it is then lowered through LLVM's
target assembler. It is not a raw Toka string and cannot interpolate text.
In particular, Toka does not add Rust/GCC-style `in(reg)`, `inout`, `clobber`,
or `options(...)` spellings inside this body.

### 4.2 Toka local references

Within `AsmBody`, an identifier that resolves to a Toka local denotes that
local's scalar storage/value according to the selected architecture's operand
rules. A target register such as `x9` or `x10` is never resolved as a Toka
name. The compiler rejects an ambiguous or unresolved operand instead of
guessing.

For the initial AArch64 subset, supported Toka operands are integer scalars,
`bool`, and `Addr`. The compiler owns any required spill slot, reload, or
write-back; source code does not depend on a fixed stack layout.

An assembly write to a Toka binding is an ordinary Toka payload write. It
therefore requires existing mutation authority:

```toka
auto value# = 0:i64

unsafe {
    asm aarch64 {
        mov value, x9
    }
}
```

The same block with `auto value = 0:i64` is rejected. `unsafe` acknowledges
machine-level risk; it does not silently manufacture `#` authority.

### 4.3 Conservative effect model

Every initial assembly block is treated as:

- volatile / side-effecting;
- reading and writing arbitrary memory;
- invalidating ordinary condition-code assumptions; and
- using exactly the registers read or written by its accepted instruction set.

The compiler must preserve the target ABI. A block that writes a callee-saved
register must restore it before the block ends. The initial subset rejects
writes to stack/frame pointers and rejects instructions whose implicit register
or memory effects cannot be modelled. There is no user spelling for `clobber`,
`nomem`, `nostack`, or similar optimization claims in the first version.

This model is intentionally conservative. A later RFC may add narrowly
verified effect refinements only after object-code and optimizer evidence shows
the need.

## 5. Target selection

### 5.1 Compile-time target facts

The compiler already has a resolved target triple from `--target` and records
it in build/interface metadata. This RFC adds a public compile-time target
query, provisionally spelled `target::arch()`:

```toka
if target::arch() == "x86_64" {
    unsafe {
        asm x86_64 {
            pause
        }
    }
} else if target::arch() == "aarch64" {
    unsafe {
        asm aarch64 {
            yield
        }
    }
} else {
    cpu::spin_hint_fallback()
}
```

`target::arch()` is a compiler-known constant. Existing constant-condition
handling must select one branch before assembly semantic validation and code
generation. Unselected branches are still lexically well formed, but their
foreign architecture instructions are not validated against the active target.

An `asm x86_64` block in a selected non-x86_64 branch is a compile error. The
architecture spelling is kept even inside a target-selected branch: it states
the assembly dialect locally and catches accidental placement in the wrong
branch.

### 5.2 CPU features are not target selection

`target::arch()` answers what binary is being compiled, not what optional
instructions the eventual CPU supports. AVX2, AES, SVE, and similar features
require a separate feature contract and, when the deployment CPU is not fixed,
runtime detection plus a fallback implementation.

The standard library owns that dispatch. A future feature RFC may define the
function annotation and runtime query; this RFC only requires that an assembly
block needing an optional feature cannot silently enter a generic baseline
binary.

## 6. Standard-library and intrinsic layering

Applications should normally use a stable, target-neutral operation:

```toka
cpu::spin_hint()
```

The standard library may select an x86_64 `pause` block, an AArch64 `yield`
block, or a portable fallback. Similar APIs may later cover atomics, bit
operations, SIMD, cryptographic instructions, and CPU feature detection.

The public API contract, not the assembly spelling, is cross-platform. A
standard API must document its fallback and required CPU features.

## 7. Initial implementation slice

The experimental first slice is deliberately small. It starts on the native
development and Tier 1 macOS AArch64 target so behavior can be compiled and
executed locally; x86_64 follows after it has an equivalent execution gate.

1. Add `asm` and an `InlineAsmStmt` AST node.
2. Parse and lower only `asm aarch64 { ... }`.
3. Support local scalar reads/writes, fixed general-purpose registers, and a
   documented whitelist of AArch64 instructions needed by qualification tests.
4. Lower accepted blocks to LLVM inline assembly with compiler-generated
   operands and conservative memory/condition-code effects.
5. Reject calls, returns, stack/frame-pointer writes, unsupported operand
   types, unknown implicit effects, and writes without `#` authority.
6. Add `target::arch()` as a compiler-known constant and ensure that target
   branch selection precedes assembly semantic validation.

No public standard-library intrinsic is stabilized merely because the asm
syntax exists. A primitive becomes public only with a separate semantic,
fallback, and cross-target test contract.

## 8. Required evidence

The implementation is accepted only when all of the following are covered:

| Case | Expected result |
|---|---|
| Read a local into a register | Compiles and returns the expected value |
| Write a `binding#` from a register | Compiles and updates the binding |
| Write a non-`#` binding | Semantic rejection |
| Use `asm aarch64` in selected AArch64 code | Compiles and emits expected object code |
| Use `asm x86_64` in selected AArch64 code | Target-mismatch rejection |
| Place x86_64 asm in an unselected AArch64 branch | Does not validate the x86_64 body |
| Modify `sp`, `x29`, or issue `blr` / `bl` | Rejection in the initial subset |
| Access arbitrary memory | Conservatively prevents invalid memory reordering |
| `cpu::spin_hint()` implementations | Each qualified target uses its implementation or documented fallback |

Generated assembly/object-code assertions are required for the local
read/write and ABI-preservation cases. Runtime tests alone do not prove that
the compiler retained the intended barrier or register handling.

## 9. Compatibility and release policy

This feature is experimental and post-1.0. It does not expand the frozen
portable language surface, and a public `.tki` interface must not require an
importer to parse or execute a foreign assembly body. Target-specific source
selection remains local to the compiling package.

The feature may be revised or removed before stabilization. Once stabilized,
the source syntax, target-selection behavior, mutation-authority rule, and
conservative effect default become language commitments.

## 10. Rejected alternatives

### 10.1 `asm(...)` with operand arguments

This reads as a normal function call in Toka, where `name(...)` already means a
function/method call or shape construction. It also exposes LLVM constraint
syntax directly to users.

### 10.2 Raw `unsafe asm "..."`

An opaque string cannot safely express a local variable's input/output
relationship or register damage. A default memory barrier cannot repair an
undeclared register write. Such a form is too weak for useful local data flow
and too unsafe as a general feature.

### 10.3 External `.S` plus `extern fn`

This remains useful FFI, but it is not a Toka language feature and does not
offer direct local-variable access.

### 10.4 A cross-platform assembly IR

It would duplicate backend instruction-selection work and conceal the very
target-specific behavior that an unsafe assembly block must make explicit.
