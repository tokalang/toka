#!/bin/bash

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
if [[ "$TOKAC" != /* ]]; then
    TOKAC="$(cd "$(dirname "$TOKAC")" && pwd)/$(basename "$TOKAC")"
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka-mixed-core-cache.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
mkdir -p "$WORK_DIR/objects" "$WORK_DIR/interfaces"

CORE_OBJECT="$WORK_DIR/objects/toka_core_batch.o"
TOKA_BUILD_DIR="$WORK_DIR" "$TOKAC" -c \
    lib/core/option.tk \
    lib/core/traits.tk \
    lib/core/types.tk \
    lib/core/result.tk \
    lib/core/utf8.tk \
    lib/core/memory.tk \
    lib/core/internal/runtime.tk \
    lib/core/str.tk \
    lib/core/string.tk \
    lib/core/task.tk \
    lib/core/prelude.tk \
    lib/sys/libc.tk \
    lib/std/vec.tk \
    -o "$CORE_OBJECT"

cat << 'EOF' > "$WORK_DIR/main.tk"
import std/fs::{metadata, read_to_string}
import std/vec::{Vec}

fn main() -> i32 {
    auto values# = Vec<i32>::new()
    values#.push(7)
    if values.len() != 1:usize { return 4 }

    auto path = string::from("/toka/mixed-cache/path-that-must-not-exist")
    if metadata(path).is_ok() { return 1 }
    auto contents = read_to_string(path)
    if contents.is_ok() { return 2 }
    auto message = contents.unwrap_err()
    if message.as_str().len() == 0 { return 3 }
    return 0
}
EOF

TOKA_BUILD_DIR="$WORK_DIR" TOKA_USE_LIB_CACHE=1 \
    "$TOKAC" --dump-dependencies=json "$CORE_OBJECT" "$WORK_DIR/main.tk" \
    > "$WORK_DIR/dependencies.json"

python3 - "$WORK_DIR/dependencies.json" << 'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    modules = json.load(stream)["modules"]

def one(suffix):
    matches = [(path, info) for path, info in modules.items()
               if info.get("shadow_coordinate", {}).get("logical_module_path") == suffix]
    assert len(matches) == 1, (suffix, matches)
    return matches[0]

for logical in ("core/string", "core/result", "std/vec"):
    path, info = one(logical)
    assert info["kind"] == "interface", (logical, path, info["kind"])

for logical in ("std/io", "std/fs"):
    path, info = one(logical)
    assert info["kind"] == "source", (logical, path, info["kind"])
PY

TOKA_BUILD_DIR="$WORK_DIR" TOKA_USE_LIB_CACHE=1 \
    "$TOKAC" "$CORE_OBJECT" "$WORK_DIR/main.tk" -o "$WORK_DIR/app"
"$WORK_DIR/app"

cat << 'EOF' > "$WORK_DIR/conflict.tk"
extern fn toka_panic(msg: Addr, len: i32) -> void

fn main() -> i32 {
    unsafe { toka_panic(0:Addr, 0) }
    return 0
}
EOF

if TOKA_BUILD_DIR="$WORK_DIR" TOKA_USE_LIB_CACHE=1 \
    "$TOKAC" "$CORE_OBJECT" "$WORK_DIR/conflict.tk" \
    -o "$WORK_DIR/conflict-app" 2> "$WORK_DIR/conflict.err"; then
    echo "FAIL: conflicting cached extern signature was accepted" >&2
    exit 1
fi
if ! grep -q "E0759" "$WORK_DIR/conflict.err"; then
    echo "FAIL: expected E0759 extern signature conflict diagnostic" >&2
    cat "$WORK_DIR/conflict.err" >&2
    exit 1
fi
if grep -q "LLVM IR Verification Failed" "$WORK_DIR/conflict.err"; then
    echo "FAIL: extern conflict reached the LLVM verifier" >&2
    cat "$WORK_DIR/conflict.err" >&2
    exit 1
fi

WINDOWS_DIR="$WORK_DIR/windows"
mkdir -p "$WINDOWS_DIR/objects" "$WINDOWS_DIR/interfaces"
WINDOWS_OBJECT="$WINDOWS_DIR/objects/toka_core_batch.obj"
TOKA_BUILD_DIR="$WINDOWS_DIR" "$TOKAC" --target x86_64-pc-windows-msvc -c \
    lib/core/option.tk \
    lib/core/traits.tk \
    lib/core/types.tk \
    lib/core/result.tk \
    lib/core/utf8.tk \
    lib/core/memory.tk \
    lib/core/internal/runtime.tk \
    lib/core/str.tk \
    lib/core/string.tk \
    lib/core/task.tk \
    lib/core/prelude.tk \
    lib/sys/libc.tk \
    lib/std/vec.tk \
    -o "$WINDOWS_OBJECT"

cat << 'EOF' > "$WINDOWS_DIR/winsock.tk"
import sys/os/abi::*

fn main() -> i32 {
    unsafe {
        auto null_addr = 0:Addr
        auto sent = send(0, null_addr as *void, 0, 0)
        auto received = recv(0, null_addr as *void, 0, 0)
        auto sent_to = sendto(0, null_addr as *void, 0, 0, null_addr as *void, 0)
        auto received_from = recvfrom(0, null_addr as *void, 0, 0, null_addr as *void, null_addr as *i32)
        return sent + received + sent_to + received_from
    }
}
EOF

TOKA_BUILD_DIR="$WINDOWS_DIR" TOKA_USE_LIB_CACHE=1 \
    "$TOKAC" --target x86_64-pc-windows-msvc --emit-llvm \
    "$WINDOWS_OBJECT" "$WORK_DIR/main.tk" -o "$WINDOWS_DIR/fs-io.ll"

TOKA_BUILD_DIR="$WINDOWS_DIR" TOKA_USE_LIB_CACHE=1 \
    "$TOKAC" --target x86_64-pc-windows-msvc --emit-llvm \
    "$WINDOWS_OBJECT" "$WINDOWS_DIR/winsock.tk" -o "$WINDOWS_DIR/winsock.ll"
for signature in \
    'declare i32 @send(i32, ptr, i32, i32)' \
    'declare i32 @recv(i32, ptr, i32, i32)' \
    'declare i32 @sendto(i32, ptr, i32, i32, ptr, i32)' \
    'declare i32 @recvfrom(i32, ptr, i32, i32, ptr, ptr)'; do
    if ! grep -Fq "$signature" "$WINDOWS_DIR/winsock.ll"; then
        echo "FAIL: Windows mixed cache emitted the wrong WinSock ABI: $signature" >&2
        exit 1
    fi
done

echo "Mixed core cache qualification PASSED"
