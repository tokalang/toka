add_executable(tokac main.cpp Basic/SourceManager.cpp Basic/ModuleResolver.cpp Lexer/Lexer.cpp
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
Sema/PAL_Checker.cpp
Type.cpp
CodeGen/CodeGen.cpp 
CodeGen/CodeGen_Expr.cpp 
CodeGen/CodeGen_Stmt.cpp 
CodeGen/CodeGen_Memory.cpp
CodeGen/CodeGen_Decl.cpp
AST/TKIExporter.cpp
)

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs Core Passes Analysis Coroutines MC MCDisassembler Target AsmPrinter AsmParser Option Object BinaryFormat TargetParser Demangle BitReader BitWriter LTO WindowsDriver WindowsManifest ProfileData LibDriver ${LLVM_TARGETS_TO_BUILD} Support)

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

target_link_libraries(tokac PUBLIC ${lld_libs} ${llvm_libs})

if(APPLE)
    target_link_libraries(tokac PUBLIC zstd z curses)
elseif(UNIX)
    target_link_libraries(tokac PUBLIC zstd z)
else()
    target_link_libraries(tokac PUBLIC zstd)
endif()
