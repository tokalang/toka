#!/bin/bash
# tool/test_pass.sh - Parallel Test Runner

# --- Configuration ---
TOKAC="${TOKAC:-./build/bin/tokac}"
TOKA="${TOKA:-./build/bin/toka}"

if [[ "$OSTYPE" == "darwin"* ]]; then
    # Add common Homebrew LLVM paths to PATH to support macOS Intel / Apple Silicon custom builds
    for prefix in "$HOME/intel-brew/opt/llvm@20/bin" "$HOME/intel-brew/opt/llvm/bin" "/opt/homebrew/opt/llvm@20/bin" "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm@20/bin" "/usr/local/opt/llvm/bin"; do
        if [ -d "$prefix" ]; then
            export PATH="$prefix:$PATH"
        fi
    done
fi

# Autodetect compiler and tools
if command -v clang++-20 &> /dev/null; then
    CLANGXX="clang++-20"
elif command -v clang++ &> /dev/null; then
    CLANGXX="clang++"
else
    CLANGXX="clang++"
fi

if command -v llvm-config-20 &> /dev/null; then
    LLVM_CONFIG="llvm-config-20"
elif command -v llvm-config &> /dev/null; then
    LLVM_CONFIG="llvm-config"
else
    LLVM_CONFIG="llvm-config"
fi

# Pre-read LLVM flags to avoid subshell forks in workers (critical for Windows MSYS2 stability under SSH)
LLVM_CPPFLAGS=""
LLVM_LDFLAGS_LIBS=""
if command -v "$LLVM_CONFIG" &> /dev/null; then
    "$LLVM_CONFIG" --cxxflags > .tokac_cppflags_$$.txt 2>/dev/null
    if [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* || "$OSTYPE" == "win32"* ]]; then
        read -r LLVM_CPPFLAGS < .tokac_cppflags_$$.txt
    else
        LLVM_CPPFLAGS=$(cat .tokac_cppflags_$$.txt | tr '\n' ' ')
    fi
    rm -f .tokac_cppflags_$$.txt

    "$LLVM_CONFIG" --ldflags --libs > .tokac_ldflags_$$.txt 2>/dev/null
    if [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* || "$OSTYPE" == "win32"* ]]; then
        read -r LLVM_LDFLAGS_LIBS < .tokac_ldflags_$$.txt
    else
        LLVM_LDFLAGS_LIBS=$(cat .tokac_ldflags_$$.txt | tr '\n' ' ')
    fi
    rm -f .tokac_ldflags_$$.txt
fi
# LLI is no longer used. We natively compile the tests to binary.

EXTRA_LIBS=""
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    EXTRA_LIBS="-lws2_32"
fi

# Compile runtime objects dynamically for the local architecture
SYSROOT_FLAGS=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    SYSROOT_FLAGS="-isysroot $(xcrun --show-sdk-path)"
    if [ -f "$TOKAC" ]; then
        FILE_INFO=$(file "$TOKAC" 2>/dev/null)
        if [[ "$FILE_INFO" == *"x86_64"* ]]; then
            SYSROOT_FLAGS="$SYSROOT_FLAGS -arch x86_64"
        elif [[ "$FILE_INFO" == *"arm64"* ]]; then
            SYSROOT_FLAGS="$SYSROOT_FLAGS -arch arm64"
        fi
    fi
fi

if command -v clang-20 &> /dev/null; then
    CLANG="clang-20"
elif command -v clang &> /dev/null; then
    CLANG="clang"
else
    CLANG="clang"
fi

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'
GRAY='\033[0;90m'

run_without_test_cache() (
    unset TOKA_BUILD_DIR
    unset TOKA_CACHED_LIB_OBJECTS_FILE
    unset TOKA_CACHED_LIB_OBJECTS_MAP
    USE_CACHED_LIB=0 "$@"
)

# --- Worker Logic ---
run_worker() {
    test_path="$1"
    [ -e "$test_path" ] || exit 0
    
    file_name="${test_path##*/}"
    safe_target="${test_path//\//_}"
    out_dir="/tmp/tokac_tests"
    mkdir -p "$out_dir"
    exe_file="${out_dir}/${safe_target}.exe"
    log_file="${out_dir}/${safe_target}.log"
    
    # Capture output in a buffer to ensure atomic printing
    OUTPUT=""
    CACHED_LIB_OBJECTS=()
    if [ "$USE_CACHED_LIB" = "1" ] && [ -n "$TOKA_CACHED_LIB_OBJECTS_MAP" ] && [ -f "$TOKA_CACHED_LIB_OBJECTS_MAP" ]; then
        test_key="$test_path"
        case "$test_key" in
            "$PWD"/*) test_key="${test_key#$PWD/}" ;;
        esac
        test_key="${test_key#./}"
        while IFS=$'\t' read -r key objects; do
            if [ "$key" = "$test_key" ]; then
                if [ -n "$objects" ]; then
                    IFS=$'\t' read -r -a CACHED_LIB_OBJECTS <<< "$objects"
                fi
                break
            fi
        done < "$TOKA_CACHED_LIB_OBJECTS_MAP"
    elif [ "$USE_CACHED_LIB" = "1" ] && [ -n "$TOKA_CACHED_LIB_OBJECTS_FILE" ] && [ -f "$TOKA_CACHED_LIB_OBJECTS_FILE" ]; then
        while IFS= read -r obj; do
            if [ -n "$obj" ]; then
                CACHED_LIB_OBJECTS+=("$obj")
            fi
        done < "$TOKA_CACHED_LIB_OBJECTS_FILE"
    fi
    
    append() {
        OUTPUT="${OUTPUT}$1\n"
    }
    
    # Step 1: Compile Native
    if [[ "$file_name" == *llvm_shim_test.tk ]] || [[ "$file_name" == *llvm_backend_instructions.tk ]]; then
        tmp_obj="${exe_file}.o"
        if ! run_without_test_cache "$TOKAC" --emit-obj "$test_path" -o "$tmp_obj" > /dev/null 2> "$log_file"; then
            append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
            append "    ${RED}$test_path:1: error: Compilation failed${NC}"
            LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
            append "$LOGS"
            echo -ne "$OUTPUT"
            rm -f "$log_file" "$exe_file" "$tmp_obj"
            exit 1
        fi
        if ! "$CLANGXX" $SYSROOT_FLAGS "$tmp_obj" lib/sys/llvm_shim.o lib/sys/toka_rt.o $LLVM_LDFLAGS_LIBS $EXTRA_LIBS -o "$exe_file" >> "$log_file" 2>&1; then
            append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
            append "    ${RED}$test_path:1: error: Linking failed${NC}"
            LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
            append "$LOGS"
            echo -ne "$OUTPUT"
            rm -f "$log_file" "$exe_file" "$tmp_obj"
            exit 1
        fi
        rm -f "$tmp_obj"
    elif [[ "$file_name" == *odr_main.tk ]]; then
        lib_obj="${out_dir}/tests_pass_odr_test_lib.o"
        helper_obj="${out_dir}/tests_pass_odr_helper.o"
        
        lib_path=$(find tests/pass -name "*odr_test_lib.tk_lib" | head -n 1)
        helper_path=$(find tests/pass -name "*odr_helper.tk_lib" | head -n 1)
        
        if [ -z "$lib_path" ]; then lib_path="tests/pass/odr_test_lib.tk_lib"; fi
        if [ -z "$helper_path" ]; then helper_path="tests/pass/odr_helper.tk_lib"; fi

        # Compile lib
        if ! run_without_test_cache "$TOKAC" -c "$lib_path" -o "$lib_obj" > /dev/null 2> "$log_file"; then
            append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
            append "    ${RED}$test_path:1: error: Compiling odr_test_lib failed${NC}"
            LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
            append "$LOGS"
            echo -ne "$OUTPUT"
            exit 1
        fi
        # Compile helper
        if ! run_without_test_cache "$TOKAC" -c "$helper_path" -o "$helper_obj" > /dev/null 2> "$log_file"; then
            append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
            append "    ${RED}$test_path:1: error: Compiling odr_helper failed${NC}"
            LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
            append "$LOGS"
            echo -ne "$OUTPUT"
            exit 1
        fi
        # Compile and link main with lib and helper
        if ! run_without_test_cache "$TOKAC" "$test_path" "$lib_obj" "$helper_obj" -o "$exe_file" > /dev/null 2> "$log_file"; then
            append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
            append "    ${RED}$test_path:1: error: Compilation failed${NC}"
            LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
            append "$LOGS"
            echo -ne "$OUTPUT"
            rm -f "$log_file" "$exe_file" "$lib_obj" "$helper_obj"
            exit 1
        fi
        rm -f "$lib_obj" "$helper_obj"
    else
        if [ "$USE_TOKA" = "1" ]; then
            "$TOKA" run "$test_path" > "$log_file" 2>&1
            exit_code=$?
            run_skipped=1
        else
            if ! "$TOKAC" "$test_path" "${CACHED_LIB_OBJECTS[@]}" -o "$exe_file" > /dev/null 2> "$log_file"; then
                append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
                append "    ${RED}$test_path:1: error: Compilation failed${NC}"
                # Tail logs
                LOGS=$(tail -n 5 "$log_file" | sed 's/^/    | /')
                append "$LOGS"
                echo -ne "$OUTPUT"
                rm -f "$log_file" "$exe_file"
                exit 1
            fi
        fi
    fi

    # Step 2: Run Native
    if [ "$run_skipped" != "1" ]; then
        {
            "$exe_file" >> "$log_file" 2>&1 &
            pid=$!
            ( sleep 30; kill -9 $pid 2>/dev/null; echo "TIMEOUT" >> "$log_file" ) >/dev/null 2>&1 < /dev/null &
            killer=$!
            wait $pid
            sub_exit=$?
            kill -9 $killer 2>/dev/null
            exit $sub_exit
        } 2>&1 | grep -v "Abort trap"
        exit_code=${PIPESTATUS[0]}
    fi

    # Step 3: Extract & Verify
    errors=()
    
    # Extract panic info
    grep "runtime error: Panic with" "$log_file" | head -n 1 > "${log_file}.panic"
    read -r panic_log_line < "${log_file}.panic"
    rm -f "${log_file}.panic"

    if [ -n "$panic_log_line" ]; then
        echo "$panic_log_line" | grep -oE ":[0-9]+ runtime" | grep -oE "[0-9]+" > "${log_file}.line"
        read -r actual_line < "${log_file}.line"
        rm -f "${log_file}.line"

        echo "$panic_log_line" | sed -n 's/.*Panic with "\(.*\)" \*\*\*/\1/p' > "${log_file}.msg"
        read -r actual_msg < "${log_file}.msg"
        rm -f "${log_file}.msg"
    else
        actual_line=""
        actual_msg=""
    fi
    
    grep -n "// EXPECT_PANIC" "$test_path" | cut -d: -f1 | tr '\n' ' ' > "${log_file}.explines"
    read -r all_expected_lines < "${log_file}.explines"
    rm -f "${log_file}.explines"

    if [ $exit_code -eq 0 ]; then
        for exp_line in $all_expected_lines; do
            errors+=("${RED}$test_path:$exp_line: error: Expected panic did not trigger.${NC}")
        done
    else
        if [ -n "$actual_line" ]; then
             # Check execution flow
             for exp_line in $all_expected_lines; do
                if [ "$exp_line" -lt "$actual_line" ]; then
                    errors+=("${RED}$test_path:$exp_line: error: Expected panic missed (passed check).${NC}")
                fi
             done
             
             # Check crash location
             sed "${actual_line}q;d" "$test_path" > "${log_file}.source"
             read -r crashed_source < "${log_file}.source"
             rm -f "${log_file}.source"

             if [[ "$crashed_source" == *"assert"* ]]; then
                 errors+=("${RED}$test_path:$actual_line: error: Assertion failed: \"$actual_msg\"${NC}")
             else
                 echo "$crashed_source" | grep "// EXPECT_PANIC" > "${log_file}.isexp"
                 read -r is_exp < "${log_file}.isexp"
                 rm -f "${log_file}.isexp"

                 if [ -z "$is_exp" ]; then
                     errors+=("${RED}$test_path:$actual_line: error: Unexpected panic: \"$actual_msg\"${NC}")
                 fi
             fi
        else
             errors+=("${RED}$test_path:1: error: Runtime crash ($exit_code) without panic info.${NC}")
        fi
    fi

    # Report
    if [ ${#errors[@]} -eq 0 ]; then
        if [ $exit_code -ne 0 ]; then
             append "$(printf "[${GREEN}PASS${NC} with Expected ${YELLOW}Panic${NC}] %-35s" "$file_name")"
             # Context for crash
             if [ -f "$log_file" ]; then
                panic_line=$(grep "runtime error: Panic with" "$log_file" | head -n 1)
                append "${GRAY}    --------------------------------------------------${NC}"
                if [ -n "$panic_line" ]; then
                     append "    | Exit Code: $exit_code"
                     append "    | $panic_line"
                else
                     append "    | Exit Code: $exit_code (No panic info found)"
                     append "    | Last logs:"
                     append "$(tail -n 3 "$log_file" | sed 's/^/    | /')"
                fi
                append "${GRAY}    --------------------------------------------------${NC}"
             fi
        else
            append "$(printf "[${GREEN}PASS${NC}] %-35s" "$file_name")"
        fi
        rm -f "$exe_file" "$log_file"
        echo -ne "$OUTPUT"
        exit 0
    else
        append "$(printf "[${RED}FAIL${NC}] %-35s" "$file_name")"
        for err in "${errors[@]}"; do
            append "    $err"
        done
        
        # Context dump
        if [ -f "$log_file" ]; then
            append "${GRAY}    --------------------------------------------------${NC}"
            append "    | Exit Code: $exit_code"
            append "    | Last logs:"
            append "$(tail -n 3 "$log_file" | sed 's/^/    | /')"
            append "${GRAY}    --------------------------------------------------${NC}"
        fi
        
        echo -ne "$OUTPUT"
        rm -f "$exe_file" # Keep log? No, simplify cleanup.
        exit 1
    fi
}

# --- Entry Point ---

# Parse options
USE_TOKA="${USE_TOKA:-0}"
USE_CACHED_LIB="${USE_CACHED_LIB:-0}"
NEW_ARGS=()
for arg in "$@"; do
    if [ "$arg" == "--toka" ]; then
        export USE_TOKA=1
    elif [ "$arg" == "--cached-lib" ]; then
        export USE_CACHED_LIB=1
    else
        NEW_ARGS+=("$arg")
    fi
done
set -- "${NEW_ARGS[@]}"

if [ "$1" == "--worker" ]; then
    run_worker "$2"
fi

# Clean up known hybrid-test scratch files before enumerating tests.
rm -f tests/pass/temp_main.tk tests/pass/temp_main.tki tests/pass/temp_shim.c target/debug/temp_main target/debug/temp_main.exe

# Clean up all untracked/ignored .tki files in the workspace to avoid polluting relative imports
if command -v git &> /dev/null && [ -d .git ]; then
    if [ "$USE_CACHED_LIB" = "1" ]; then
        CACHE_PREFIX="${TOKA_TEST_CACHE_DIR:-tmp/toka_test_cache}"
        git status --ignored --porcelain | grep -E "^(!!|\?\?) " | grep -E "\.tki$" | cut -c4- | grep -v "^${CACHE_PREFIX%/}/" | xargs rm -f 2>/dev/null || true
    else
        git status --ignored --porcelain | grep -E "^(!!|\?\?) " | grep -E "\.tki$" | cut -c4- | xargs rm -f 2>/dev/null || true
    fi
fi

# Determine core count dynamically for CI and local optimizations
if [ -n "$CORES" ]; then
    # Respect pre-defined CORES environment variable
    :
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    # Default to 1 on Windows to prevent unstable MSYS2 xargs parallel fork crashes
    CORES=1
elif command -v nproc &> /dev/null; then
    CORES=$(nproc)
else
    CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
fi

if ! [[ "$CORES" =~ ^[0-9]+$ ]] || [ "$CORES" -lt 1 ]; then
    CORES=1
fi

# Orchestrator
if [ -d build ] && [ -f build/CMakeCache.txt ] && command -v cmake &> /dev/null; then
    cmake --build build --parallel $CORES > /dev/null || { echo "Compiler Build Failed"; exit 1; }
fi

# Compile runtime objects dynamically for the local architecture
rm -f lib/sys/toka_rt.o
"$CLANG" $SYSROOT_FLAGS -c lib/sys/toka_rt.c -o lib/sys/toka_rt.o || { echo "Failed to compile toka_rt.c"; exit 1; }

rm -f lib/sys/llvm_shim.o
SHIM_CXXFLAGS="$LLVM_CPPFLAGS"
if [[ "$OSTYPE" == "msys"* || "$OSTYPE" == "cygwin"* || "$OSTYPE" == "win32"* ]]; then
    SHIM_CXXFLAGS="$SHIM_CXXFLAGS -DLLVM_SHARED_LIBS"
fi
"$CLANGXX" $SYSROOT_FLAGS -O3 -c lib/sys/llvm_shim.cpp -o lib/sys/llvm_shim.o $SHIM_CXXFLAGS || { echo "Failed to compile llvm_shim.cpp"; exit 1; }

if [ "$USE_CACHED_LIB" = "1" ]; then
    CACHE_DIR="${TOKA_TEST_CACHE_DIR:-tmp/toka_test_cache}"
    python3 tools/scripts/prepare_test_lib_cache.py --tokac "$TOKAC" --cache-dir "$CACHE_DIR"
    export TOKA_BUILD_DIR="$CACHE_DIR"
    export TOKA_CACHED_LIB_OBJECTS_FILE="$CACHE_DIR/objects.list"
    export TOKA_CACHED_LIB_OBJECTS_MAP="$CACHE_DIR/test_objects.map"
fi

echo "Starting Toka 'PASS' Test Suite (Parallel: $CORES)..."
echo "---------------------------------"

RESULTS_FILE="/tmp/tokac_res_${RANDOM}_$$.txt"

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    SCRIPT_PATH="tools/scripts/test_pass.sh"
else
    SCRIPT_PATH=$(realpath "$0" 2>/dev/null || readlink -f "$0" 2>/dev/null || echo "./$0")
fi

# Run parallel tests based on available cores
find tests/pass -name "*.tk" | sort | tr '\n' '\0' | xargs -0 -P $CORES -n 1 "$SCRIPT_PATH" --worker | tee "$RESULTS_FILE"

# Stats
# Strip ANSI codes for accurate counting
# sed -r is GNU, sed -E is BSD/Mac. Use perl or simplified grep.
sed 's/\x1b\[[0-9;]*m//g' "$RESULTS_FILE" | grep -c "\[PASS" > "/tmp/tokac_pass_$$.txt"
read pass_count < "/tmp/tokac_pass_$$.txt"
rm -f "/tmp/tokac_pass_$$.txt"

sed 's/\x1b\[[0-9;]*m//g' "$RESULTS_FILE" | grep -c "\[FAIL" > "/tmp/tokac_fail_$$.txt"
read fail_count < "/tmp/tokac_fail_$$.txt"
rm -f "/tmp/tokac_fail_$$.txt"

echo "---------------------------------"

if [ $fail_count -gt 0 ]; then
    echo -e "${RED}!!! Concentrated Failure Summary !!!${NC}"
    echo "---------------------------------"
    awk '/\[.*FAIL.*\]/ {print; print_block=1; next} /^[[:space:]]/ && print_block {print; next} {print_block=0}' "$RESULTS_FILE"
    echo "---------------------------------"
fi

rm -f "$RESULTS_FILE"

echo "Summary:"
echo -e "  Passed: ${GREEN}$pass_count${NC}"
echo -e "  Failed: ${RED}$fail_count${NC}"

[ $fail_count -eq 0 ] || exit 1

echo ""
echo "Running ODR Path & Interface Behavior lock tests..."
if ! run_without_test_cache bash tools/scripts/test_path_behavior.sh; then
    echo -e "${RED}Path behavior lock tests failed!${NC}"
    exit 1
fi

echo ""
echo "Running TKI Cache Validation tests..."
if ! run_without_test_cache bash tools/scripts/test_tki_cache_validation.sh; then
    echo -e "${RED}TKI Cache Validation tests failed!${NC}"
    exit 1
fi

echo ""
echo "Running Incremental Build tests..."
if ! run_without_test_cache bash tools/scripts/test_incremental_build.sh; then
    echo -e "${RED}Incremental Build tests failed!${NC}"
    exit 1
fi
