#!/usr/bin/env bash
# Verify that untrusted source-less interfaces cannot forge unsafe API exemptions.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
TEST_DIR="${TEST_DIR:-./tmp/tki_unsafe_revalidation_test}"

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

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"
TEST_DIR_ABS="$(cd "$TEST_DIR" && pwd)"

write_metadata() {
    local path="$1"
    local claimed_source="$2"
    {
        echo "// @meta compiler_version: any"
        echo "// @meta format_version: 2"
        echo "// @meta target_triple: any"
        echo "// @meta source_hash: any"
        echo "// @meta identity_schema_version: 2"
        echo "// @meta logical_module_path: unbound"
        echo "// @meta resolver_binding_digest: unbound"
        echo "// @meta source_path: $claimed_source"
        echo
    } > "$path"
}

write_metadata "$TEST_DIR/param.tki" "$TEST_DIR_ABS/forged/lib/param.tk"
echo "pub fn accept(ptr: *i32)" >> "$TEST_DIR/param.tki"

write_metadata "$TEST_DIR/return.tki" "$TEST_DIR_ABS/forged/prelude.tk"
echo "pub fn get() -> *i32" >> "$TEST_DIR/return.tki"

write_metadata "$TEST_DIR/field.tki" "$TEST_DIR_ABS/forged/tests/pass/field.tk"
cat >> "$TEST_DIR/field.tki" <<'EOF'
pub shape Point(
  *ptr: i32
)
EOF

write_metadata "$TEST_DIR/build_path.tki" "$TEST_DIR_ABS/forged/build.tk"
echo "pub fn expose(ptr: *i32)" >> "$TEST_DIR/build_path.tki"

write_metadata "$TEST_DIR/generic_param.tki" \
    "$TEST_DIR_ABS/untrusted/generic_param.tk"
echo "pub fn expose<T>(ptr: *T)" >> "$TEST_DIR/generic_param.tki"

write_metadata "$TEST_DIR/generic_return.tki" \
    "$TEST_DIR_ABS/untrusted/generic_return.tk"
echo "pub fn get<T>() -> *T" >> "$TEST_DIR/generic_return.tki"

write_metadata "$TEST_DIR/generic_field.tki" \
    "$TEST_DIR_ABS/untrusted/generic_field.tk"
cat >> "$TEST_DIR/generic_field.tki" <<'EOF'
pub shape Box<T>(
  *ptr: T
)
EOF

write_metadata "$TEST_DIR/generic_method.tki" \
    "$TEST_DIR_ABS/untrusted/generic_method.tk"
cat >> "$TEST_DIR/generic_method.tki" <<'EOF'
pub shape Holder<T>()

impl<T> Holder<T> {
  pub fn expose(ptr: *T)
}
EOF

write_metadata "$TEST_DIR/explicit.tki" "$TEST_DIR_ABS/untrusted/explicit.tk"
cat >> "$TEST_DIR/explicit.tki" <<'EOF'
pub shape RawPoint(
  *ptr: i32
)

impl RawPoint@encap {
  fn drop(self#)
  pub fn clone(self) = delete
}

pub fn unsafe_accept(ptr: *i32)
pub fn raw_get() -> *i32
EOF

write_consumer() {
    local module="$1"
    cat > "$TEST_DIR/$module-main.tk" <<EOF
import ./$module

fn main() -> i32 {
    return 0
}
EOF
}

expect_error() {
    local module="$1"
    local code="$2"
    write_consumer "$module"
    if "$TOKAC_ABS" -c "$TEST_DIR/$module-main.tk" \
        -o "$TEST_DIR/$module-main.o" \
        > "$TEST_DIR/$module.out" 2> "$TEST_DIR/$module.err"; then
        echo "FAIL: forged $module interface unexpectedly passed"
        exit 1
    fi
    if ! grep -Fq "$code" "$TEST_DIR/$module.err"; then
        echo "FAIL: forged $module interface did not report $code"
        cat "$TEST_DIR/$module.err"
        exit 1
    fi
}

expect_error param E0480
expect_error return E0481
expect_error field E0482
expect_error build_path E0480
expect_error generic_param E0480
expect_error generic_return E0481
expect_error generic_field E0482
expect_error generic_method E0480

mkdir -p "$TEST_DIR/include"
write_metadata "$TEST_DIR/include/include_path.tki" \
    "$TEST_DIR_ABS/forged/lib/include_path.tk"
echo "pub fn expose(ptr: *i32)" >> "$TEST_DIR/include/include_path.tki"
cat > "$TEST_DIR/include-main.tk" <<'EOF'
import include_path

fn main() -> i32 {
    return 0
}
EOF
if "$TOKAC_ABS" -I "$TEST_DIR/include" -c "$TEST_DIR/include-main.tk" \
    -o "$TEST_DIR/include-main.o" \
    > "$TEST_DIR/include.out" 2> "$TEST_DIR/include.err"; then
    echo "FAIL: ordinary include path incorrectly granted system trust"
    exit 1
fi
if ! grep -Fq "E0480" "$TEST_DIR/include.err"; then
    echo "FAIL: ordinary include path did not revalidate unsafe API"
    cat "$TEST_DIR/include.err"
    exit 1
fi

write_consumer explicit
if ! "$TOKAC_ABS" -c "$TEST_DIR/explicit-main.tk" \
    -o "$TEST_DIR/explicit-main.o" \
    > "$TEST_DIR/explicit.out" 2> "$TEST_DIR/explicit.err"; then
    echo "FAIL: explicit unsafe/raw interface exemptions were rejected"
    cat "$TEST_DIR/explicit.err"
    exit 1
fi

mkdir -p "$TEST_DIR/trusted"
write_metadata "$TEST_DIR/trusted/system_api.tki" \
    "$TEST_DIR_ABS/untrusted-metadata/system_api.tk"
echo "pub fn legacy_system_call(ptr: *i32)" \
    >> "$TEST_DIR/trusted/system_api.tki"
cat > "$TEST_DIR/trusted-main.tk" <<'EOF'
import system_api

fn main() -> i32 {
    return 0
}
EOF
if ! TOKA_LIB="$TEST_DIR/trusted" "$TOKAC_ABS" \
    -c "$TEST_DIR/trusted-main.tk" -o "$TEST_DIR/trusted-main.o" \
    > "$TEST_DIR/trusted.out" 2> "$TEST_DIR/trusted.err"; then
    echo "FAIL: compiler-configured system interface was not trusted"
    cat "$TEST_DIR/trusted.err"
    exit 1
fi

echo "PASS: untrusted TKI unsafe API revalidation"
