#!/usr/bin/env bash
# Verify that removed shape-dependency syntax cannot re-enter through .tki.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
WORK_ROOT="${WORK_ROOT:-/tmp/toka_tki_excluded_syntax}"

if [[ "$TOKAC" = /* ]]; then
    TOKAC_ABS="$TOKAC"
elif [[ "$TOKAC" = */* ]]; then
    TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd)/$(basename "$TOKAC")"
else
    TOKAC_ABS="$(command -v "$TOKAC" 2>/dev/null || echo "$TOKAC")"
fi

rm -rf "$WORK_ROOT"
mkdir -p "$WORK_ROOT"

write_metadata() {
    local path="$1"
    {
        echo "// @meta compiler_version: any"
        echo "// @meta format_version: 2"
        echo "// @meta target_triple: any"
        echo "// @meta source_hash: any"
        echo "// @meta identity_schema_version: 2"
        echo "// @meta logical_module_path: unbound"
        echo "// @meta resolver_binding_digest: unbound"
        echo "// @meta source_path: $path.tk"
        echo
    } > "$path"
}

expect_rejected_interface() {
    local module="$1"
    local code="$2"
    cat > "$WORK_ROOT/$module-main.tk" <<EOF
import ./$module

fn main() -> i32 {
    return 0
}
EOF
    if "$TOKAC_ABS" -c "$WORK_ROOT/$module-main.tk" \
        -o "$WORK_ROOT/$module-main.o" \
        > "$WORK_ROOT/$module.out" 2> "$WORK_ROOT/$module.err"; then
        echo "FAIL: excluded syntax in $module.tki unexpectedly passed"
        exit 1
    fi
    if ! grep -Fq "$code" "$WORK_ROOT/$module.err"; then
        echo "FAIL: $module.tki did not report $code"
        cat "$WORK_ROOT/$module.err"
        exit 1
    fi
}

write_metadata "$WORK_ROOT/header.tki"
cat >> "$WORK_ROOT/header.tki" <<'EOF'
pub shape RefInt <- val(
    &val: i32
)
EOF

write_metadata "$WORK_ROOT/member.tki"
cat >> "$WORK_ROOT/member.tki" <<'EOF'
pub shape RefInt(
    owner: i32,
    &view: i32 <- owner
)
EOF

expect_rejected_interface header E01247
expect_rejected_interface member E01248

echo "PASS: excluded shape dependency syntax rejected from TKI"
