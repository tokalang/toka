#!/bin/bash
# Test compiler .tki cache validation, version mismatch, target mismatch, and fallback.

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

TEST_DIR="./tmp/tki_validation_test"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# 1. Test case 1: Valid .tki usage (no .tk exists, metadata is valid)
echo "Test 1: Valid .tki usage without .tk source"
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF

cat << 'EOF' > "$TEST_DIR/main.tk"
import ./lib::{get_num}
fn main() -> i32 {
    if (get_num() == 100) {
        return 0
    }
    return 1
}
EOF

# Compile lib to .o and .tki
"$TOKAC_ABS" -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"
if [ ! -f "$TEST_DIR/lib.tki" ]; then
    echo "FAIL: lib.tki was not generated"
    exit 1
fi

# Verify metadata is present
if ! grep -q "compiler_version" "$TEST_DIR/lib.tki"; then
    echo "FAIL: compiler_version metadata missing from generated .tki"
    exit 1
fi

# Remove lib.tk source file
rm -f "$TEST_DIR/lib.tk"

# Compile and link main.tk using lib.tki and lib.o
"$TOKAC_ABS" "$TEST_DIR/main.tk" "$TEST_DIR/lib.o" -o "$TEST_DIR/main_app"
if [ ! -f "$TEST_DIR/main_app" ]; then
    echo "FAIL: main_app compilation failed using valid .tki"
    exit 1
fi
# Run main_app
"$TEST_DIR/main_app"
echo "PASS: Test 1"

# 2. Test case 2: Stale .tki fallback to .tk (metadata hash mismatch, but .tk exists)
echo "Test 2: Stale .tki fallback to .tk source"
# Re-create lib.tk
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF

# Compile to generate lib.tki and lib.o matching the current state
"$TOKAC_ABS" -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"

# Modify lib.tk (change return value to 200) so that lib.tki hash becomes stale
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 200
}
EOF

# Modify main.tk to expect 200
cat << 'EOF' > "$TEST_DIR/main.tk"
import ./lib::{get_num}
fn main() -> i32 {
    if (get_num() == 200) {
        return 0
    }
    return 1
}
EOF

# Compile main.tk. It should fallback to lib.tk, see the updated get_num() -> 200, and compile successfully
"$TOKAC_ABS" "$TEST_DIR/main.tk" -o "$TEST_DIR/main_app_2"
"$TEST_DIR/main_app_2"
echo "PASS: Test 2"

# 3. Test case 3: Target triple mismatch rejection (no .tk exists)
echo "Test 3: Target triple mismatch rejection"
rm -f "$TEST_DIR/lib.tk"
# Modify lib.tki to have mismatched target_triple (using portable sed pattern)
cat "$TEST_DIR/lib.tki" | sed 's/target_triple: .*/target_triple: invalid-target-triple/' > "$TEST_DIR/temp.tki"
mv "$TEST_DIR/temp.tki" "$TEST_DIR/lib.tki"

# Compiling main.tk should fail since lib.tk is missing and lib.tki is incompatible
if "$TOKAC_ABS" "$TEST_DIR/main.tk" "$TEST_DIR/lib.o" -o "$TEST_DIR/main_app_3" 2> "$TEST_DIR/err3.txt"; then
    echo "FAIL: Expected compilation to fail due to target triple mismatch"
    exit 1
fi
if ! grep -q "Incompatible or stale interface file" "$TEST_DIR/err3.txt"; then
    echo "FAIL: Expected stale interface file error message"
    cat "$TEST_DIR/err3.txt"
    exit 1
fi
echo "PASS: Test 3"

# 4. Test case 4: Compiler version mismatch rejection
echo "Test 4: Compiler version mismatch rejection"
# Recompile to get valid lib.tki
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF
"$TOKAC_ABS" -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"
rm -f "$TEST_DIR/lib.tk"

# Modify lib.tki to have mismatched compiler version
cat "$TEST_DIR/lib.tki" | sed 's/compiler_version: .*/compiler_version: 99.9.9/' > "$TEST_DIR/temp.tki"
mv "$TEST_DIR/temp.tki" "$TEST_DIR/lib.tki"

if "$TOKAC_ABS" "$TEST_DIR/main.tk" "$TEST_DIR/lib.o" -o "$TEST_DIR/main_app_4" 2> "$TEST_DIR/err4.txt"; then
    echo "FAIL: Expected compilation to fail due to compiler version mismatch"
    exit 1
fi
if ! grep -q "Incompatible or stale interface file" "$TEST_DIR/err4.txt"; then
    echo "FAIL: Expected stale interface file error message"
    cat "$TEST_DIR/err4.txt"
    exit 1
fi
echo "PASS: Test 4"

# 5. Test case 5: No metadata rejection
echo "Test 5: Missing metadata rejection"
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF
"$TOKAC_ABS" -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"
rm -f "$TEST_DIR/lib.tk"

# Strip all metadata lines from lib.tki
cat "$TEST_DIR/lib.tki" | grep -v "^// @meta" > "$TEST_DIR/temp.tki"
mv "$TEST_DIR/temp.tki" "$TEST_DIR/lib.tki"

if "$TOKAC_ABS" "$TEST_DIR/main.tk" "$TEST_DIR/lib.o" -o "$TEST_DIR/main_app_5" 2> "$TEST_DIR/err5.txt"; then
    echo "FAIL: Expected compilation to fail due to missing metadata"
    exit 1
fi
if ! grep -q "Incompatible or stale interface file" "$TEST_DIR/err5.txt"; then
    echo "FAIL: Expected stale interface file error message"
    cat "$TEST_DIR/err5.txt"
    exit 1
fi
echo "PASS: Test 5"

# 6. Test case 6: Dependency dumping
echo "Test 6: Dependency dumping"
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF
cat << 'EOF' > "$TEST_DIR/main.tk"
import ./lib::{get_num}
fn main() -> i32 {
    return 0
}
EOF
# Run with --dump-dependencies
"$TOKAC_ABS" --dump-dependencies "$TEST_DIR/main.tk" > "$TEST_DIR/deps.txt"
if ! grep -q "main.tk:" "$TEST_DIR/deps.txt"; then
    echo "FAIL: Dependency output missing main.tk"
    cat "$TEST_DIR/deps.txt"
    exit 1
fi
if ! grep -q "lib.tk" "$TEST_DIR/deps.txt"; then
    echo "FAIL: Dependency output missing lib.tk"
    cat "$TEST_DIR/deps.txt"
    exit 1
fi
echo "PASS: Test 6"

# Clean up
rm -rf "$TEST_DIR"
echo "All TKI cache validation tests PASSED!"
