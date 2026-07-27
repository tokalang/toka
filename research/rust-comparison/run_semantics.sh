#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOKAC="${TOKAC:-$ROOT/build/bin/tokac}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/toka-rust-semantics.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$TOKAC" ]]; then
    echo "error: tokac not found or not executable: $TOKAC" >&2
    exit 2
fi
if ! command -v rustc >/dev/null 2>&1; then
    echo "error: rustc unavailable; semantics comparison needs both compilers" >&2
    exit 2
fi

BASE="$ROOT/research/rust-comparison/semantics/01_panic_recovery_boundary"

rustc --edition=2024 "$BASE/rust.rs" -o "$WORK/rust"
"$WORK/rust" >"$WORK/rust.log" 2>&1

"$TOKAC" "$BASE/toka_termination.tk" -o "$WORK/toka"
if "$WORK/toka" >"$WORK/toka.log" 2>&1; then
    echo "FAIL toka: panic program returned normally" >&2
    cat "$WORK/toka.log" >&2
    exit 1
fi

echo "PASS rust: unwind-enabled catch_unwind recovered and ran Drop"
echo "PASS toka: panic remained a non-returning process boundary"
echo "Semantic comparison cases passed."
