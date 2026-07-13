# Toka 1.0 Core String & Byte API Specification

This document defines the official API reference, design philosophy, architectural trade-offs, and memory geometry for Toka's 1.0 core string, byte-stream, and cursor sub-systems (`str`, `bytes`, and `string`).

---

## 🏛️ 1. Core Design Philosophy

Toka adheres to a high-safety, zero-cost system programming philosophy. String design in Toka 1.0 strictly follows these key pillars:

### 1.1 Txt vs. Byte Dual-Track Intent Alignment
Text and raw binary data represent fundamentally distinct physical intents. Conflating them is the root of countless security vulnerabilities and performance bottlenecks.
- **Text (`str`, `string`)**: Guaranteed to be valid UTF-8 sequences. Operations on text (e.g., slicing, character extraction) must respect variable-width logical character boundaries.
- **Binary (`bytes`)**: A raw physical stream of 8-bit bytes. Operations are purely physical, byte-oriented, and executed with guaranteed $O(1)$ efficiency.

### 1.2 Honest Complexity Naming
Functions that hide $O(N)$ runtime cost behind innocent-looking names are strictly forbidden in Toka's core.
- **`byte_count(self) -> usize`**: Returns the physical size of the view in bytes. Complexity: **$O(1)$**.
- **`count(self) -> usize`**: Scans the UTF-8 byte stream to count logical Unicode codepoints. Complexity: **$O(N)$**. 

### 1.3 The Unidirectional River Principle (No Reverse Scanning)
UTF-8 is a variable-width, single-state byte flow. Scanning backwards is mathematically asymmetrical and highly error-prone (especially with complex Grapheme Clusters, ZWJ-joiners, or surrogate limits). 
- **The rule**: Standard library APIs are strictly **forward-only**. Reverse scans are completely removed.
- **The solution**: Backtracking is exclusively managed via explicit **Cursor Rewind** (`Cursor::rewind(k)`). The physical overhead and state control of traveling "backwards" are explicitly handed back to the developer.

---

## 🏛️ 2. Memory Geometry and Ownership

Toka's string and byte shapes are defined in LLVM with absolute physical clarity:

| Type | LLVM Representation (Memory Geometry) | Ownership Model | Null-Terminated (`\0`) | Security Boundary (PAL) |
| :--- | :--- | :--- | :--- | :--- |
| **`str`** | `%struct.str = { i8* ptr, i64 len }` (16 bytes) | **Borrowed View** (Observe-only, no allocation) | **No** (Can be arbitrary slice) | Bound to host lifetime |
| **`bytes`** | `%struct.bytes = { i8* ptr, i64 len }` (16 bytes) | **Borrowed View** (Observe-only, no allocation) | **No** | Bound to host lifetime |
| **`string`** | `%struct.string = { i8* ptr, i64 len, i64 cap }` (24 bytes) | **Owned Heap Buffer** (Exclusive owner, drops on unwind) | **Yes** (Heap buffer is null-terminated) | Owned anchor |
| **`cstr`** | `%struct.cstr = { i8* ptr }` (8 bytes) | **FFI Borrowed View** (No allocation, no free) | **Yes** (Guaranteed) | Safe FFI only |

---

## 🏛️ 3. Complete API Specifications

### 3.1 `str` (Read-only Text View)

An immutable UTF-8 string slice. It represents a zero-overhead window into static `.rodata`, stack memory, or an owned heap `string`.

| Method Signature | Complexity | Description |
| :--- | :--- | :--- |
| **`byte_count(self) -> usize`** | $O(1)$ | Returns the raw physical size in bytes. |
| **`count(self) -> usize`** | $O(N)$ | Iterates and counts logical Unicode characters. |
| **`slice(self, start: i32, end: i32) -> str`** | $O(N)$ | Returns a sub-slice bounded by logical character boundaries. Negative indices map backwards from end. |
| **`at(self, idx: usize) -> Option<Char32>`** | $O(N)$ | Obtains the Unicode codepoint at the specified logical index. |
| **`chars(self) -> Cursor`** | $O(1)$ | Spawns a forward UTF-8 decoding cursor. |
| **`as_bytes(self) -> bytes`** | $O(1)$ | Zero-cost physical downgrade to raw binary bytes view. |
| **`is_empty(self) -> bool`** | $O(1)$ | Returns `true` if `byte_count() == 0`. |
| **`equals(self, other: str) -> bool`** | $O(N)$ | Bitwise memory comparison using physical pointer addresses. |
| **`starts_with(self, prefix: str) -> bool`** | $O(N)$ | Determines if the string starts with the given prefix. |
| **`ends_with(self, suffix: str) -> bool`** | $O(N)$ | Determines if the string ends with the given suffix. |
| **`index_of_str(self, needle: str) -> Option<usize>`** | $O(N \cdot M)$ | Searches for a substring and returns the byte offset. |
| **`index_of(self, c: char) -> Option<usize>`** | $O(N)$ | Searches for a single ASCII byte and returns its offset. |
| **`contains_char(self, c: char) -> bool`** | $O(N)$ | Convenience method checking if an ASCII byte exists. |
| **`last_index_of(self, c: char) -> Option<usize>`** | $O(N)$ | Finds the last occurrence of an ASCII byte from right to left. |
| **`trim(self) -> str`** | $O(N)$ | Trims whitespace (spaces, tabs, newlines) from both ends. |
| **`trim_start(self) -> str`** | $O(N)$ | Trims leading whitespace. |
| **`trim_end(self) -> str`** | $O(N)$ | Trims trailing whitespace. |
| **`split(self, sep: str) -> ViewStrSplitIterator`** | $O(1)$ | Spawns an iterator to split the string by a separator. |
| **`lines(self) -> ViewStrLinesIterator`** | $O(1)$ | Splits the string into lines, safely stripping `\r`. |
| **`to_i32(self) -> Option<i32>`** | $O(N)$ | Parses an integer value from string content. |
| **`raw_ptr(self) -> *char`** | $O(1)$ | Unsafe direct access to the starting raw memory address. |
| **`as_cstr(self) -> cstr`** | $O(1)$ | Zero-overhead conversion. Assumes null termination at the end of the view. |
| **`to_string(self) -> string`** | $O(N)$ | Allocates heap and deep-copies the view to a mutable `string`. |
| **`repeat(self, n: i32) -> string`** | $O(N)$ | Creates a new heap `string` repeating this view `n` times. |

---

### 3.2 `bytes` (Read-only Binary View)

An immutable raw byte array view. Highly optimized for binary protocol parsing and FFI payload observation.

| Method Signature | Complexity | Description |
| :--- | :--- | :--- |
| **`size(self) -> usize`** | $O(1)$ | Returns the exact byte length. |
| **`at(self, idx: usize) -> Option<byte>`** | $O(1)$ | Reads the byte at the physical index. |
| **`slice_bytes(self, start: usize, end: usize) -> bytes`** | $O(1)$ | Returns a sub-slice. Checks boundaries ($O(1)$). |
| **`try_to_str(self) -> Result<str, UnicodeError>`** | $O(N)$ | Promotes raw binary to a text `str` view, strictly validating UTF-8 validity. |
| **`to_str_lossy(self) -> string`** | $O(N)$ | Lossy heap conversion replacing invalid UTF-8 sequences with `U+FFFD`. |
| **`is_empty(self) -> bool`** | $O(1)$ | Returns `true` if `size() == 0`. |
| **`raw_ptr(self) -> *byte`** | $O(1)$ | Unsafe direct access to the underlying byte pointer. |

---

### 3.3 `string` (Owned Mutable Heap Text)

An owned, growable UTF-8 string buffer managed on the heap. Maintains an automatic null terminator (`\0`) at the end of its active length for seamless FFI interoperability.

| Method Signature | Complexity | Description |
| :--- | :--- | :--- |
| **`push_str(self#, s: str)`** | $O(M)$ | Appends a `str` view to the end of the buffer, growing capacity if necessary. |
| **`push_char(self#, c: Char32)`** | $O(1)$ | Appends a Unicode codepoint encoded in 1 to 4 UTF-8 bytes. |
| **`push_ascii(self#, c: byte)`** | $O(1)$ | Performance-optimized ASCII byte append. Panics if `c >= 0x80`. |
| **`pop_codepoint(self#) -> Option<Char32>`** | $O(1)$ | Removes and returns the last Unicode character. Uses a stack-allocated local buffer to bypass aliasing conflicts. |
| **`as_str(self) -> str`** | $O(1)$ | Zero-overhead borrowed text slice projection. |
| **`c_str(self) -> *char`** | $O(1)$ | Exposes the direct null-terminated heap buffer pointer. |
| **`capacity(self) -> usize`** | $O(1)$ | Returns the total allocated size of the heap buffer. |
| **`clear(self#)`** | $O(1)$ | Resets the active length to 0. Capacity is preserved. |

---

### 3.4 `Cursor` (UTF-8 Decoding Stream Iterator)

A stateful forward-only decoder. Spawned by `str.chars()`.

| Method Signature | Complexity | Description |
| :--- | :--- | :--- |
| **`next(self#) -> Option<Char32>`** | $O(1)$ | Decodes and returns the next Unicode character, advancing internal pointer. |
| **`peek(self) -> Option<Char32>`** | $O(1)$ | Decodes the next character without advancing the internal pointer. |
| **`rewind(self#, k: usize)`** | Bounded $O(4)$ | Rolls back the cursor pointer by `k` physical bytes. Complexity is bounded $O(4)$ due to continuation byte alignment validation. |
| **`current_pos(self) -> usize`** | $O(1)$ | Returns the current physical byte offset in the underlying slice. |

---

## 🏛️ 4. Cross-Conversion and DX Ergonomics

Toka ensures strict type checking while offering seamless ergonomics via Explicit-Cost naming guidelines:
- **`as_` methods (Zero-Cost Views)**: Fast, zero-allocation conversions returning views bounded by parent lifetimes.
- **`to_` methods (Allocation/Copying)**: Triggers deep copies, memory allocations, or UTF-8 validations.

```
       "literal" ----[Default]-----> str <=========[try_to_str]========= bytes
           |                          ^                                    ^
       [to_string]                 [as_str]                             [as_bytes]
           |                          |                                    |
           v                          |                                    |
        string  ========[as_str]======+                                    |
           |                                                               |
        [c_str]                                                            |
           v                                                               v
        *char <====================[raw_ptr]============================ *byte
```

### 4.1 Auto-Deref Ergonomics
To reduce syntax noise, whenever a function or method signature explicitly expects a `str` parameter, the compiler automatically allows passing a mutable `string` variable directly, auto-inserting the zero-cost `.as_str()` view projection under the hood. No manual `.as_str()` required!

The same zero-cost projection applies to equality with a `str` value or text
literal in either operand order. Write `command == "scan"`, not
`command == string::from("scan")`; equality never allocates or consumes the
owned string.

---

## ⚖️ 5. Safety and C-FFI Redlines

1. **Physical Copy Boundaries**: `str` is **not** guaranteed to be null-terminated. Passing `str.raw_ptr()` directly into C functions (like `printf` or `strlen`) is strictly prohibited and highly dangerous. Always promote to `cstr` or `string.c_str()`.
2. **Memory Overwrites**: Modifying elements in a `str` view via FFI or unsafe raw pointer manipulations is undefined behavior (UB), as `str` may slice immutable `.rodata` static memory segments. Use `string` for all mutation workflows.
