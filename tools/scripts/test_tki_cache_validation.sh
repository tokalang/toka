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

# 7. Test case 7: JSON dependency graph dumping and protocol validation
echo "Test 7: JSON dependency graph dumping and protocol validation"
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
cat << 'EOF' > "$TEST_DIR/main2.tk"
import ./lib::{get_num}
fn main() -> i32 {
    return 1
}
EOF

# 7.1 Test Multi-root outputs and roots array
"$TOKAC_ABS" --dump-dependencies=json --emit-interface "$TEST_DIR/main.tk" "$TEST_DIR/main2.tk" > "$TEST_DIR/multi_roots.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
roots = data.get("roots", [])
modules = data.get("modules", {})

# Check roots
assert len(roots) == 2, f"Expected 2 roots, got {roots}"
assert any(r.endswith("main.tk") for r in roots), "roots missing main.tk"
assert any(r.endswith("main2.tk") for r in roots), "roots missing main2.tk"

# Check modules
assert any(k.endswith("main.tk") for k in modules), "modules missing main.tk"
assert any(k.endswith("main2.tk") for k in modules), "modules missing main2.tk"
assert any(k.endswith("lib.tk") for k in modules), "modules missing lib.tk"

main_key = next(k for k in modules if k.endswith("main.tk"))
main2_key = next(k for k in modules if k.endswith("main2.tk"))
lib_key = next(k for k in modules if k.endswith("lib.tk"))

# Verify root outputs
main_outputs = modules[main_key]["outputs"]
assert main_outputs["interface"].endswith("main.tki"), f"Unexpected interface output: {main_outputs}"
assert main_outputs["executable"] == "", f"Executable output should be empty: {main_outputs}"
assert main_outputs["object"] == "", f"Object output should be empty: {main_outputs}"

main2_outputs = modules[main2_key]["outputs"]
assert main2_outputs["interface"].endswith("main2.tki"), f"Unexpected interface output: {main2_outputs}"
assert main2_outputs["executable"] == "", f"Executable output should be empty: {main2_outputs}"
assert main2_outputs["object"] == "", f"Object output should be empty: {main2_outputs}"

# Verify non-root output (lib.tk)
lib_outputs = modules[lib_key]["outputs"]
assert lib_outputs["interface"] == "", f"Lib interface should be empty: {lib_outputs}"
assert lib_outputs["object"] == "", f"Lib object should be empty: {lib_outputs}"
assert lib_outputs["executable"] == "", f"Lib executable should be empty: {lib_outputs}"

# Verify hashes are populated and identical for source files
for k in [main_key, main2_key, lib_key]:
    m = modules[k]
    assert m["source_hash"] != "", f"source_hash empty for {k}"
    assert m["content_hash"] != "", f"content_hash empty for {k}"
    assert m["source_hash"] == m["content_hash"], f"hashes differ for source file {k}"
' < "$TEST_DIR/multi_roots.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.1 Python validation failed"
    cat "$TEST_DIR/multi_roots.json"
    exit 1
fi

# 7.2 Test fallback triggering & status validation
# Compile lib.tk to lib.tki
"$TOKAC_ABS" --emit-interface -c -o "$TEST_DIR/lib.o" "$TEST_DIR/lib.tk"

# Corrupt lib.tki metadata (compiler version mismatch) to trigger fallback to lib.tk
python3 -c "
import re
p = '$TEST_DIR/lib.tki'
c = open(p).read()
c = re.sub(r'compiler_version:\s*\S+', 'compiler_version: 0.0.0', c)
open(p, 'w').write(c)
"

"$TOKAC_ABS" --dump-dependencies=json -c "$TEST_DIR/main.tk" > "$TEST_DIR/fallback.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})

lib_key = next((k for k in modules if k.endswith("lib.tk")), None)
assert lib_key is not None, "Fallback should resolve to lib.tk"
lib_module = modules[lib_key]
assert lib_module["fallback_triggered"] is True, "fallback_triggered should be true"
assert lib_module["cache_status"] == "CompilerVersionMismatch", f"Unexpected cache_status: {lib_module}"
' < "$TEST_DIR/fallback.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.2 Python validation failed"
    cat "$TEST_DIR/fallback.json"
    exit 1
fi

# 7.3 Test dual hashes distinction without fallback (remove .tk source)
# Create a fresh, valid lib.tki from lib.tk
"$TOKAC_ABS" --emit-interface -c -o "$TEST_DIR/lib.o" "$TEST_DIR/lib.tk"

# Move lib.tk away to prevent fallback
mv "$TEST_DIR/lib.tk" "$TEST_DIR/lib.tk.bak"

# Append spaces to lib.tki to change its physical content hash but keep original source_hash metadata
echo "   " >> "$TEST_DIR/lib.tki"

# Dump dependencies. Since lib.tk is not present, it will successfully load lib.tki as interface file
"$TOKAC_ABS" --dump-dependencies=json "$TEST_DIR/main.tk" > "$TEST_DIR/dual_hashes.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})

lib_key = next((k for k in modules if k.endswith("lib.tki")), None)
assert lib_key is not None, "Should resolve to lib.tki"
lib_module = modules[lib_key]
assert lib_module["kind"] == "interface", "kind should be interface"
assert lib_module["fallback_triggered"] is False, "fallback should not trigger"
assert lib_module["cache_status"] == "Ok", f"cache_status should be Ok: {lib_module}"

# source_hash is original source hash, content_hash is modified tki physical hash
assert lib_module["source_hash"] != "", "source_hash empty"
assert lib_module["content_hash"] != "", "content_hash empty"
assert lib_module["source_hash"] != lib_module["content_hash"], f"hashes should differ: {lib_module}"
' < "$TEST_DIR/dual_hashes.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.3 Python validation failed"
    cat "$TEST_DIR/dual_hashes.json"
    exit 1
fi

# 7.4 Test multi-root interface output clash rejection
echo "Test 7.4: Multi-root single output interface emission clash rejection"
if "$TOKAC_ABS" --emit-interface -o "$TEST_DIR/out" "$TEST_DIR/main.tk" "$TEST_DIR/main2.tk" 2> "$TEST_DIR/clash_err.txt"; then
    echo "FAIL: Expected multi-root single output interface emission to fail, but it succeeded"
    exit 1
fi
if ! grep -q "cannot emit multiple interfaces to a single output path" "$TEST_DIR/clash_err.txt"; then
    echo "FAIL: Expected clash error message, but got:"
    cat "$TEST_DIR/clash_err.txt"
    exit 1
fi
echo "PASS: Test 7.4"

# 7.4.2 Test multi-root interface output clash rejection without -o
echo "Test 7.4.2: Multi-root single output interface emission clash rejection without -o"
mkdir -p "$TEST_DIR/a" "$TEST_DIR/b"
touch "$TEST_DIR/a/main.tk"
touch "$TEST_DIR/b/main.tk"
if "$TOKAC_ABS" --emit-interface "$TEST_DIR/a/main.tk" "$TEST_DIR/b/main.tk" 2> "$TEST_DIR/clash_err_no_o.txt"; then
    echo "FAIL: Expected multi-root no-o interface emission to fail, but it succeeded"
    exit 1
fi
if ! grep -q "cannot emit multiple interfaces to a single output path" "$TEST_DIR/clash_err_no_o.txt"; then
    echo "FAIL: Expected clash error message, but got:"
    cat "$TEST_DIR/clash_err_no_o.txt"
    exit 1
fi
echo "PASS: Test 7.4.2"

# 7.5 Test package alias preferSource alignment
echo "Test 7.5: Package alias preferSource alignment"
# Recover lib.tk source
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_num() -> i32 {
    return 100
}
EOF
# Re-create fresh, valid lib.tki from lib.tk
"$TOKAC_ABS" --emit-interface -c -o "$TEST_DIR/lib.o" "$TEST_DIR/lib.tk"

# Corrupt lib.tki metadata (compiler version mismatch) to trigger fallback to lib.tk
python3 -c "
import re
p = '$TEST_DIR/lib.tki'
c = open(p).read()
c = re.sub(r'compiler_version:\s*\S+', 'compiler_version: 0.0.0', c)
open(p, 'w').write(c)
"

# Create main_pkg.tk importing via mylib alias
cat << 'EOF' > "$TEST_DIR/main_pkg.tk"
import mylib::{get_num}
fn main() -> i32 {
    if (get_num() == 100) {
        return 0
    }
    return 1
}
EOF

# 7.5.1 In standard compile/link mode: should preferSource = true (ignore stale lib.tki, use lib.tk directly, link successfully)
"$TOKAC_ABS" --pkg "mylib=$TEST_DIR/lib" "$TEST_DIR/main_pkg.tk" -o "$TEST_DIR/out_pkg"
if [ ! -f "$TEST_DIR/out_pkg" ]; then
    echo "FAIL: Test 7.5.1 compilation link failed"
    exit 1
fi
# Run out_pkg to verify it executes successfully and returns 0
if ! "$TEST_DIR/out_pkg"; then
    echo "FAIL: Test 7.5.1 execution failed or returned non-zero status"
    exit 1
fi
echo "PASS: Test 7.5.1"

# 7.5.2 In dependency dump mode: should preferSource = false (read lib.tki, trigger fallback to lib.tk)
"$TOKAC_ABS" --dump-dependencies=json -c --pkg "mylib=$TEST_DIR/lib" "$TEST_DIR/main_pkg.tk" > "$TEST_DIR/pkg_fallback.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})

lib_key = next((k for k in modules if k.endswith("lib.tk")), None)
assert lib_key is not None, "Alias fallback should resolve to lib.tk"
lib_module = modules[lib_key]
assert lib_module["fallback_triggered"] is True, "fallback_triggered should be true for alias"
assert lib_module["cache_status"] == "CompilerVersionMismatch", f"Unexpected cache_status: {lib_module}"
' < "$TEST_DIR/pkg_fallback.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.5.2 Python validation failed"
    cat "$TEST_DIR/pkg_fallback.json"
    exit 1
fi
echo "PASS: Test 7.5.2"

echo "PASS: Test 7"

# 7.6 Test source_path metadata used for source_hash verification in hash-cached .tki
echo "Test 7.6: Using source_path in validateTKIMetadata for source_hash verification"
cat << 'EOF' > "$TEST_DIR/dep.tk"
pub fn get_val() -> i32 { return 42 }
EOF
cat << 'EOF' > "$TEST_DIR/main_cache.tk"
import ./dep::{get_val}
fn main() -> i32 { return 0 }
EOF

# Ensure clean build dir
rm -rf "$TEST_DIR/build"
mkdir -p "$TEST_DIR/build/objects" "$TEST_DIR/build/interfaces"

# Compile first to generate expected .tki in custom TOKA_BUILD_DIR
TOKA_BUILD_DIR="$TEST_DIR/build" "$TOKAC_ABS" -c "$TEST_DIR/dep.tk" --emit-interface -o "$TEST_DIR/build/objects/dep.o"
TOKA_BUILD_DIR="$TEST_DIR/build" "$TOKAC_ABS" -c "$TEST_DIR/main_cache.tk" --emit-interface -o "$TEST_DIR/build/objects/main_cache.o"

# Verify the .tki was indeed created in custom build directory
TKI_COUNT=$(find "$TEST_DIR/build/interfaces" -name "*.tki" | wc -l)
if [ "$TKI_COUNT" -ne 2 ]; then
    echo "FAIL: Expected 2 cached .tki in custom build dir, found $TKI_COUNT"
    exit 1
fi

# Modify the source file dep.tk to invalidate the cached source_hash
cat << 'EOF' > "$TEST_DIR/dep.tk"
pub fn get_val() -> i32 { return 43 }
EOF

# Dump dependencies in compile-only mode (preferSource = false) to trigger interface check
TOKA_BUILD_DIR="$TEST_DIR/build" "$TOKAC_ABS" --dump-dependencies=json -c "$TEST_DIR/main_cache.tk" > "$TEST_DIR/source_hash_mismatch.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})

dep_key = next((k for k in modules if k.endswith("dep.tk")), None)
assert dep_key is not None, "Should resolve and fallback to dep.tk"
dep_module = modules[dep_key]
assert dep_module["fallback_triggered"] is True, "fallback_triggered should be true for changed source"
assert dep_module["cache_status"] == "SourceHashMismatch", f"Unexpected cache_status: {dep_module}"
' < "$TEST_DIR/source_hash_mismatch.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.6 Python validation failed"
    cat "$TEST_DIR/source_hash_mismatch.json"
    exit 1
fi
echo "PASS: Test 7.6"

# Clean up
rm -rf "$TEST_DIR"
echo "All TKI cache validation tests PASSED!"
