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
"$TOKAC_ABS" -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build/helper_custom.o"
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
"$TOKAC_ABS" -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build.v1/app"
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
"$TOKAC_ABS" -I ../../lib --emit-interface --emit-llvm src/helper.tk >/dev/null
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
"$TOKAC_ABS" "$TEST_DIR/src/main.tk" -o "$TEST_DIR/build/main_app"
"$TEST_DIR/build/main_app"
echo "PASS: Test 3"

echo "Test 4: pub(path) uses normalized import-path identity"
mkdir -p "$TEST_DIR/visibility/allowed/nested"
cat << 'EOF' > "$TEST_DIR/visibility/grant.tk"
pub shape PathGate (
    allowed_val: i32,
    nested_val: i32,
    middle_val: i32
)

impl PathGate@encap {
    pub(tmp/path_test/visibility/allowed/../allowed) allowed_val
    pub(tmp/path_test/visibility/allowed/nested) nested_val
    pub(path_test) middle_val

    fn drop(self#) {}

    pub fn clone(self) -> PathGate {
        return PathGate(
            allowed_val = self.allowed_val,
            nested_val = self.nested_val,
            middle_val = self.middle_val
        )
    }
}
EOF

cat << 'EOF' > "$TEST_DIR/visibility/allowed/main_ok.tk"
import ../grant::{PathGate}

fn main() -> i32 {
    auto gate = PathGate(allowed_val = 11, nested_val = 22, middle_val = 33)
    return gate.allowed_val - 11
}
EOF

"$TOKAC_ABS" "$TEST_DIR/visibility/allowed/main_ok.tk" -o "$TEST_DIR/build/path_visibility_ok"
"$TEST_DIR/build/path_visibility_ok"
echo "PASS: Test 4"

echo "Test 5: pub(path) allows child modules under the target path"
cat << 'EOF' > "$TEST_DIR/visibility/allowed/nested/main_nested.tk"
import ../../grant::{PathGate}

fn main() -> i32 {
    auto gate = PathGate(allowed_val = 11, nested_val = 22, middle_val = 33)
    return gate.nested_val - 22
}
EOF

"$TOKAC_ABS" "$TEST_DIR/visibility/allowed/nested/main_nested.tk" -o "$TEST_DIR/build/path_visibility_nested"
"$TEST_DIR/build/path_visibility_nested"
echo "PASS: Test 5"

echo "Test 6: pub(path) does not match path text in the middle"
cat << 'EOF' > "$TEST_DIR/visibility/allowed/main_middle_denied.tk"
import ../grant::{PathGate}

fn main() -> i32 {
    auto gate = PathGate(allowed_val = 11, nested_val = 22, middle_val = 33)
    return gate.middle_val
}
EOF

if "$TOKAC_ABS" "$TEST_DIR/visibility/allowed/main_middle_denied.tk" -o "$TEST_DIR/build/path_visibility_middle_denied" > "$TEST_DIR/build/path_visibility_middle_denied.log" 2>&1; then
    echo "FAIL: pub(path_test) incorrectly matched a middle path segment"
    exit 1
fi
if ! grep -q "error\\[E0418\\]" "$TEST_DIR/build/path_visibility_middle_denied.log"; then
    echo "FAIL: Expected private member error for middle path segment match"
    cat "$TEST_DIR/build/path_visibility_middle_denied.log"
    exit 1
fi
echo "PASS: Test 6"

# Clean up
rm -rf "$TEST_DIR"
echo "All path behavior tests PASSED!"
