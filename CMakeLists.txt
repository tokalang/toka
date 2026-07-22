cmake_minimum_required(VERSION 3.13)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version" FORCE)
    # Suppress warnings from ld when linking LLVM binaries built for newer macOS versions
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-w")
endif()

project(TokaCompiler VERSION 0.9.8 LANGUAGES C CXX)
include(GNUInstallDirs)
set(TOKA_RELEASE_SUFFIX "-09-RC")
set(TOKA_VERSION_STRING "${TokaCompiler_VERSION}${TOKA_RELEASE_SUFFIX}")

# Version Control Info
find_package(Git)
if(GIT_FOUND)
    # Get the commit count for build number
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE TOKA_BUILD_NUMBER
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    # Get the latest commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE TOKA_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(TOKA_BUILD_NUMBER 0)
    set(TOKA_COMMIT_HASH "unknown")
endif()

# Determine Target Triple
if(LLVM_DEFAULT_TARGET_TRIPLE)
    set(TOKA_TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE}")
else()
    set(TOKA_TARGET_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")
endif()

# Configure the Version header
configure_file(
    ${CMAKE_SOURCE_DIR}/include/toka/Version.h.in
    ${CMAKE_BINARY_DIR}/include/toka/Version.h
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output executables to build/bin instead of build/src/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Setup Homebrew path detection for macOS (Apple Silicon & Intel Mac)
if(APPLE)
    # Dynamically query system Homebrew prefix
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    # Inject dynamically if found
    if(HOMEBREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
    endif()
    # Statically append standard fallback locations for safety
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

# Find LLVM 20 intelligently
if(NOT LLVM_DIR)
    if(APPLE)
        # Check dynamic or standard Homebrew locations for LLVM 20
        if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/llvm@20/lib/cmake/llvm")
            set(LLVM_DIR "${HOMEBREW_PREFIX}/opt/llvm@20/lib/cmake/llvm")
        elseif(EXISTS "/opt/homebrew/opt/llvm@20/lib/cmake/llvm")
            set(LLVM_DIR "/opt/homebrew/opt/llvm@20/lib/cmake/llvm")
        elseif(EXISTS "/usr/local/opt/llvm@20/lib/cmake/llvm")
            set(LLVM_DIR "/usr/local/opt/llvm@20/lib/cmake/llvm")
        endif()
    elseif(WIN32)
        if(EXISTS "C:/msys64/mingw64/lib/cmake/llvm")
            set(LLVM_DIR "C:/msys64/mingw64/lib/cmake/llvm")
        elseif(EXISTS "C:/Program Files/LLVM/lib/cmake/llvm")
            set(LLVM_DIR "C:/Program Files/LLVM/lib/cmake/llvm")
        endif()
    else()
        # Fallback to standard Ubuntu APT locations since this was the original explicitly set path
        set(LLVM_DIR "/usr/lib/llvm-20/lib/cmake/llvm")
    endif()
endif()
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Find LLD 20 intelligently
if(NOT LLD_DIR)
    if(APPLE)
        if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/lld@20/lib/cmake/lld")
            set(LLD_DIR "${HOMEBREW_PREFIX}/opt/lld@20/lib/cmake/lld")
        elseif(EXISTS "/opt/homebrew/opt/lld@20/lib/cmake/lld")
            set(LLD_DIR "/opt/homebrew/opt/lld@20/lib/cmake/lld")
        elseif(EXISTS "/usr/local/opt/lld@20/lib/cmake/lld")
            set(LLD_DIR "/usr/local/opt/lld@20/lib/cmake/lld")
        endif()
    elseif(WIN32)
        if(EXISTS "C:/msys64/mingw64/lib/cmake/lld")
            set(LLD_DIR "C:/msys64/mingw64/lib/cmake/lld")
        endif()
    endif()
endif()

if(LLD_DIR)
    find_package(LLD REQUIRED CONFIG)
    message(STATUS "Found LLD ${LLD_PACKAGE_VERSION}")
    include_directories(${LLD_INCLUDE_DIRS})
endif()

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFS NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFS})

include_directories(${CMAKE_BINARY_DIR}/include ${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)

# Build the native runtime and the user-facing tools into one SDK layout.  The
# compiler must be built first because the remaining tools are written in Toka.
set(TOKA_RUNTIME_OBJECT
    "${CMAKE_BINARY_DIR}/lib/sys/toka_rt${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT "${TOKA_RUNTIME_OBJECT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/sys"
    COMMAND ${CMAKE_C_COMPILER} -c "${CMAKE_SOURCE_DIR}/lib/sys/toka_rt.c"
            -o "${TOKA_RUNTIME_OBJECT}"
    DEPENDS "${CMAKE_SOURCE_DIR}/lib/sys/toka_rt.c"
    VERBATIM
)

file(GLOB_RECURSE TOKA_TOOL_AND_LIBRARY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/toka/*.tk"
    "${CMAKE_SOURCE_DIR}/tools/tokafmt/*.tk"
    "${CMAKE_SOURCE_DIR}/lib/*.tk")

set(TOKA_MANAGER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/toka")
set(TOKA_FORMATTER "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tokafmt")

add_custom_command(
    OUTPUT "${TOKA_MANAGER}"
    COMMAND $<TARGET_FILE:tokac> -I "${CMAKE_SOURCE_DIR}/lib"
            -I "${CMAKE_SOURCE_DIR}/tools/toka"
            "${CMAKE_SOURCE_DIR}/tools/toka/src/main.tk"
            "${TOKA_RUNTIME_OBJECT}" -o "${TOKA_MANAGER}" -O3
    DEPENDS tokac "${TOKA_RUNTIME_OBJECT}" ${TOKA_TOOL_AND_LIBRARY_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${TOKA_FORMATTER}"
    COMMAND $<TARGET_FILE:tokac> -I "${CMAKE_SOURCE_DIR}/lib"
            -I "${CMAKE_SOURCE_DIR}/tools/tokafmt"
            "${CMAKE_SOURCE_DIR}/tools/tokafmt/src/main.tk"
            "${TOKA_RUNTIME_OBJECT}" -o "${TOKA_FORMATTER}" -O3
    DEPENDS tokac "${TOKA_RUNTIME_OBJECT}" ${TOKA_TOOL_AND_LIBRARY_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(toka-tools ALL
    DEPENDS "${TOKA_MANAGER}" "${TOKA_FORMATTER}")
add_dependencies(toka-tools tokalsp)

install(TARGETS tokac tokalsp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS
    "${TOKA_MANAGER}"
    "${TOKA_FORMATTER}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toka/lib"
    PATTERN "*.o" EXCLUDE
    PATTERN "*.obj" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "__pycache__" EXCLUDE)
install(FILES "${TOKA_RUNTIME_OBJECT}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toka/lib/sys")
install(PROGRAMS "${CMAKE_SOURCE_DIR}/tools/scripts/toka_build.py"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toka/lib/toolchain")
install(FILES README.md README_zh.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
