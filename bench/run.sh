#!/bin/bash
set -e

# 检查是否提供了参数
if [ -z "$1" ]; then
    echo "用法: $0 <文件名.tk 或 文件名.cpp>"
    exit 1
fi

# 获取文件路径和不带扩展名的基本名称
INPUT_FILE="$1"
# 移除扩展名（无论输入是 .tk 还是 .cpp）
BASE_PATH="${INPUT_FILE%.*}"
# 获取纯文件名（不含路径）用于显示
FILE_NAME=$(basename "$BASE_PATH")

TK_FILE="${BASE_PATH}.tk"
LL_FILE="${BASE_PATH}.ll"
TOKA_BIN="${BASE_PATH}_toka_native.bin"

# Detect whether it is .cpp or .c
if [[ -f "${BASE_PATH}.cpp" ]]; then
    C_FILE="${BASE_PATH}.cpp"
    C_BIN="${BASE_PATH}_cpp.bin"
    COMPILER="/usr/bin/clang++-20"
else
    C_FILE="${BASE_PATH}.c"
    C_BIN="${BASE_PATH}_c.bin"
    COMPILER="/usr/bin/clang-20"
fi

# 检查两个必要文件是否存在
if [[ ! -f "$TK_FILE" ]]; then
    echo "错误: 找不到 Toka 文件 '$TK_FILE'"
    exit 1
fi

if [[ ! -f "$C_FILE" ]]; then
    echo "错误: 找不到 C/C++ 文件 '$C_FILE'"
    exit 1
fi

echo "=== 正在对比测试: $FILE_NAME ==="

# --- Toka 编译流程 ---
echo "--- Step 1: 编译 Toka ($FILE_NAME.tk) 到 LLVM IR ---"
build/bin/tokac "$TK_FILE" > "$LL_FILE"

echo "--- Step 2: 编译 LLVM IR 到 Native 二进制 ---"
if [ "$(uname)" == "Darwin" ]; then
    SDK_PATH=$(xcrun --show-sdk-path)
    /usr/bin/clang lib/sys/toka_rt.o -x ir "$LL_FILE" -O3 -o "$TOKA_BIN" -isysroot "$SDK_PATH" # -mllvm # -opaque-pointers
else
    /usr/bin/clang-20 lib/sys/toka_rt.o -x ir "$LL_FILE" -O3 -o "$TOKA_BIN"
fi

echo "=== 运行 Toka (Native) ==="
time "./$TOKA_BIN"
echo ""

# --- C/C++ 编译流程 ---
echo "--- Step 3: 编译 C/C++ ($C_FILE) ---"
"$COMPILER" -O3 -o "$C_BIN" "$C_FILE"

echo "=== 运行 C/C++ (Native -O3) ==="
time "./$C_BIN"

echo ""
echo "=== 测试完成 ==="