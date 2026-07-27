#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOKAC="${TOKAC:-$ROOT/build/bin/tokac}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/toka-rust-comparison.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$TOKAC" ]]; then
    echo "error: tokac not found or not executable: $TOKAC" >&2
    exit 2
fi

run_toka_pass() {
    local name="$1"
    local source="$2"
    "$TOKAC" "$source" -o "$WORK/$name"
    "$WORK/$name"
    echo "PASS toka: $name"
}

run_toka_fail() {
    local name="$1"
    local source="$2"
    local expected_code="$3"
    local log="$WORK/$name.log"
    if "$TOKAC" "$source" -o "$WORK/$name" >"$log" 2>&1; then
        echo "FAIL toka: $name compiled but should have been rejected" >&2
        exit 1
    fi
    if ! grep -q "$expected_code" "$log"; then
        echo "FAIL toka: $name did not report $expected_code" >&2
        cat "$log" >&2
        exit 1
    fi
    echo "PASS toka rejection: $name ($expected_code)"
}

run_rust_pass() {
    local name="$1"
    local source="$2"
    if ! command -v rustc >/dev/null 2>&1; then
        echo "SKIP rust: $name (rustc unavailable)"
        return
    fi
    rustc --edition=2024 "$source" -o "$WORK/$name"
    "$WORK/$name"
    echo "PASS rust: $name"
}

run_rust_fail() {
    local name="$1"
    local source="$2"
    if ! command -v rustc >/dev/null 2>&1; then
        echo "SKIP rust rejection: $name (rustc unavailable)"
        return
    fi
    if rustc --edition=2024 "$source" -o "$WORK/$name" >"$WORK/$name.log" 2>&1; then
        echo "FAIL rust: $name compiled but should have been rejected" >&2
        exit 1
    fi
    echo "PASS rust rejection: $name"
}

BASE="$ROOT/research/rust-comparison/expressiveness"

run_toka_pass handle_payload_pass "$BASE/01_handle_payload_permissions/toka_pass.tk"
"$TOKAC" "$BASE/01_handle_payload_permissions/toka_pass.tk" -o "$WORK/handle_payload_warning_profile" >"$WORK/handle_payload_warning_profile.log" 2>&1
if grep -qE 'W0401|W0402' "$WORK/handle_payload_warning_profile.log"; then
    echo "FAIL toka: H/P payload-use case emitted a misleading W0401 or W0402" >&2
    cat "$WORK/handle_payload_warning_profile.log" >&2
    exit 1
fi
if ! grep -q W0407 "$WORK/handle_payload_warning_profile.log"; then
    echo "FAIL toka: H/P payload-use case lost the meaningful unused-handle warning" >&2
    cat "$WORK/handle_payload_warning_profile.log" >&2
    exit 1
fi
echo "PASS toka warning profile: payload use suppresses W0401/W0402 and retains W0407"
run_toka_fail handle_payload_reject_payload "$BASE/01_handle_payload_permissions/toka_reject_payload_from_handle.tk" E04571
run_toka_fail handle_payload_reject_rebind "$BASE/01_handle_payload_permissions/toka_reject_rebind_from_payload.tk" E04571
run_rust_pass handle_payload_rust "$BASE/01_handle_payload_permissions/rust.rs"

run_toka_pass interior_mutability_pass "$BASE/02_field_interior_mutability/toka_pass.tk"
run_toka_fail interior_mutability_reject_sibling "$BASE/02_field_interior_mutability/toka_reject_ordinary_sibling.tk" E04573
run_rust_pass interior_mutability_rust "$BASE/02_field_interior_mutability/rust.rs"
run_rust_fail interior_mutability_rust_without_cell "$BASE/02_field_interior_mutability/rust_reject_without_cell.rs"

run_rust_pass scoped_borrowed_concurrency_rust "$BASE/03_scoped_borrowed_concurrency/rust.rs"
run_toka_fail scoped_borrowed_concurrency_toka_boundary "$BASE/03_scoped_borrowed_concurrency/toka_1_0_boundary.tk" E04583

run_toka_pass lazy_iterator_eager_baseline "$BASE/04_lazy_consuming_iterator/toka_eager_callback_baseline.tk"
run_rust_pass lazy_iterator_rust "$BASE/04_lazy_consuming_iterator/rust.rs"

run_toka_pass borrowed_iterator_toka "$BASE/05_borrowed_iterator_baseline/toka_pass.tk"
run_rust_pass borrowed_iterator_rust "$BASE/05_borrowed_iterator_baseline/rust.rs"

run_toka_pass detached_non_borrowing_toka "$BASE/06_detached_non_borrowing_baseline/toka_pass.tk"
run_rust_pass detached_non_borrowing_rust "$BASE/06_detached_non_borrowing_baseline/rust.rs"
run_rust_fail detached_borrowed_rust "$BASE/06_detached_non_borrowing_baseline/rust_reject_borrowed.rs"

run_rust_pass dyn_associated_type_object_rust "$BASE/07_dyn_associated_type_object/rust.rs"
run_toka_fail dyn_associated_type_object_toka_boundary "$BASE/07_dyn_associated_type_object/toka_1_0_boundary.tk" E0617

run_rust_pass async_trait_protocol_rust "$BASE/08_async_trait_protocol/rust.rs"
run_toka_pass async_trait_protocol_toka_ordinary_baseline "$BASE/08_async_trait_protocol/toka_ordinary_async_baseline.tk"
run_toka_fail async_trait_protocol_toka_1_0_boundary "$BASE/08_async_trait_protocol/toka_trait_async_1_0_boundary.tk" E0618

run_rust_pass error_entry_and_erasure_rust "$BASE/09_error_entry_and_erasure/rust.rs"
run_toka_pass error_entry_and_erasure_toka_baseline "$BASE/09_error_entry_and_erasure/toka_typed_entry_baseline.tk"
run_toka_fail error_entry_and_erasure_toka_boundary "$BASE/09_error_entry_and_erasure/toka_main_result_boundary.tk" E04596

echo "All expression comparison cases passed."
