#!/bin/bash
# Test compiler path behavior for ODR and tki output directory logic.

set -e

TOKAC="./build/bin/tokac"
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
$TOKAC -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build/helper_custom.o"
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
$TOKAC -c "$TEST_DIR/src/helper.tk" -o "$TEST_DIR/build.v1/app"
if [ ! -f "$TEST_DIR/build.v1/app.tki" ]; then
    echo "FAIL: app.tki not found in build.v1 directory"
    exit 1
fi
if [ -f "$TEST_DIR/build.v1/build.tki" ]; then
    echo "FAIL: Incorrectly generated build.tki due to dotted directory"
    exit 1
fi
echo "PASS: Test 1b"

echo "Test 2: Output .tki in CWD when -o is not specified"
cd "$TEST_DIR"
rm -f *.tki
../../build/bin/tokac -I ../../lib --emit-interface --emit-llvm src/helper.tk
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
$TOKAC "$TEST_DIR/src/main.tk" -o "$TEST_DIR/build/main_app"
"$TEST_DIR/build/main_app"
echo "PASS: Test 3"

# Clean up
rm -rf "$TEST_DIR"
echo "All path behavior tests PASSED!"
