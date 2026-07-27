#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOKAC="${TOKAC:-$ROOT/build/bin/tokac}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/toka-rust-diagnostics.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

if [[ ! -x "$TOKAC" ]]; then
    echo "error: tokac not found or not executable: $TOKAC" >&2
    exit 2
fi
if ! command -v rustc >/dev/null 2>&1; then
    echo "error: rustc unavailable; diagnostics comparison needs both compilers" >&2
    exit 2
fi

BASE="$ROOT/research/rust-comparison/diagnostics/01_shared_write_rejection"

if "$TOKAC" "$BASE/toka_reject.tk" -o "$WORK/toka" >"$WORK/toka.log" 2>&1; then
    echo "FAIL toka: shared write compiled but should have been rejected" >&2
    exit 1
fi
grep -q E04573 "$WORK/toka.log"
grep -q "payload-write authority is local to the target field declaration" "$WORK/toka.log"

if rustc --edition=2024 "$BASE/rust_reject.rs" -o "$WORK/rust" >"$WORK/rust.log" 2>&1; then
    echo "FAIL rust: shared write compiled but should have been rejected" >&2
    exit 1
fi
grep -q E0594 "$WORK/rust.log"

echo "PASS toka diagnostic rejection: E04573 plus field-authority note"
echo "PASS rust diagnostic rejection: E0594 present"
echo "Diagnostic comparison cases passed."
