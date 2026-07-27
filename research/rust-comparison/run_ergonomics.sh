#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOKAC="${TOKAC:-$ROOT/build/bin/tokac}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/toka-rust-ergonomics.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$TOKAC" ]]; then
    echo "error: tokac not found or not executable: $TOKAC" >&2
    exit 2
fi
if ! command -v rustc >/dev/null 2>&1; then
    echo "error: rustc unavailable; ergonomics comparison needs both compilers" >&2
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
    local code="$3"
    if "$TOKAC" "$source" -o "$WORK/$name" >"$WORK/$name.log" 2>&1; then
        echo "FAIL toka: $name compiled but should have been rejected" >&2
        exit 1
    fi
    grep -q "$code" "$WORK/$name.log"
    echo "PASS toka rejection: $name ($code)"
}

run_rust_pass() {
    local name="$1"
    local source="$2"
    rustc --edition=2024 "$source" -o "$WORK/$name"
    "$WORK/$name"
    echo "PASS rust: $name"
}

BASE="$ROOT/research/rust-comparison/ergonomics"

run_rust_pass bool_exhaustiveness_rust "$BASE/01_non_enum_exhaustiveness/rust.rs"
run_toka_pass bool_exhaustiveness_toka_baseline "$BASE/01_non_enum_exhaustiveness/toka_baseline.tk"
run_toka_fail bool_exhaustiveness_toka_boundary "$BASE/01_non_enum_exhaustiveness/toka_boundary.tk" E0553

run_rust_pass partial_result_rust "$BASE/02_partial_result_propagation/rust.rs"
run_toka_fail partial_result_toka_boundary "$BASE/02_partial_result_propagation/toka_boundary.tk" E04595

echo "Ergonomics comparison cases passed."
