#!/usr/bin/env bash
set -euo pipefail

CXX_BIN="${CXX:-c++}"
OUT="${TMPDIR:-/tmp}/toka_access_path_test"
trap 'rm -f "$OUT"' EXIT

"$CXX_BIN" -std=c++17 -Iinclude \
  tests/semantics/access_path_test.cpp \
  src/Sema/AccessPath.cpp \
  -o "$OUT"
"$OUT"
