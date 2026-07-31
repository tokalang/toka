#!/usr/bin/env bash
# Verify public unsafe/raw API naming exemptions outside tests/pass redline bypasses.

set -e

TOKAC="${TOKAC:-./build/bin/tokac}"
if [[ "$TOKAC" = /* ]]; then
    TOKAC_ABS="$TOKAC"
elif [[ "$TOKAC" = */* ]]; then
    TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd)/$(basename "$TOKAC")"
else
    TOKAC_ABS="$(command -v "$TOKAC" 2>/dev/null || echo "$TOKAC")"
    if [[ "$TOKAC_ABS" != /* ]]; then
        TOKAC_ABS="./$TOKAC"
    fi
fi

TEST_DIR="./tmp/public_unsafe_api_test"
mkdir -p "$TEST_DIR"

echo "Running public unsafe/raw API naming tests..."

cat << 'EOF' > "$TEST_DIR/bad_api.tk"
pub fn foo(ptr: *i32) -> void {
}

fn main() -> i32 {
    return 0
}
EOF

cat << 'EOF' > "$TEST_DIR/good_api.tk"
pub fn unsafe_accept(ptr: *i32) -> void {
}

pub fn raw_accept(ptr: *i32) -> void {
}

fn main() -> i32 {
    return 0
}
EOF

cat << 'EOF' > "$TEST_DIR/good_shape.tk"
pub shape RawPoint(
    *ptr: i32
)

impl RawPoint@Encap {
    fn drop(self#) {}
}

fn main() -> i32 {
    return 0
}
EOF

if "$TOKAC_ABS" "$TEST_DIR/bad_api.tk" -o "$TEST_DIR/bad_api" 2> "$TEST_DIR/bad_api.err"; then
    echo "FAIL: public raw-pointer API without unsafe/raw prefix unexpectedly passed"
    exit 1
fi
if ! grep -q "E0480" "$TEST_DIR/bad_api.err"; then
    echo "FAIL: expected E0480 for public raw-pointer API"
    cat "$TEST_DIR/bad_api.err"
    exit 1
fi

"$TOKAC_ABS" "$TEST_DIR/good_api.tk" -o "$TEST_DIR/good_api" 2> "$TEST_DIR/good_api.err"
"$TOKAC_ABS" "$TEST_DIR/good_shape.tk" -o "$TEST_DIR/good_shape" 2> "$TEST_DIR/good_shape.err"

echo "PASS: public unsafe/raw API naming tests"
