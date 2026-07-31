#!/bin/bash
# Test compiler path behavior for ODR and tki output directory logic.

set -e

TOKAC="${TOKAC:-./build/bin/tokac}"
# Resolve TOKAC to absolute path
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
WORKSPACE_ROOT="$(pwd)"
TOKAC_SCOPE_ARGS=(--workspace-node toka-path-behavior-v1 --workspace-root "$WORKSPACE_ROOT")
export TOKA_LIB="$WORKSPACE_ROOT/lib"
TEST_DIR="./tmp/path_test"

# Recreate test directory
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"
mkdir -p "$TEST_DIR/build"
mkdir -p "$TEST_DIR/src"
mkdir -p "$TEST_DIR/src/nested"

# Create source files
cat << 'EOF' > "$TEST_DIR/src/helper.tk"
pub fn get_val() -> i32 {
    return 42
}
EOF

cat << 'EOF' > "$TEST_DIR/src/main.tk"
import ./helper::{get_val}
fn main() -> i32 {
    if (get_val() == 42) {
        return 0
    }
    return 1
}
EOF

echo "Test 1: Output .tki next to -o target"
"$TOKAC_ABS" "${TOKAC_SCOPE_ARGS[@]}" -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build/helper_custom.o"
if [ ! -f "$TEST_DIR/build/helper_custom.tki" ]; then
    echo "FAIL: helper_custom.tki not found in build directory"
    exit 1
fi
if [ -f "$TEST_DIR/src/helper.tki" ]; then
    echo "FAIL: helper.tki incorrectly generated in src directory"
    exit 1
fi
echo "PASS: Test 1"

echo "Test 1b: Output .tki next to -o target with dotted directory in path"
mkdir -p "$TEST_DIR/build.v1"
"$TOKAC_ABS" "${TOKAC_SCOPE_ARGS[@]}" -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build.v1/app"
if [ ! -f "$TEST_DIR/build.v1/app.tki" ]; then
    echo "FAIL: app.tki not found in build.v1 directory"
    exit 1
fi
if [ -f "$TEST_DIR/build.tki" ]; then
    echo "FAIL: Incorrectly generated build.tki due to dotted directory"
    exit 1
fi
echo "PASS: Test 1b"

echo "Test 2: Output .tki in CWD when -o is not specified"
cd "$TEST_DIR"
rm -f *.tki
"$TOKAC_ABS" "${TOKAC_SCOPE_ARGS[@]}" -I ../../lib --emit-interface --emit-llvm src/helper.tk >/dev/null
if [ ! -f "helper.tki" ]; then
    echo "FAIL: helper.tki not found in CWD"
    exit 1
fi
if [ -f "src/helper.tki" ]; then
    echo "FAIL: helper.tki incorrectly generated in src directory"
    exit 1
fi
cd ../..
echo "PASS: Test 2"

echo "Test 3: Relative import does not change behavior when .tki exists in source folder (and no .o is linked)"
# If helper.tki exists in src, compiling main.tk should STILL succeed and compile helper's body, because helper.o is not linked
cp "$TEST_DIR/build/helper_custom.tki" "$TEST_DIR/src/helper.tki"
"$TOKAC_ABS" "${TOKAC_SCOPE_ARGS[@]}" "$TEST_DIR/src/main.tk" -o "$TEST_DIR/build/main_app"
"$TEST_DIR/build/main_app"
echo "PASS: Test 3"

echo "Test 4: legacy pub(path) grants are rejected"
mkdir -p "$TEST_DIR/visibility"
cat << 'EOF' > "$TEST_DIR/visibility/grant.tk"
pub shape PathGate (
    value: i32
)

impl PathGate@encap {
    pub(tmp/path_test/visibility) value
    fn drop(self#) {}
}
EOF

if "$TOKAC_ABS" "${TOKAC_SCOPE_ARGS[@]}" -c "$TEST_DIR/visibility/grant.tk" -o "$TEST_DIR/build/path_visibility_legacy.o" > "$TEST_DIR/build/path_visibility_legacy.log" 2>&1; then
    echo "FAIL: legacy pub(path) grant unexpectedly compiled"
    exit 1
fi
if ! grep -q "error\\[E01252\\]" "$TEST_DIR/build/path_visibility_legacy.log"; then
    echo "FAIL: Expected legacy pub(path) rejection"
    cat "$TEST_DIR/build/path_visibility_legacy.log"
    exit 1
fi
echo "PASS: Test 4"

# Clean up
rm -rf "$TEST_DIR"
echo "All path behavior tests PASSED!"
