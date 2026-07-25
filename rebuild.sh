#!/bin/bash
set -e

# Toka Project Rebuild Script
# Automatically recompiles both the 'tokac' compiler and the 'toka' native package manager CLI

# Workaround for LLVM 20 ASan container-overflow false positives on macOS
export ASAN_OPTIONS=detect_container_overflow=0

ROOT_DIR=$(pwd)
BIN_DIR="$ROOT_DIR/build/bin"

# Attempt to find LLVM 20 clang path dynamically via brew
if command -v brew >/dev/null 2>&1; then
    LLVM_CLANG="$(brew --prefix llvm@20 2>/dev/null || brew --prefix llvm 2>/dev/null)/bin/clang"
fi

# Fallback paths
if [ -z "$LLVM_CLANG" ] || [ ! -f "$LLVM_CLANG" ]; then
    if [ -f "/opt/homebrew/opt/llvm@20/bin/clang" ]; then
        LLVM_CLANG="/opt/homebrew/opt/llvm@20/bin/clang"
    elif [ -f "/usr/local/opt/llvm@20/bin/clang" ]; then
        LLVM_CLANG="/usr/local/opt/llvm@20/bin/clang"
    else
        LLVM_CLANG="clang-20"
    fi
fi

if ! command -v "$LLVM_CLANG" >/dev/null 2>&1 && [ ! -f "$LLVM_CLANG" ]; then
    echo "Error: LLVM 20 clang not found."
    echo "Please ensure llvm@20 is installed via Homebrew (macOS) or apt (Linux)."
    exit 1
fi

# OpenSSL is an optional TLS backend.  Non-TLS programs must still be able to
# build and link when the headers/libraries are absent, so resolve the include
# path when available and otherwise compile the runtime with TLS stubs.
OPENSSL_CFLAGS=""
if command -v pkg-config >/dev/null 2>&1; then
    OPENSSL_CFLAGS="$(pkg-config --cflags openssl 2>/dev/null || true)"
fi
if [ -z "$OPENSSL_CFLAGS" ]; then
    for openssl_prefix in \
        "$(brew --prefix openssl@3 2>/dev/null || true)" \
        "/opt/homebrew/opt/openssl@3" \
        "/usr/local/opt/openssl@3" \
        "/usr/local/opt/openssl"; do
        if [ -n "$openssl_prefix" ] && [ -f "$openssl_prefix/include/openssl/ssl.h" ]; then
            OPENSSL_CFLAGS="-I$openssl_prefix/include"
            break
        fi
    done
fi
OPENSSL_RUNTIME_FLAGS=""
if [ -n "$OPENSSL_CFLAGS" ]; then
    OPENSSL_RUNTIME_FLAGS="-DTOKA_HAS_OPENSSL=1 $OPENSSL_CFLAGS"
    echo "   -> OpenSSL detected; TLS backend enabled."
else
    echo "   -> OpenSSL not detected; TLS backend disabled (non-TLS builds remain supported)."
fi

echo "====================================="
echo "0. Generating Diagnostics Definition"
echo "====================================="
python3 tools/scripts/gen_diagnostics.py

echo "====================================="
echo "1. Building Toka Compiler (tokac)"
echo "====================================="
make -C build -j8

# Ensure the newly built tokac is in the PATH so it can compile the toka wrapper
export PATH="$BIN_DIR:$PATH"

echo "   -> Compiling Toka Runtime (toka_rt.o / toka_rt.obj)..."
if [ "$(uname)" == "Darwin" ]; then
    $LLVM_CLANG $OPENSSL_RUNTIME_FLAGS -isysroot $(xcrun --show-sdk-path) -c lib/sys/toka_rt.c -o lib/sys/toka_rt.o
else
    $LLVM_CLANG $OPENSSL_RUNTIME_FLAGS -c lib/sys/toka_rt.c -o lib/sys/toka_rt.o
    cp lib/sys/toka_rt.o lib/sys/toka_rt.obj 2>/dev/null || true
fi

echo ""
echo "====================================="
echo "2. Building Toka CLI Tool (toka)"
echo "====================================="
cd tools/toka
echo "   -> Compiling and Linking tools/toka/src/main.tk with internal LLD..."
tokac -I "$ROOT_DIR/lib" -I src src/main.tk "$ROOT_DIR/lib/sys/toka_rt.o" -o toka

echo "   -> Installing toka to $BIN_DIR/toka..."
mkdir -p "$BIN_DIR"
cp toka "$BIN_DIR/toka"

# Clean up build artifacts in tools/toka
rm -f toka

# Return to root directory
cd "$ROOT_DIR"

echo ""
echo "====================================="
echo "3. Building Toka Formatter (tokafmt)"
echo "====================================="
cd tools/tokafmt
echo "   -> Compiling and Linking tools/tokafmt/src/main.tk with internal LLD..."
tokac -I "$ROOT_DIR/lib" src/main.tk "$ROOT_DIR/lib/sys/toka_rt.o" -o tokafmt

echo "   -> Installing tokafmt to $BIN_DIR/tokafmt..."
cp tokafmt "$BIN_DIR/tokafmt"

# Clean up build artifacts in tools/tokafmt
rm -f tokafmt

cd "$ROOT_DIR"

echo ""
echo "====================================="
echo "4. Building Toka Language Server (tokalsp)"
echo "====================================="
echo "   -> tokalsp is built by the CMake step above (tools/tokalsp/main.cpp)."
if [ ! -x "$BIN_DIR/tokalsp" ]; then
    make -C build tokalsp
fi

echo ""
echo "====================================="
echo "5. Building Toka Incremental Engine (forge)"
echo "====================================="
echo "   -> forge is currently a legacy Toka tool and is not part of the CMake SDK build; skipping."

echo ""
echo "✨ Rebuild Successful! 'tokac', 'toka', 'tokafmt', and 'tokalsp' are ready in build/bin."
echo "Make sure to add $BIN_DIR to your PATH if you haven't already:"
echo "    export PATH=\"$ROOT_DIR/build/bin:\$PATH\""
