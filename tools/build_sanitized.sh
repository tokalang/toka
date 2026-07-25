#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-help}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="${ROOT_DIR}/tmp"

mkdir -p "${TMP_DIR}"

case "${MODE}" in
  runtime-asan)
    echo "=== Compiling Toka Runtime (toka_rt.c) with AddressSanitizer (ASan) ==="
    OPENSSL_CFLAGS=""
    if [ -d "/opt/homebrew/opt/openssl@3/include" ]; then
        OPENSSL_CFLAGS="-DTOKA_HAS_OPENSSL=1 -I/opt/homebrew/opt/openssl@3/include"
    fi
    clang -fsanitize=address -O1 -g ${OPENSSL_CFLAGS} -c "${ROOT_DIR}/lib/sys/toka_rt.c" \
      -o "${TMP_DIR}/toka_rt_asan.o"
    echo "[Sanitizer] Runtime ASan object compiled cleanly at tmp/toka_rt_asan.o."
    ;;

  runtime-tsan)
    echo "=== Compiling Toka Runtime (toka_rt.c) with ThreadSanitizer (TSan) ==="
    OPENSSL_CFLAGS=""
    if [ -d "/opt/homebrew/opt/openssl@3/include" ]; then
        OPENSSL_CFLAGS="-DTOKA_HAS_OPENSSL=1 -I/opt/homebrew/opt/openssl@3/include"
    fi
    clang -fsanitize=thread -O1 -g ${OPENSSL_CFLAGS} -c "${ROOT_DIR}/lib/sys/toka_rt.c" \
      -o "${TMP_DIR}/toka_rt_tsan.o"
    echo "[Sanitizer] Runtime TSan object compiled cleanly at tmp/toka_rt_tsan.o."
    ;;

  compiler-asan)
    echo "=== Building Toka Compiler (tokac) with AddressSanitizer ==="
    mkdir -p "${ROOT_DIR}/build_asan"
    cd "${ROOT_DIR}/build_asan"
    cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address" ..
    make -j4 tokac
    echo "[Sanitizer] Compiler ASan Build Completed at build_asan/bin/tokac."
    ;;

  *)
    echo "Usage: $0 {runtime-asan|runtime-tsan|compiler-asan}"
    exit 1
    ;;
esac
