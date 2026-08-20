#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Detect Clang compiler
if [ -x "/opt/homebrew/opt/llvm/bin/clang" ]; then
    CLANG_BIN="/opt/homebrew/opt/llvm/bin/clang"
elif command -v clang-20 &> /dev/null; then
    CLANG_BIN="clang-20"
elif command -v clang-19 &> /dev/null; then
    CLANG_BIN="clang-19"
elif command -v clang-18 &> /dev/null; then
    CLANG_BIN="clang-18"
else
    CLANG_BIN="clang"
fi

# Detect platform-specific flags
OS_NAME="$(uname -s)"
OPENSSL_INC=""
OPENSSL_LIB=""
EXTRA_LIBS="-lm"

if [ "$OS_NAME" = "Darwin" ]; then
    if [ -d "/opt/homebrew/include" ]; then
        OPENSSL_INC="-I/opt/homebrew/include"
        OPENSSL_LIB="-L/opt/homebrew/lib"
    elif [ -d "/usr/local/include" ]; then
        OPENSSL_INC="-I/usr/local/include"
        OPENSSL_LIB="-L/usr/local/lib"
    fi
    EXTRA_LIBS="-lpthread -lssl -lcrypto"
elif [ "$OS_NAME" = "Linux" ]; then
    EXTRA_LIBS="-lpthread -ldl -lssl -lcrypto"
fi

echo "=== [Sanitizer Runner] Testing Positional I/O under ASan / TSan ($OS_NAME, $CLANG_BIN) ==="

mkdir -p "$ROOT_DIR/build/sanitizer_test"
cd "$ROOT_DIR"

# 1. Compile ASan runtime (with -DTOKA_TESTING=1 for failpoint verification)
echo "-> Building ASan runtime object..."
"$CLANG_BIN" -DTOKA_HAS_OPENSSL=1 -DTOKA_TESTING=1 $OPENSSL_INC -fsanitize=address -g -c lib/sys/toka_rt.c -o "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o"

# 2. Compile tests using tokac
echo "-> Compiling conformance test object files..."
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_buffer_contract_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/buffer_contract.o"
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_owner_return_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/owner_return.o"
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_concurrency_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/concurrency.o"
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_write_all_partial_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/write_all_partial.o"
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_shared_view_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/shared_view.o"
"$ROOT_DIR/build/bin/tokac" -I lib tests/conformance/io/datafile_truncate_test.tk -c -o "$ROOT_DIR/build/sanitizer_test/truncate.o"

# 3. Link with AddressSanitizer and execute
echo "-> Linking and running with AddressSanitizer (ASan)..."
"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/buffer_contract.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_buffer_asan"
"$ROOT_DIR/build/sanitizer_test/test_buffer_asan"

"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/owner_return.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_owner_asan"
"$ROOT_DIR/build/sanitizer_test/test_owner_asan"

"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/concurrency.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_concurrency_asan"
"$ROOT_DIR/build/sanitizer_test/test_concurrency_asan"

"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/write_all_partial.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_write_all_partial_asan"
"$ROOT_DIR/build/sanitizer_test/test_write_all_partial_asan"

"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/shared_view.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_shared_view_asan"
"$ROOT_DIR/build/sanitizer_test/test_shared_view_asan"

"$CLANG_BIN" -fsanitize=address $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/truncate.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_asan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_truncate_asan"
"$ROOT_DIR/build/sanitizer_test/test_truncate_asan"

echo "-> All ASan checks PASSED!"

# 4. Compile TSan runtime, link and execute concurrency test
echo "-> Building TSan runtime and running ThreadSanitizer..."
"$CLANG_BIN" -DTOKA_HAS_OPENSSL=1 -DTOKA_TESTING=1 $OPENSSL_INC -fsanitize=thread -g -c lib/sys/toka_rt.c -o "$ROOT_DIR/build/sanitizer_test/toka_rt_tsan.o"

"$CLANG_BIN" -fsanitize=thread $OPENSSL_LIB "$ROOT_DIR/build/sanitizer_test/concurrency.o" "$ROOT_DIR/build/sanitizer_test/toka_rt_tsan.o" $EXTRA_LIBS -o "$ROOT_DIR/build/sanitizer_test/test_concurrency_tsan"
"$ROOT_DIR/build/sanitizer_test/test_concurrency_tsan"

echo "-> All TSan checks PASSED!"
echo "=== [Sanitizer Runner] ALL ASAN + TSAN CHECKS PASSED CLEANLY! ==="
