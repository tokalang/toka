# Toka 1.0 Language Conformance Test Suite

This directory contains the normative multi-modal **Toka 1.0 Language Conformance Test Suite**. Conformance tests verify syntax rules, ownership semantics, lifetime bounds, error diagnostics, and async runtime invariants against the Toka Language Specification ([`docs/syntax.md`](../../docs/syntax.md)).

---

## 1. Directory Structure

```text
tests/conformance/
  manifest.json              # Master JSON manifest defining test suites & expectations
  README.md                  # Specification & test harness instructions
  syntax/                    # Lexer/Parser positive & negative syntax tests
  ownership/                 # Borrow, cede, move, and destructor count tests
  async/                     # Async frame capture, .await points, cancellation tests
  diagnostics/               # Layered error code (E####) & span line tests
  codegen/                   # LLVM IR lowering & sret ABI verification tests
  std/                       # Core/Std container & buffer stealing contracts
```

---

## 2. Manifest Schema Specification

All test cases are registered in [`manifest.json`](manifest.json) using the following JSON schema:

```json
{
  "id": "unique_test_identifier",
  "path": "relative/path/to/test.tk",
  "type": "compile-pass | compile-fail | run | ir-verify",
  "expected_exit_code": 0,
  "expected_diagnostic_code": "E####",
  "expected_span_line": 15,
  "timeout_seconds": 10,
  "description": "Human-readable test purpose"
}
```

### Test Modalities
- `run`: Compiles with `tokac`, executes the output binary, asserts `expected_exit_code` (default: `0`), and cleans up temporary binaries.
- `compile-pass`: Compiles with `tokac`, asserts return code `0`.
- `compile-fail`: Compiles with `tokac`, asserts non-zero return code, and verifies that `expected_diagnostic_code` (e.g. `E0443`) appears in compiler output.
- `ir-verify`: Compiles to LLVM IR and checks for required IR attributes (e.g. `sret`).

---

## 3. Running the Conformance Suite

Run the conformance suite runner:

```bash
python3 tools/run_conformance.py
```

The runner automatically enforces timeouts, cleans up temporary build artifacts in `tmp/`, and reports test results.
