#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOKAC_BIN="${ROOT_DIR}/build/bin/tokac"
TMP_DIR="${ROOT_DIR}/tmp"

mkdir -p "${TMP_DIR}"

echo "=== Verifying Plaintext Network & Stdlib I/O without OpenSSL Backend ==="

# Step 1: Compile C runtime without OpenSSL (-UTOKA_HAS_OPENSSL)
clang -UTOKA_HAS_OPENSSL -O1 -g -c "${ROOT_DIR}/lib/sys/toka_rt.c" \
  -o "${TMP_DIR}/toka_rt_no_openssl.o"
echo "[No-OpenSSL Step 1/3] Plaintext runtime compiled cleanly at tmp/toka_rt_no_openssl.o."

# Step 2: HTTP Client/Server compile & link smoke test without OpenSSL
"${TOKAC_BIN}" "${ROOT_DIR}/tests/pass/g12_stdx_http_client_server_test.tk" \
  "${TMP_DIR}/toka_rt_no_openssl.o" -o "${TMP_DIR}/no_openssl_http.exe"
echo "[No-OpenSSL Step 2/3] Plaintext HTTP client/server compiled & linked cleanly without OpenSSL!"

# Step 3: Plaintext JSON Serde non-network execution test
"${TOKAC_BIN}" "${ROOT_DIR}/demos/vertical_slices/04_json_serde_slice.tk" \
  "${TMP_DIR}/toka_rt_no_openssl.o" -o "${TMP_DIR}/no_openssl_serde.exe"

"${TMP_DIR}/no_openssl_serde.exe"
echo "[No-OpenSSL Step 3/3] Plaintext non-network JSON Serde executed cleanly without OpenSSL!"

# Cleanup temporary artifacts
rm -f "${TMP_DIR}/no_openssl_http.exe" "${TMP_DIR}/no_openssl_serde.exe" "${TMP_DIR}/toka_rt_no_openssl.o"
