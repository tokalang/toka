set(toka_frontend_sources
    Basic/SourceManager.cpp
    Basic/ModuleResolver.cpp
    Analysis/AnalysisSession.cpp
    Lexer/Lexer.cpp
    DiagnosticEngine.cpp
    AST/TKIExporter.cpp
    AST/ASTEvaluator.cpp
    Parser/Parser.cpp
    Parser/Parser_Decl.cpp
    Parser/Parser_Stmt.cpp
    Parser/Parser_Expr.cpp
    Sema/Sema.cpp
    Sema/Sema_Type.cpp
    Sema/Sema_Expr.cpp
    Sema/Sema_Expr_Member.cpp
    Sema/Sema_Expr_Unary.cpp
    Sema/Sema_Expr_Binary.cpp
    Sema/Sema_Expr_Call.cpp
    Sema/Sema_Expr_Init.cpp
    Sema/Sema_Expr_Closure.cpp
    Sema/Sema_Stmt.cpp
    Sema/Sema_Template.cpp
    Sema/AccessPath.cpp
    Sema/Sema_AccessPath.cpp
    Sema/CanonicalDeclarationWitness.cpp
    Sema/SemanticDependencyClosure.cpp
    Sema/SemanticManifestEnvelope.cpp
    Sema/SemanticManifestAttestation.cpp
    Sema/SemanticEvidence.cpp
    Sema/DirectCallObservation.cpp
    Sema/DirectCallObservationAudit.cpp
    Sema/PureNominalOverloadProbe.cpp
    Sema/PureNominalOverloadProbeAudit.cpp
    Sema/PreparedCallParityAudit.cpp
    Sema/SemanticIndex.cpp
    Sema/MemorySummary.cpp
    Sema/MemoryEvidence.cpp
    Sema/MemoryContract.cpp
    Sema/PAL_Checker.cpp
    Sema/TopologyCacheEval.cpp
    Type.cpp
)

add_library(toka_frontend STATIC ${toka_frontend_sources})

add_executable(tokac
    main.cpp
    CodeGen/CodeGen.cpp
    CodeGen/CodeGen_Expr.cpp
    CodeGen/CodeGen_Stmt.cpp
    CodeGen/CodeGen_Memory.cpp
    CodeGen/CodeGen_Decl.cpp
)

# Link against LLVM libraries
set(llvm_components Core Passes Analysis Coroutines MC MCDisassembler Target AsmPrinter AsmParser Option Object BinaryFormat TargetParser Demangle BitReader BitWriter LTO WindowsDriver WindowsManifest ProfileData LibDriver ${LLVM_TARGETS_TO_BUILD} Support)
if(WIN32 AND TARGET LLVM)
    # MSYS2 LLD archives are built against the LLVM DLL and reference __imp_ symbols.
    set(llvm_libs LLVM)
else()
    llvm_map_components_to_libnames(llvm_libs ${llvm_components})
endif()

set(lld_names lldELF lldMinGW lldCOFF lldMachO lldWasm lldCommon)
set(lld_libs "")

foreach(libname ${lld_names})
    find_library(LIB_${libname} NAMES ${libname} ${libname}-20 HINTS ${LLVM_LIBRARY_DIRS} /usr/lib/llvm-20/lib /usr/lib/llvm-19/lib)
    if(LIB_${libname})
        list(APPEND lld_libs ${LIB_${libname}})
    else()
        # Fallback to literal name if find_library fails (might work for some systems)
        list(APPEND lld_libs ${libname})
    endif()
endforeach()

target_link_libraries(toka_frontend PUBLIC ${llvm_libs})
target_link_libraries(tokac PUBLIC toka_frontend ${lld_libs})

add_executable(tokalsp "${CMAKE_SOURCE_DIR}/tools/tokalsp/main.cpp")
target_link_libraries(tokalsp PRIVATE toka_frontend)

set(zstd_hints ${LLVM_LIBRARY_DIRS})
if(APPLE)
    execute_process(
        COMMAND brew --prefix zstd
        OUTPUT_VARIABLE ZSTD_HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ZSTD_HOMEBREW_PREFIX)
        list(APPEND zstd_hints "${ZSTD_HOMEBREW_PREFIX}/lib")
    endif()
    if(HOMEBREW_PREFIX)
        list(APPEND zstd_hints "${HOMEBREW_PREFIX}/opt/zstd/lib" "${HOMEBREW_PREFIX}/lib")
    endif()
    list(APPEND zstd_hints
        "/opt/homebrew/opt/zstd/lib"
        "/opt/homebrew/lib"
        "/usr/local/opt/zstd/lib"
        "/usr/local/lib"
    )
endif()

find_library(ZSTD_LIBRARY NAMES zstd libzstd HINTS ${zstd_hints})
if(NOT ZSTD_LIBRARY)
    message(FATAL_ERROR "zstd library not found. Install zstd or set ZSTD_LIBRARY to the libzstd path.")
endif()

if(APPLE)
    target_link_libraries(toka_frontend PUBLIC ${ZSTD_LIBRARY} z curses)
    target_link_libraries(tokac PUBLIC ${ZSTD_LIBRARY} z curses)
elseif(UNIX)
    target_link_libraries(toka_frontend PUBLIC ${ZSTD_LIBRARY} z)
    target_link_libraries(tokac PUBLIC ${ZSTD_LIBRARY} z)
else()
    target_link_libraries(toka_frontend PUBLIC ${ZSTD_LIBRARY})
    target_link_libraries(tokac PUBLIC ${ZSTD_LIBRARY})
endif()
