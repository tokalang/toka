# Toka 1.0 Qualification Evidence Ledger

**Status**: Qualification & Discovery Ledger (RC3 evidence retained; post-RC3 P0 requalification pending)
**Authority Hierarchy**:
- Normative language and compiler rules are governed by [`docs/1_0_freeze_decision_list.md`](1_0_freeze_decision_list.md) and [`docs/1_0_closure_plan.md`](1_0_closure_plan.md).
- This ledger tracks empirical evidence (conformance tests, benchmark logs, sanitizer build smoke, micro-slices) for production qualification.

---

## 1. CI Gate & Qualification Tiering

- **Pull Request Gate (`.github/workflows/ci.yml`)**: Automated on every PR and commit on `main`. Enforces Conformance Suite (14/14), Plaintext No-OpenSSL Script (`tools/test_no_openssl.sh`), and Runtime ASan Build Smoke (`tools/build_sanitized.sh runtime-asan`).
- **Release Candidate Gate (`.github/workflows/release.yml`)**: Validated for the `v0.9.9-rc3` dry-run candidate via `tools/scripts/release_gate.py` (13-stage release qualification across Linux x64/arm64 and macOS x64/arm64); evidence: [Run 30189209349](https://github.com/tokalang/toka/actions/runs/30189209349).
- **Manual / Scheduled Sanitizer Gate (`tools/build_sanitized.sh`)**: `runtime-tsan` and `compiler-asan` are available for manual developer validation or dedicated scheduled builds.

---

## 2. Active Gap Inventory & Release Audit

### A. Language Semantics & Ownership

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-LANG-01** | **`Vec::get` Move vs. Borrow Semantics**<br>Calling `.get(idx)` on `@encap` element vectors moves/drops temporary elements unless `.clone()` or explicit borrow is used. | Language | `P1` | Yes | `closed` | [vec_get_clone.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/ownership/vec_get_clone.tk) & `tests/conformance/manifest.json#ownership_vec_get_clone_01` |
| **GAP-LANG-02** | **Layered Diagnostic Conformance**<br>Lock stable diagnostic codes (`E0417`, `E0443`), levels, and line/col spans without freezing text formatting. | Language | `P1` | Yes | `closed` | [mut_borrow_err.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/diagnostics/mut_borrow_err.tk) & `spec/diagnostic.map.json` |
| **GAP-LANG-03a** | **Async Frame Local Lifetime Across `.await`**<br>Local variables retained across `.await` points must preserve state and execute deterministic destructors on scope exit (`drop_count == 1`). | Language | `P0` | Yes | `closed` | [async_frame_drop_across_await.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/async/async_frame_drop_across_await.tk) & `tests/conformance/manifest.json` |
| **GAP-LANG-03b** | **Async Task Cancellation Destructors**<br>Rich cancellation and exactly-once destructor invocation upon task handle cancellation. | Language | `P2` | Post-1.0 | `planned` | `docs/1_0_closure_plan.md#cancellation` |
| **GAP-LANG-04** | **Handle Identity / Payload Write Separation**<br>For an existing binding, effective authority is `declaration/signature capability ∩ use-site intent ∩ PAL permission`. A handle-side `#` (`*#p`, `^#p`, `~#p`, `&#p`) authorizes only rebinding that handle; it cannot authorize bare, member, indexed, call-argument, callable, or mutable-receiver payload writes. The first fresh-local Shared-flow slice additionally enforces `effective-P(LHS) = declared-P(LHS) ∩ effective-P(direct RHS)` without provenance traversal. Whole independent `cede`, return, field, nullable, and pattern-flow closure remain separate work. | Language | `P0` | Yes | `in_progress` | Layer 1 evidence: `tests/conformance/diagnostics/handle_identity_not_payload_writable_*.tk`, `call_*_cannot_supply_*.tk`, `static_call_handle_only_cannot_supply_payload.tk`, `callable_argument_cannot_forge_payload.tk`, `raw_payload_write_requires_unsafe.tk`, `method_use_site_cannot_forge_payload.tk`, `ownership_call_permission_capability_matrix_01`, `ownership_callable_argument_permission_matrix_01`, `shared_view_cannot_amplify_payload.tk`, `cede_shared_view_cannot_amplify_payload.tk`, `shared_view_cannot_supply_payload_call.tk`, `shared_view_preserves_payload_capability.tk`, `cede_unique_creates_independent_owner.tk`, and source-less `permission_001_capability` replay |

### B. Compiler & Lowering

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-COMP-01** | **Large Generic Aggregate SRET Lowering**<br>Generic struct returns like `Option<Entry<'K, 'V>>` must lower via `sret` without LLVM physreg copy errors. | Compiler | `P1` | Yes | `closed` | [sret_abi.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/codegen/sret_abi.tk) & `tests/conformance/manifest.json#ir_sret_abi_01` |

### C. Async Runtime & Memory Safety

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-RNTM-01** | **Socket Fragmentation & Chunked Streaming**<br>TCP socket reads fragmenting across CRLF, chunk headers, and trailer headers must resume cleanly without memory leaks. | Runtime | `P0` | Yes | `closed` | `tests/pass/g12_stdx_http_client_server_test.tk#Chunked Server` |
| **GAP-RNTM-02** | **Stale Wakeup & Race Condition Stress**<br>WaitRegistry generation increments must prevent stale wakeups under task concurrency. | Runtime | `P0` | Yes | `closed` | [repro_wait_registry_race.c](file:///Users/zhyi/GitDP/toka/playground/repro_wait_registry_race.c) & `playground/repro_concurrency_gate_race.c` |
| **GAP-RNTM-03** | **Sanitizer Build Smoke Verification**<br>Compile C runtime objects (`toka_rt.c`) and `tokac` compiler with AddressSanitizer and ThreadSanitizer flags. | Runtime | `P1` | Yes | `closed` | [build_sanitized.sh](file:///Users/zhyi/GitDP/toka/tools/build_sanitized.sh) & `.github/workflows/ci.yml` |

### D. Standard Library (`core`/`std`/`stdx`)

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-STDL-01** | **Owner-Carrying Buffer Stealing (`Bytes` / `Vec`)**<br>`Bytes::from_vec(cede v)` and `b#.into_vec()` must transfer underlying buffers without heap allocation. | Stdlib | `P0` | Yes | `closed` | `tests/pass/g13_stdx_net_zero_copy_bench.tk` |
| **GAP-STDL-02** | **OpenSSL Optionality & Clean Plaintext Fallback**<br>Core plaintext HTTP and TCP applications MUST compile and run cleanly without OpenSSL enabled (`-UTOKA_HAS_OPENSSL`). | Stdlib | `P1` | Yes | `closed` | [test_no_openssl.sh](file:///Users/zhyi/GitDP/toka/tools/test_no_openssl.sh) & `docs/1_0_scope.md#capability` |
| **GAP-STDL-03** | **Safe Cede Ownership Transfer in `HeaderMap::clone`**<br>`return HeaderMap(entries = cede new_entries)` eliminates `unsafe_forget` bypasses. | Stdlib | `P1` | Yes | `closed` | [http.tk:105](file:///Users/zhyi/GitDP/toka/lib/stdx/net/http.tk#L105) & `tests/pass/g12_stdx_http_client_server_test.tk#test_headermap_clone_independence` |
| **GAP-STDL-04** | **OpenSSL 3 RSA Legacy API Migration**<br>Migrate legacy `RSA_generate_key` calls in C runtime to modern OpenSSL 3 `EVP_PKEY` keygen API. | Stdlib | `P2` | Post-1.0 | `planned` | [lib/sys/toka_rt.c:2726](file:///Users/zhyi/GitDP/toka/lib/sys/toka_rt.c#L2726) |

### E. Production Vertical Micro-Slices

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-VSLC-01** | **HTTP/1.1, HTTPS & WSS Micro-Slice**<br>Async client/server streaming, chunked transfer, TLS security verification, and WebSocket framing. | Application | `P0` | Yes | `closed` | `tests/pass/g12_stdx_http_client_server_test.tk` & `g12_stdx_https_wss_test.tk` |
| **GAP-VSLC-02** | **Synchronous Filesystem Micro-Slice**<br>File open, synchronous read/write, path existence, and directory traversal. | Application | `P1` | Yes | `closed` | [01_filesystem_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/01_filesystem_slice.tk) |
| **GAP-VSLC-03** | **Real `stdx/serde/json` Serde Micro-Slice**<br>Reflection-based JSON serialization, typed deserialization, and malformed error handling. | Application | `P1` | Yes | `closed` | [04_json_serde_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/04_json_serde_slice.tk) |
| **GAP-VSLC-04** | **Subprocess & OS Execution Micro-Slice**<br>Command line argument parsing, subprocess spawning, stdout capture, and wait status. | Application | `P2` | Post-1.0 | `verified` | [03_subprocess_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/03_subprocess_slice.tk) & `tests/conformance/manifest.json#subprocess_conformance_01` |

---

## 3. Freeze Gate Exit Criteria

Toka 1.0 will enter Freeze Mode only when:
1. All P0 and P1 gaps transition to status `closed` upon completion of `docs/1_0_release_review.md` backed by a Tier 1 multi-platform execution report for the current revision. RC3 is historical evidence only after GAP-LANG-04.
2. All declared production vertical micro-slices execute cleanly.
3. PR Gate and Release Candidate Gate runs pass cleanly without regression.
