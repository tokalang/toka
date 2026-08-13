#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
output_dir="${1:-$repo_root/build/browser}"

if ! command -v em++ >/dev/null 2>&1; then
    echo "error: em++ is required to build the browser compiler" >&2
    exit 1
fi

mkdir -p "$output_dir"

em++ -O3 -std=c++17 \
    -I"$repo_root/include" \
    "$repo_root/src/wasm_api.cpp" \
    "$repo_root/src/Lexer/Lexer.cpp" \
    "$repo_root/src/Parser/Parser.cpp" \
    "$repo_root/src/Parser/Parser_Decl.cpp" \
    "$repo_root/src/Parser/Parser_Expr.cpp" \
    "$repo_root/src/Parser/Parser_Stmt.cpp" \
    "$repo_root/src/Sema/Sema.cpp" \
    "$repo_root/src/Sema/Sema_Expr.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Member.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Unary.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Binary.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Call.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Init.cpp" \
    "$repo_root/src/Sema/Sema_Expr_Closure.cpp" \
    "$repo_root/src/Sema/Sema_Stmt.cpp" \
    "$repo_root/src/Sema/Sema_Template.cpp" \
    "$repo_root/src/Sema/Sema_Type.cpp" \
    "$repo_root/src/Sema/AccessPath.cpp" \
    "$repo_root/src/Sema/Sema_AccessPath.cpp" \
    "$repo_root/src/Sema/CanonicalDeclarationWitness.cpp" \
    "$repo_root/src/Sema/SemanticEvidence.cpp" \
    "$repo_root/src/Sema/PAL_Checker.cpp" \
    "$repo_root/src/DiagnosticEngine.cpp" \
    "$repo_root/src/Basic/SourceManager.cpp" \
    "$repo_root/src/Basic/ModuleResolver.cpp" \
    "$repo_root/src/Type.cpp" \
    "$repo_root/src/AST/ASTEvaluator.cpp" \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s STACK_SIZE=1048576 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "stringToUTF8", "lengthBytesUTF8", "UTF8ToString"]' \
    -s EXPORTED_FUNCTIONS='["_check_toka_code", "_malloc", "_free"]' \
    --embed-file "$repo_root/lib@lib/" \
    -o "$output_dir/tokacheck.js"

test -s "$output_dir/tokacheck.js"
test -s "$output_dir/tokacheck.wasm"
