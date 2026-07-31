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
fn main() -> i32 { return get_val() }
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

# 7.7 Test std/core interface syntax round-trip for trait impls and declaration-only effects.
echo "Test 7.7: Round-tripping core/string.tki as a reusable interface"
rm -rf "$TEST_DIR/string_build"
mkdir -p "$TEST_DIR/string_build/objects" "$TEST_DIR/string_build/interfaces"

STRING_HASH=$(python3 -c '
import os
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
path = os.path.realpath("lib/core/string.tk").replace("\\\\", "/")
h = FNV_OFFSET
for b in path.encode():
    h ^= b
    h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
print(f"{h:016x}")
')
STRING_OBJ="$TEST_DIR/string_build/objects/$STRING_HASH.o"
STRING_TKI="$TEST_DIR/string_build/interfaces/$STRING_HASH.tki"

TOKA_BUILD_DIR="$TEST_DIR/string_build" "$TOKAC_ABS" -c lib/core/string.tk --emit-interface -o "$STRING_OBJ"
if [ ! -f "$STRING_TKI" ]; then
    echo "FAIL: Expected cached core/string.tki at $STRING_TKI"
    exit 1
fi
if ! grep -q "impl string@Encap" "$STRING_TKI"; then
    echo "FAIL: core/string.tki did not emit canonical Type@Trait impl syntax"
    exit 1
fi
if grep -q "nullptr" "$STRING_TKI"; then
    echo "FAIL: core/string.tki emitted non-Toka null pointer syntax"
    cat "$STRING_TKI"
    exit 1
fi

if ! "$TOKAC_ABS" --dump-dependencies=json "$STRING_TKI" > "$TEST_DIR/string_tki_roundtrip.json" 2> "$TEST_DIR/string_tki_roundtrip.err"; then
    echo "FAIL: core/string.tki could not be parsed as an interface root"
    cat "$TEST_DIR/string_tki_roundtrip.err"
    exit 1
fi

cat << 'EOF' > "$TEST_DIR/string_user.tk"
fn main() -> i32 {
    auto s = string::from("abc")
    if s.len() == 3 {
        return 0
    }
    return 1
}
EOF

if ! TOKA_BUILD_DIR="$TEST_DIR/string_build" "$TOKAC_ABS" "$TEST_DIR/string_user.tk" "$STRING_OBJ" -o "$TEST_DIR/string_user_app" 2> "$TEST_DIR/string_user.err"; then
    echo "FAIL: core/string.tki could not be used with its cached object"
    cat "$TEST_DIR/string_user.err"
    exit 1
fi
"$TEST_DIR/string_user_app"
echo "PASS: Test 7.7"

# 7.8 Test cached core interfaces keep their original parser context.
echo "Test 7.8: Preserving core source context while parsing cached interfaces"
rm -rf "$TEST_DIR/interface_context_build"
mkdir -p "$TEST_DIR/interface_context_build/objects" "$TEST_DIR/interface_context_build/interfaces"

OPTION_HASH=$(python3 -c '
import os
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
path = os.path.realpath("lib/core/option.tk").replace("\\\\", "/")
h = FNV_OFFSET
for b in path.encode():
    h ^= b
    h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
print(f"{h:016x}")
')
OPTION_OBJ="$TEST_DIR/interface_context_build/objects/$OPTION_HASH.o"
OPTION_TKI="$TEST_DIR/interface_context_build/interfaces/$OPTION_HASH.tki"

TOKA_BUILD_DIR="$TEST_DIR/interface_context_build" "$TOKAC_ABS" -c lib/core/option.tk --emit-interface -o "$OPTION_OBJ"
if [ ! -f "$OPTION_TKI" ]; then
    echo "FAIL: Expected cached core/option.tki at $OPTION_TKI"
    exit 1
fi

TOKA_BUILD_DIR="$TEST_DIR/interface_context_build" "$TOKAC_ABS" --dump-dependencies=json -c lib/core/traits.tk > "$TEST_DIR/traits_deps.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})
assert any(path.endswith("/interfaces/'"$OPTION_HASH"'.tki") for path in modules), "core/traits should use cached core/option.tki"
assert not any(path.endswith("/lib/core/prelude.tk") for path in modules), "core interface parsing must not inject implicit prelude"
option_key = next(path for path in modules if path.endswith("/interfaces/'"$OPTION_HASH"'.tki"))
assert modules[option_key].get("kind") == "interface", "cached option module should remain an interface"
assert modules[option_key].get("dependencies") == [], "core/option.tki should not gain implicit dependencies"
' < "$TEST_DIR/traits_deps.json"

if [ $? -ne 0 ]; then
    echo "FAIL: Test 7.8 Python validation failed"
    cat "$TEST_DIR/traits_deps.json"
    exit 1
fi
echo "PASS: Test 7.8"

# 7.9 Test core type interfaces preserve strong aliases and support the string stack.
echo "Test 7.9: Caching core type and string stack interfaces"
rm -rf "$TEST_DIR/core_stack_build"
mkdir -p "$TEST_DIR/core_stack_build/objects" "$TEST_DIR/core_stack_build/interfaces"

TYPES_HASH=$(python3 -c '
import os
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
path = os.path.realpath("lib/core/types.tk").replace("\\\\", "/")
h = FNV_OFFSET
for b in path.encode():
    h ^= b
    h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
print(f"{h:016x}")
')
TYPES_TKI="$TEST_DIR/core_stack_build/interfaces/$TYPES_HASH.tki"

if ! TOKA_BUILD_DIR="$TEST_DIR/core_stack_build" "$TOKAC_ABS" -c lib/core/types.tk -o "$TEST_DIR/core_stack_build/objects/types.o" 2> "$TEST_DIR/core_stack_types.err"; then
    echo "FAIL: core/types.tk could not be compiled as a cached module"
    cat "$TEST_DIR/core_stack_types.err"
    exit 1
fi
if ! grep -q "pub type char = i8" "$TYPES_TKI"; then
    echo "FAIL: core/types.tki did not preserve strong type syntax for char"
    cat "$TYPES_TKI"
    exit 1
fi
if grep -q "__Toka_Anon_Rec_" "$TYPES_TKI"; then
    echo "FAIL: core/types.tki leaked an internal anonymous record type name"
    cat "$TYPES_TKI"
    exit 1
fi

for module in lib/core/internal/runtime.tk lib/core/utf8.tk lib/core/str.tk lib/core/string.tk; do
    stem=$(basename "$module" .tk)
    if ! TOKA_BUILD_DIR="$TEST_DIR/core_stack_build" "$TOKAC_ABS" -c "$module" -o "$TEST_DIR/core_stack_build/objects/$stem.o" 2> "$TEST_DIR/core_stack_$stem.err"; then
        echo "FAIL: $module could not be compiled using cached lower core interfaces"
        cat "$TEST_DIR/core_stack_$stem.err"
        exit 1
    fi
done
echo "PASS: Test 7.9"

# 7.10 Test cached simple imports keep the source import module name.
echo "Test 7.10: Cached simple import preserves logical module name"
rm -rf "$TEST_DIR/simple_import_build" "$TEST_DIR/simple_import"
mkdir -p "$TEST_DIR/simple_import_build/objects" "$TEST_DIR/simple_import_build/interfaces" "$TEST_DIR/simple_import"

cat << 'EOF' > "$TEST_DIR/simple_import/lib.tk"
import sys/libc::{libc_strlen}

pub const CACHE_LIMITS = (
    u8 = (
        max = 255:u8
    )
)

alias LocalValue = i32

shape LocalCounter(val: i32)

impl LocalCounter {
    fn inc(self#) {
        self.val += 1
    }
}

pub shape LocalBox<'T>(value: T)

impl<'T> LocalBox<'T> {
    pub fn marker(self) -> i32 {
        auto c# = LocalCounter(val = 0)
        if libc_strlen(c"cached\n") == 7:usize {
            c#.inc()
        }
        return c.val
    }
}

pub fn value() -> i32 {
    return 7
}

pub fn aliased_value() -> LocalValue {
    if libc_strlen(c"probe\n") == 6:usize {
        return 11
    }
    return 0
}
EOF

cat << 'EOF' > "$TEST_DIR/simple_import/main.tk"
import ./lib
import ./lib::{LocalBox}

fn main() -> i32 {
    auto b = LocalBox<i32>(value = 3)
    if lib::value() == 7 && lib::aliased_value() == 11 && b.marker() == 1 {
        return 0
    }
    return 1
}
EOF

LIB_HASH=$(python3 -c '
import os
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
path = os.path.realpath("'"$TEST_DIR"'/simple_import/lib.tk").replace("\\\\", "/")
h = FNV_OFFSET
for b in path.encode():
    h ^= b
    h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
print(f"{h:016x}")
')
LIB_OBJ="$TEST_DIR/simple_import_build/objects/$LIB_HASH.o"

TOKA_BUILD_DIR="$TEST_DIR/simple_import_build" "$TOKAC_ABS" -c "$TEST_DIR/simple_import/lib.tk" -o "$LIB_OBJ"
if ! TOKA_BUILD_DIR="$TEST_DIR/simple_import_build" "$TOKAC_ABS" "$TEST_DIR/simple_import/main.tk" "$LIB_OBJ" -o "$TEST_DIR/simple_import_app" 2> "$TEST_DIR/simple_import.err"; then
    echo "FAIL: cached simple import did not bind the logical module name"
    cat "$TEST_DIR/simple_import.err"
    exit 1
fi
"$TEST_DIR/simple_import_app"
echo "PASS: Test 7.10"

# 7.11 Test archive-backed cache mode uses cached interfaces in link mode.
echo "Test 7.11: Archive-backed cache mode uses cached interfaces"
AR_TOOL="${AR:-$(command -v llvm-ar 2>/dev/null || command -v ar 2>/dev/null || true)}"
if [ -z "$AR_TOOL" ]; then
    echo "FAIL: llvm-ar or ar is required for cache archive validation"
    exit 1
fi
LIB_ARCHIVE="$TEST_DIR/simple_import_build/libsimple_cache.a"
"$AR_TOOL" rcs "$LIB_ARCHIVE" "$LIB_OBJ"

cat << 'EOF' > "$TEST_DIR/simple_import/const_main.tk"
import ./lib::{CACHE_LIMITS}

fn main() -> i32 {
    if CACHE_LIMITS.u8.max == 255:u8 {
        return 0
    }
    return 1
}
EOF

if ! TOKA_BUILD_DIR="$TEST_DIR/simple_import_build" TOKA_USE_LIB_CACHE=1 "$TOKAC_ABS" --dump-dependencies=json "$TEST_DIR/simple_import/main.tk" > "$TEST_DIR/simple_import_archive_deps.json" 2> "$TEST_DIR/simple_import_archive_deps.err"; then
    echo "FAIL: archive-backed cache mode could not dump dependencies"
    cat "$TEST_DIR/simple_import_archive_deps.err"
    exit 1
fi

python3 -c '
import json, sys
data = json.load(sys.stdin)
modules = data.get("modules", {})
assert any(path.endswith("/interfaces/'"$LIB_HASH"'.tki") for path in modules), "cached lib interface was not used in link-mode cache"
' < "$TEST_DIR/simple_import_archive_deps.json"

if [ $? -ne 0 ]; then
    echo "FAIL: archive-backed cache mode did not use the cached interface"
    cat "$TEST_DIR/simple_import_archive_deps.json"
    exit 1
fi

if ! TOKA_BUILD_DIR="$TEST_DIR/simple_import_build" TOKA_USE_LIB_CACHE=1 "$TOKAC_ABS" "$TEST_DIR/simple_import/main.tk" "$LIB_ARCHIVE" -o "$TEST_DIR/simple_import_archive_app" 2> "$TEST_DIR/simple_import_archive.err"; then
    echo "FAIL: archive-backed cache mode could not link the cached object"
    cat "$TEST_DIR/simple_import_archive.err"
    exit 1
fi
"$TEST_DIR/simple_import_archive_app"

if ! TOKA_BUILD_DIR="$TEST_DIR/simple_import_build" TOKA_USE_LIB_CACHE=1 "$TOKAC_ABS" "$TEST_DIR/simple_import/const_main.tk" "$LIB_ARCHIVE" -o "$TEST_DIR/simple_import_const_archive_app" 2> "$TEST_DIR/simple_import_const_archive.err"; then
    echo "FAIL: archive-backed cache mode could not link cached anonymous record constants"
    cat "$TEST_DIR/simple_import_const_archive.err"
    exit 1
fi
"$TEST_DIR/simple_import_const_archive_app"
echo "PASS: Test 7.11"

# 7.12 Test source-less standalone interfaces keep const definitions.
echo "Test 7.12: Source-less standalone interfaces keep const definitions"
mkdir -p "$TEST_DIR/sourceless_interface"

cat << 'EOF' > "$TEST_DIR/sourceless_interface/lib.tki"
// @meta compiler_version: any
// @meta format_version: 2
// @meta target_triple: any
// @meta source_hash: any
// @meta identity_schema_version: 2
// @meta logical_module_path: unbound
// @meta resolver_binding_digest: unbound

pub const SOURCELESS_CONST: i32 = 42
EOF

cat << 'EOF' > "$TEST_DIR/sourceless_interface/main.tk"
import ./lib::{SOURCELESS_CONST}

fn main() -> i32 {
    if SOURCELESS_CONST == 42 {
        return 0
    }
    return 1
}
EOF

if ! "$TOKAC_ABS" "$TEST_DIR/sourceless_interface/main.tk" -o "$TEST_DIR/sourceless_interface_app" 2> "$TEST_DIR/sourceless_interface.err"; then
    echo "FAIL: source-less interface const was not emitted as a definition"
    cat "$TEST_DIR/sourceless_interface.err"
    exit 1
fi
"$TEST_DIR/sourceless_interface_app"
echo "PASS: Test 7.12"

# 7.13 Test associated type projections survive source-less .tki replay.
echo "Test 7.13: Associated type projection through source-less interface"
rm -rf "$TEST_DIR/associated_type_interface"
mkdir -p "$TEST_DIR/associated_type_interface"

cat << 'EOF' > "$TEST_DIR/associated_type_interface/lib.tk"
pub trait @Readable {
    type Item
    pub fn read(self) -> Item
}

pub shape IntBox(value: i32)

impl IntBox@Readable {
    type Item = i32

    pub fn read(self) -> Item {
        auto tmp: Item = self.value
        return tmp
    }
}
EOF

cat << 'EOF' > "$TEST_DIR/associated_type_interface/main.tk"
import ./lib::{IntBox, @Readable}

fn accept_item(x: IntBox@Readable::Item) -> i32 {
    return x
}

fn main() -> i32 {
    auto box = IntBox(value = 41)
    return accept_item(box.read()) - 41
}
EOF

"$TOKAC_ABS" -c "$TEST_DIR/associated_type_interface/lib.tk" -o "$TEST_DIR/associated_type_interface/lib.o"
if [ ! -f "$TEST_DIR/associated_type_interface/lib.tki" ]; then
    echo "FAIL: associated type interface lib.tki was not generated"
    exit 1
fi
if ! grep -q "type Item" "$TEST_DIR/associated_type_interface/lib.tki"; then
    echo "FAIL: associated type declaration was not emitted to lib.tki"
    cat "$TEST_DIR/associated_type_interface/lib.tki"
    exit 1
fi
if ! grep -q "type Item = i32" "$TEST_DIR/associated_type_interface/lib.tki"; then
    echo "FAIL: associated type implementation was not emitted to lib.tki"
    cat "$TEST_DIR/associated_type_interface/lib.tki"
    exit 1
fi

mv "$TEST_DIR/associated_type_interface/lib.tk" "$TEST_DIR/associated_type_interface/lib.tk.bak"

if ! "$TOKAC_ABS" "$TEST_DIR/associated_type_interface/main.tk" "$TEST_DIR/associated_type_interface/lib.o" -o "$TEST_DIR/associated_type_interface_app" 2> "$TEST_DIR/associated_type_interface.err"; then
    echo "FAIL: associated type projection could not be resolved from source-less lib.tki"
    cat "$TEST_DIR/associated_type_interface.err"
    exit 1
fi
"$TEST_DIR/associated_type_interface_app"
echo "PASS: Test 7.13"

# 7.14 Test pub import re-exports survive source-less .tki replay.
echo "Test 7.14: pub import re-export through source-less interface"
rm -rf "$TEST_DIR/pub_import_interface"
mkdir -p "$TEST_DIR/pub_import_interface"

cat << 'EOF' > "$TEST_DIR/pub_import_interface/base.tk"
pub fn base_value() -> i32 {
    return 77
}
EOF

cat << 'EOF' > "$TEST_DIR/pub_import_interface/reexport.tk"
pub import ./base::{base_value}
EOF

cat << 'EOF' > "$TEST_DIR/pub_import_interface/main.tk"
import ./reexport::{base_value}

fn main() -> i32 {
    return base_value() - 77
}
EOF

"$TOKAC_ABS" -c "$TEST_DIR/pub_import_interface/base.tk" -o "$TEST_DIR/pub_import_interface/base.o"
"$TOKAC_ABS" -c "$TEST_DIR/pub_import_interface/reexport.tk" -o "$TEST_DIR/pub_import_interface/reexport.o"

if [ ! -f "$TEST_DIR/pub_import_interface/reexport.tki" ]; then
    echo "FAIL: pub import reexport.tki was not generated"
    exit 1
fi
if ! grep -q "pub import ./base" "$TEST_DIR/pub_import_interface/reexport.tki"; then
    echo "FAIL: pub import was not emitted to reexport.tki"
    cat "$TEST_DIR/pub_import_interface/reexport.tki"
    exit 1
fi
if ! grep -q "base_value" "$TEST_DIR/pub_import_interface/reexport.tki"; then
    echo "FAIL: re-exported item was not emitted to reexport.tki"
    cat "$TEST_DIR/pub_import_interface/reexport.tki"
    exit 1
fi

mv "$TEST_DIR/pub_import_interface/base.tk" "$TEST_DIR/pub_import_interface/base.tk.bak"
mv "$TEST_DIR/pub_import_interface/reexport.tk" "$TEST_DIR/pub_import_interface/reexport.tk.bak"

if ! "$TOKAC_ABS" \
    "$TEST_DIR/pub_import_interface/main.tk" \
    "$TEST_DIR/pub_import_interface/base.o" \
    "$TEST_DIR/pub_import_interface/reexport.o" \
    -o "$TEST_DIR/pub_import_interface_app" \
    2> "$TEST_DIR/pub_import_interface.err"; then
    echo "FAIL: pub import re-export could not be resolved from source-less reexport.tki"
    cat "$TEST_DIR/pub_import_interface.err"
    exit 1
fi
"$TEST_DIR/pub_import_interface_app"
echo "PASS: Test 7.14"

# 7.15 Test dyn trait interfaces survive source-less .tki replay.
echo "Test 7.15: dyn trait through source-less interface"
rm -rf "$TEST_DIR/dyn_trait_interface"
mkdir -p "$TEST_DIR/dyn_trait_interface"

cat << 'EOF' > "$TEST_DIR/dyn_trait_interface/lib.tk"
pub trait @VisibleShape {
    pub fn public_id(self) -> i32
    fn private_id(self) -> i32
}

pub shape DynBox(value: i32)

impl DynBox@VisibleShape {
    pub fn public_id(self) -> i32 {
        return self.value
    }

    fn private_id(self) -> i32 {
        return self.value + 1
    }
}
EOF

cat << 'EOF' > "$TEST_DIR/dyn_trait_interface/main.tk"
import ./lib::{DynBox, @VisibleShape}

fn public_id_of(obj: dyn @VisibleShape) -> i32 {
    return obj.public_id()
}

fn main() -> i32 {
    auto box = DynBox(value = 33)
    return public_id_of(box) - 33
}
EOF

cat << 'EOF' > "$TEST_DIR/dyn_trait_interface/private_main.tk"
import ./lib::{DynBox, @VisibleShape}

fn private_id_of(obj: dyn @VisibleShape) -> i32 {
    return obj.private_id()
}

fn main() -> i32 {
    auto box = DynBox(value = 33)
    return private_id_of(box)
}
EOF

"$TOKAC_ABS" -c "$TEST_DIR/dyn_trait_interface/lib.tk" -o "$TEST_DIR/dyn_trait_interface/lib.o"
if [ ! -f "$TEST_DIR/dyn_trait_interface/lib.tki" ]; then
    echo "FAIL: dyn trait lib.tki was not generated"
    exit 1
fi
if ! grep -q "pub trait @VisibleShape" "$TEST_DIR/dyn_trait_interface/lib.tki"; then
    echo "FAIL: dyn trait declaration was not emitted to lib.tki"
    cat "$TEST_DIR/dyn_trait_interface/lib.tki"
    exit 1
fi
if ! grep -q "impl DynBox@VisibleShape" "$TEST_DIR/dyn_trait_interface/lib.tki"; then
    echo "FAIL: dyn trait implementation was not emitted to lib.tki"
    cat "$TEST_DIR/dyn_trait_interface/lib.tki"
    exit 1
fi

mv "$TEST_DIR/dyn_trait_interface/lib.tk" "$TEST_DIR/dyn_trait_interface/lib.tk.bak"

if ! "$TOKAC_ABS" "$TEST_DIR/dyn_trait_interface/main.tk" "$TEST_DIR/dyn_trait_interface/lib.o" -o "$TEST_DIR/dyn_trait_interface_app" 2> "$TEST_DIR/dyn_trait_interface.err"; then
    echo "FAIL: dyn trait public method could not be resolved from source-less lib.tki"
    cat "$TEST_DIR/dyn_trait_interface.err"
    exit 1
fi
"$TEST_DIR/dyn_trait_interface_app"

if "$TOKAC_ABS" "$TEST_DIR/dyn_trait_interface/private_main.tk" "$TEST_DIR/dyn_trait_interface/lib.o" -o "$TEST_DIR/dyn_trait_private_app" 2> "$TEST_DIR/dyn_trait_private.err"; then
    echo "FAIL: dyn trait private method unexpectedly resolved from source-less lib.tki"
    exit 1
fi
if ! grep -q "private" "$TEST_DIR/dyn_trait_private.err"; then
    echo "FAIL: expected dyn trait private method diagnostic"
    cat "$TEST_DIR/dyn_trait_private.err"
    exit 1
fi
echo "PASS: Test 7.15"

# 7.16 Test generic impl where constraints survive source-less .tki replay.
echo "Test 7.16: generic impl where through source-less interface"
rm -rf "$TEST_DIR/where_interface"
mkdir -p "$TEST_DIR/where_interface"

cat << 'EOF' > "$TEST_DIR/where_interface/lib.tk"
pub trait @Marked {}

pub shape Token(value: i32)

impl Token@Marked {}

pub shape Plain(value: i32)

pub shape Box<T>(item: T)

impl<T> Box<T>
where:
    T: @Marked
{
    pub fn marker(self) -> i32 {
        return 7
    }
}
EOF

cat << 'EOF' > "$TEST_DIR/where_interface/main.tk"
import ./lib::{Box, Token, @Marked}

fn main() -> i32 {
    auto box = Box<Token>(item = Token(value = 1))
    return box.marker() - 7
}
EOF

cat << 'EOF' > "$TEST_DIR/where_interface/bad_main.tk"
import ./lib::{Box, Plain, @Marked}

fn main() -> i32 {
    auto box = Box<Plain>(item = Plain(value = 1))
    return box.marker()
}
EOF

"$TOKAC_ABS" -c "$TEST_DIR/where_interface/lib.tk" -o "$TEST_DIR/where_interface/lib.o"
if [ ! -f "$TEST_DIR/where_interface/lib.tki" ]; then
    echo "FAIL: where interface lib.tki was not generated"
    exit 1
fi
if ! grep -q "impl<T: @Marked> Box<T>" "$TEST_DIR/where_interface/lib.tki"; then
    echo "FAIL: generic impl where constraint was not emitted to lib.tki"
    cat "$TEST_DIR/where_interface/lib.tki"
    exit 1
fi

mv "$TEST_DIR/where_interface/lib.tk" "$TEST_DIR/where_interface/lib.tk.bak"

if ! "$TOKAC_ABS" "$TEST_DIR/where_interface/main.tk" "$TEST_DIR/where_interface/lib.o" -o "$TEST_DIR/where_interface_app" 2> "$TEST_DIR/where_interface.err"; then
    echo "FAIL: generic impl where constraint could not be satisfied from source-less lib.tki"
    cat "$TEST_DIR/where_interface.err"
    exit 1
fi
"$TEST_DIR/where_interface_app"

if "$TOKAC_ABS" "$TEST_DIR/where_interface/bad_main.tk" "$TEST_DIR/where_interface/lib.o" -o "$TEST_DIR/where_bad_app" 2> "$TEST_DIR/where_bad.err"; then
    echo "FAIL: generic impl where constraint leaked to an unsatisfied type"
    exit 1
fi
if ! grep -q "no member named 'marker'" "$TEST_DIR/where_bad.err"; then
    echo "FAIL: expected unsatisfied where-bound method lookup diagnostic"
    cat "$TEST_DIR/where_bad.err"
    exit 1
fi
echo "PASS: Test 7.16"

# 7.17 Test @Encap visibility survives source-less .tki replay.
echo "Test 7.17: @Encap visibility through source-less interface"
rm -rf "$TEST_DIR/encap_interface"
mkdir -p "$TEST_DIR/encap_interface"

cat << 'EOF' > "$TEST_DIR/encap_interface/lib.tk"
pub shape VisibilityBox(
    open_val: i32,
    secret_val: i32
)

impl VisibilityBox@Encap {
    pub open_val

    fn drop(self#) {}
}

impl VisibilityBox {
    pub fn new() -> VisibilityBox {
        return VisibilityBox(open_val = 5, secret_val = 9)
    }
}
EOF

cat << 'EOF' > "$TEST_DIR/encap_interface/main.tk"
import ./lib::{VisibilityBox}

fn main() -> i32 {
    auto box = VisibilityBox::new()
    return box.open_val - 5
}
EOF

cat << 'EOF' > "$TEST_DIR/encap_interface/private_main.tk"
import ./lib::{VisibilityBox}

fn main() -> i32 {
    auto box = VisibilityBox::new()
    return box.secret_val
}
EOF

"$TOKAC_ABS" -c "$TEST_DIR/encap_interface/lib.tk" -o "$TEST_DIR/encap_interface/lib.o"
if [ ! -f "$TEST_DIR/encap_interface/lib.tki" ]; then
    echo "FAIL: encap interface lib.tki was not generated"
    exit 1
fi
if ! grep -q "impl VisibilityBox@Encap" "$TEST_DIR/encap_interface/lib.tki"; then
    echo "FAIL: @Encap impl was not emitted to lib.tki"
    cat "$TEST_DIR/encap_interface/lib.tki"
    exit 1
fi
if ! grep -q "pub open_val" "$TEST_DIR/encap_interface/lib.tki"; then
    echo "FAIL: @Encap public field entry was not emitted to lib.tki"
    cat "$TEST_DIR/encap_interface/lib.tki"
    exit 1
fi
if ! grep -q "secret_val: i32" "$TEST_DIR/encap_interface/lib.tki"; then
    echo "FAIL: private shape field structure was not preserved in lib.tki"
    cat "$TEST_DIR/encap_interface/lib.tki"
    exit 1
fi

mv "$TEST_DIR/encap_interface/lib.tk" "$TEST_DIR/encap_interface/lib.tk.bak"

if ! "$TOKAC_ABS" "$TEST_DIR/encap_interface/main.tk" "$TEST_DIR/encap_interface/lib.o" -o "$TEST_DIR/encap_interface_app" 2> "$TEST_DIR/encap_interface.err"; then
    echo "FAIL: @Encap public field could not be accessed from source-less lib.tki"
    cat "$TEST_DIR/encap_interface.err"
    exit 1
fi
"$TEST_DIR/encap_interface_app"

if "$TOKAC_ABS" "$TEST_DIR/encap_interface/private_main.tk" "$TEST_DIR/encap_interface/lib.o" -o "$TEST_DIR/encap_private_app" 2> "$TEST_DIR/encap_private.err"; then
    echo "FAIL: @Encap private field unexpectedly resolved from source-less lib.tki"
    exit 1
fi
if ! grep -q "private" "$TEST_DIR/encap_private.err"; then
    echo "FAIL: expected @Encap private field diagnostic"
    cat "$TEST_DIR/encap_private.err"
    exit 1
fi
echo "PASS: Test 7.17"

# Clean up
rm -rf "$TEST_DIR"
echo "All TKI cache validation tests PASSED!"
