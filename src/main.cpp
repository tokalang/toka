// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "toka/CodeGen.h"
#include "toka/DiagnosticEngine.h"
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/TKIExporter.h"
#include "toka/SourceLocation.h"
#include "toka/SourceManager.h"
#include "toka/PathUtils.h"
#include "toka/ModuleResolver.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/Coroutines/CoroCleanup.h"
#include "llvm/Transforms/Coroutines/CoroEarly.h"
#include "llvm/Transforms/Coroutines/CoroElide.h"
#include "llvm/Transforms/Coroutines/CoroSplit.h"

#include "toka/Version.h"
#include "toka/InterfaceVersion.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <set>
#include <list>
#include <sstream>

#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include <sstream>

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/TargetParser/Triple.h"
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
extern "C" int toka_setmode(int fd, int mode) { return 0; }
extern "C" int toka_fileno(FILE *f) { return fileno(f); }
#else
#include <io.h>
#include <fcntl.h>
extern "C" int toka_setmode(int fd, int mode) { return _setmode(fd, mode); }
extern "C" int toka_fileno(FILE *f) { return _fileno(f); }
#endif

#include "lld/Common/Driver.h"

LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(macho)
LLD_HAS_DRIVER(mingw)
LLD_HAS_DRIVER(wasm)

bool linkWithLLD(std::string objFile, std::vector<std::string> extraObjs, std::string outputFile) {
    llvm::Triple triple(toka::Parser::TargetTriple);
    if (triple.isOSWASI() || triple.getArch() == llvm::Triple::wasm32 || triple.getArch() == llvm::Triple::wasm64) {
        std::vector<std::string> searchPaths = {
            "/usr/lib/wasm32-wasi",
            "/usr/share/wasi-sysroot/lib/wasm32-wasi",
            "/opt/homebrew/share/wasi-sysroot/lib/wasm32-wasi",
            "/usr/local/share/wasi-sysroot/lib/wasm32-wasi"
        };
        std::string libDir = "";
        for (const auto &path : searchPaths) {
            if (std::filesystem::exists(path + "/crt1.o")) {
                libDir = path;
                break;
            }
        }
        if (libDir.empty()) {
            llvm::errs() << "Linker error: WASI sysroot library directory not found. Please install wasi-libc.\n";
            return false;
        }

        std::vector<const char *> args;
        args.push_back("toka-lld");

        // Keep strings alive during LLD invocation
        std::string crtPath = libDir + "/crt1.o";
        std::string libDirArg = "-L" + libDir;

        args.push_back(crtPath.c_str());
        args.push_back(objFile.c_str());
        for (const auto &extra : extraObjs) {
            args.push_back(extra.c_str());
        }
        args.push_back("-o");
        args.push_back(outputFile.c_str());
        args.push_back(libDirArg.c_str());
        args.push_back("-lc");

        return lld::wasm::link(args, llvm::outs(), llvm::errs(), false, false);
    }

    std::vector<const char *> args;
    args.push_back("toka-lld"); // dummy argv[0]

#ifdef _WIN32
    // Windows builds using MSYS2 or MinGW. LLD doesn't resolve default C libraries without explicit paths.
    // Since MSYS2 paths can be dynamic (e.g., D:/a/_temp/msys64 on GitHub Actions), 
    // we shell out to gcc which acts as the linker driver and knows all implicit library paths.
    std::string cmd = "gcc \"" + objFile + "\"";
    for (const auto &extra : extraObjs) {
        cmd += " \"" + extra + "\"";
    }
    cmd += " -o \"" + outputFile + "\" -lws2_32 -lshell32";
    return system(cmd.c_str()) == 0;
#elif defined(__APPLE__)
    args.push_back("-w");
    args.push_back("-arch");
#if defined(__arm64__) || defined(__aarch64__)
    args.push_back("arm64");
#else
    args.push_back("x86_64");
#endif
    args.push_back("-platform_version");
    args.push_back("macos");
    args.push_back("12.0.0");
    args.push_back("12.0.0");
    
    args.push_back("-syslibroot");
    args.push_back("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk");
    
    args.push_back(objFile.c_str());
    for (const auto &extra : extraObjs) {
        args.push_back(extra.c_str());
    }
    args.push_back("-o");
    args.push_back(outputFile.c_str());
    args.push_back("-lSystem");
    return lld::macho::link(args, llvm::outs(), llvm::errs(), false, false);
#else
    // Linux and other Unix-like systems.
    // Use the system 'cc' as the linker driver to automatically correctly resolve 
    // crt1.o, crti.o, crtbegin.o, crtend.o, and crtn.o which are absolutely required 
    // for proper .init_array (global constructors) execution.
    std::string cmd = "cc \"" + objFile + "\"";
    for (const auto &extra : extraObjs) {
        cmd += " \"" + extra + "\"";
    }
    cmd += " -o \"" + outputFile + "\" -lm -lc";
    return system(cmd.c_str()) == 0;
#endif
}
extern "C" const char *__asan_default_options() {
  return "detect_leaks=0";
}

bool verboseMode = false;
bool g_JsonDiagnostics = false;

static std::string normalizePath(const std::string &path) {
  return toka::PathUtils::normalize(path);
}


int main(int argc, char **argv) {
  std::vector<std::string> searchPaths;
  auto splitEnvPaths = [&](const char* envName) {
    if (const char* env_p = std::getenv(envName)) {
      std::string envStr(env_p);
      std::stringstream ss(envStr);
      std::string item;
      while (std::getline(ss, item, llvm::sys::EnvPathSeparator)) {
        if (!item.empty()) {
          searchPaths.push_back(item);
        }
      }
      
      // Dual-Separator Fallback for MSYS2/MinGW mixed environments on Windows
      if (llvm::sys::EnvPathSeparator == ';' && searchPaths.size() <= 1) {
        std::string singlePath = searchPaths.empty() ? envStr : searchPaths[0];
        size_t colonCount = 0;
        for (char c : singlePath) if (c == ':') colonCount++;
        
        // If it contains colons and doesn't look like a standard Windows drive letter path (like C:\)
        if (colonCount > 0 && (singlePath.size() < 2 || singlePath[1] != ':')) {
          searchPaths.clear();
          std::stringstream ss2(singlePath);
          std::string item2;
          while (std::getline(ss2, item2, ':')) {
            if (!item2.empty()) {
              searchPaths.push_back(item2);
            }
          }
        }
      }
    }
  };

  splitEnvPaths("TOKA_LIB");
  splitEnvPaths("TOKA_PATH");

  std::vector<std::string> sourceFiles;
  std::vector<std::string> objectFiles;
  std::map<std::string, std::string> pkgMap;
  bool disableBorrowCheck = false;
  bool emitObj = false;
  bool compileOnly = false;
  bool emitInterface = false;
  bool dumpDependencies = false;
  bool dumpJson = false;
  llvm::OptimizationLevel optLevel = llvm::OptimizationLevel::O0;
  std::string outputFile = "";
  std::string cliTargetTriple = "";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-target" || arg == "--target") {
      if (i + 1 < argc) {
        cliTargetTriple = argv[++i];
      } else {
        llvm::errs() << "-target requires an argument\n";
        return 1;
      }
    } else if (arg == "-I") {
      if (i + 1 < argc) {
        searchPaths.push_back(argv[++i]);
      } else {
        llvm::errs() << "-I requires an argument\n";
        return 1;
      }
    } else if (arg.rfind("-I", 0) == 0 && arg.length() > 2) {
      searchPaths.push_back(arg.substr(2));
    } else if (arg == "--version" || arg == "-V") {
      llvm::outs() << "toka version " << TOKA_VERSION_STRING << " (Built: " << __DATE__ << " " << __TIME__ << ")\n";
      return 0;
    } else if (arg == "--check-json") {
      g_JsonDiagnostics = true;
    } else if (arg == "--disable-borrow-check") {
      disableBorrowCheck = true;
    } else if (arg == "--dump-dependencies") {
      dumpDependencies = true;
    } else if (arg == "--dump-dependencies=json" || arg == "--dump-module-graph-json") {
      dumpDependencies = true;
      dumpJson = true;
    } else if (arg == "--pkg" || arg == "-P") {
      if (i + 1 < argc) {
        std::string mapping = argv[++i];
        size_t eqPos = mapping.find('=');
        if (eqPos != std::string::npos) {
          pkgMap[mapping.substr(0, eqPos)] = mapping.substr(eqPos + 1);
        } else {
          llvm::errs() << "--pkg requires format name=path\n";
          return 1;
        }
      } else {
        llvm::errs() << "--pkg requires an argument\n";
        return 1;
      }
    } else if (arg == "-v" || arg == "--verbose") {
      verboseMode = true;
    } else if (arg == "-o") {
      if (i + 1 < argc) {
        outputFile = argv[++i];
        if (outputFile.length() > 2 && outputFile.substr(outputFile.length() - 2) == ".o") {
          emitObj = true;
          compileOnly = true;
        } else if (outputFile.length() > 3 && outputFile.substr(outputFile.length() - 3) == ".ll") {
          emitObj = false;
        } else {
          emitObj = true;
        }
      } else {
        llvm::errs() << "-o requires an argument\n";
        return 1;
      }
    } else if (arg == "-c") {
      compileOnly = true;
      emitObj = true;
    } else if (arg == "-O0") {
      optLevel = llvm::OptimizationLevel::O0;
    } else if (arg == "-O1") {
      optLevel = llvm::OptimizationLevel::O1;
    } else if (arg == "-O2") {
      optLevel = llvm::OptimizationLevel::O2;
    } else if (arg == "-O3") {
      optLevel = llvm::OptimizationLevel::O3;
    } else if (arg == "-Os") {
      optLevel = llvm::OptimizationLevel::Os;
    } else if (arg == "-Oz") {
      optLevel = llvm::OptimizationLevel::Oz;
    } else if (arg == "--emit-obj") {
      emitObj = true;
    } else if (arg == "--emit-llvm") {
      emitObj = false;
    } else if (arg == "--emit-interface") {
      emitInterface = true;
    } else if (arg.rfind("-", 0) == 0) {
      // Ignore other flags for now or report error
    } else {
      if (arg.length() > 2 && (arg.substr(arg.length() - 2) == ".o" || arg.substr(arg.length() - 2) == ".a")) {
        objectFiles.push_back(arg);
      } else {
        sourceFiles.push_back(arg);
      }
    }
  }

  if (compileOnly) {
    emitInterface = true;
  }

  if (sourceFiles.empty()) {
    llvm::errs() << "Usage: tokac [options] <source.tk> [objects...]\n";
    return 1;
  }

  std::string resolvedTargetTriple = llvm::sys::getDefaultTargetTriple();
  if (!cliTargetTriple.empty()) {
    resolvedTargetTriple = cliTargetTriple;
  } else if (const char* envTriple = std::getenv("TOKA_TARGET_TRIPLE")) {
    resolvedTargetTriple = envTriple;
  }
  toka::Parser::TargetTriple = resolvedTargetTriple;

  toka::SourceManager sm;
  toka::DiagnosticEngine::init(sm);

  std::vector<std::unique_ptr<toka::Module>> astModules;
  toka::ModuleResolver resolver(sm, searchPaths, pkgMap);
  bool parseSuccess = true;
  for (const auto &file : sourceFiles) {
    if (!resolver.resolveAndParse(file, astModules)) {
      parseSuccess = false;
    }
  }

  if (dumpDependencies) {
    if (!parseSuccess || toka::DiagnosticEngine::hasErrors()) {
      return 1;
    }
    if (dumpJson) {
      std::string rootPath = "";
      for (const auto &ast : astModules) {
        if (ast->IsRootModule) {
          rootPath = toka::PathUtils::canonicalize(ast->SourcePath);
          break;
        }
      }

      auto escapeJsonString = [](const std::string &s) -> std::string {
          std::string res;
          for (char c : s) {
              if (c == '\\') res += "\\\\";
              else if (c == '"') res += "\\\"";
              else if (c == '\n') res += "\\n";
              else if (c == '\r') res += "\\r";
              else if (c == '\t') res += "\\t";
              else res += c;
          }
          return res;
      };

      auto cacheStatusToString = [](toka::TKICacheStatus status) -> std::string {
          switch (status) {
              case toka::TKICacheStatus::Ok: return "Ok";
              case toka::TKICacheStatus::ReadError: return "ReadError";
              case toka::TKICacheStatus::MissingCompilerVersion: return "MissingCompilerVersion";
              case toka::TKICacheStatus::MissingFormatVersion: return "MissingFormatVersion";
              case toka::TKICacheStatus::MissingTargetTriple: return "MissingTargetTriple";
              case toka::TKICacheStatus::MissingSourceHash: return "MissingSourceHash";
              case toka::TKICacheStatus::CompilerVersionMismatch: return "CompilerVersionMismatch";
              case toka::TKICacheStatus::FormatVersionMismatch: return "FormatVersionMismatch";
              case toka::TKICacheStatus::TargetTripleMismatch: return "TargetTripleMismatch";
              case toka::TKICacheStatus::SourceHashMismatch: return "SourceHashMismatch";
          }
          return "Unknown";
      };

      auto calculateFNV1a = [](const std::string &content) -> std::string {
          uint64_t hash = 14695981039346656037ULL;
          for (char c : content) {
              hash ^= static_cast<uint8_t>(c);
              hash *= 1099511628211ULL;
          }
          char buf[17];
          snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
          return std::string(buf);
      };

      auto getFileHash = [&](const std::string &filePath) -> std::string {
          std::ifstream ifs(filePath, std::ios::binary);
          if (!ifs) return "";
          std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
          return calculateFNV1a(content);
      };

      llvm::outs() << "{\n";
      llvm::outs() << "  \"root\": \"" << escapeJsonString(rootPath) << "\",\n";
      llvm::outs() << "  \"modules\": {\n";

      bool firstModule = true;
      const auto &records = resolver.getResolutionRecords();
      const auto &dependencies = resolver.getDependencies();

      for (const auto &pair : records) {
        if (!firstModule) {
          llvm::outs() << ",\n";
        }
        firstModule = false;

        const auto &info = pair.second;
        llvm::outs() << "    \"" << escapeJsonString(info.CanonicalPath) << "\": {\n";
        llvm::outs() << "      \"kind\": \"" << (info.IsInterface ? "interface" : "source") << "\",\n";
        llvm::outs() << "      \"fallback_triggered\": " << (info.FallbackTriggered ? "true" : "false") << ",\n";
        llvm::outs() << "      \"cache_status\": \"" << cacheStatusToString(info.CacheStatus) << "\",\n";
        llvm::outs() << "      \"cache_status_reason\": \"" << escapeJsonString(info.CacheStatusReason) << "\",\n";
        llvm::outs() << "      \"target_triple\": \"" << escapeJsonString(toka::Parser::TargetTriple) << "\",\n";
        llvm::outs() << "      \"compiler_version\": \"" << TOKA_COMPILER_INTERFACE_VERSION << "\",\n";
        llvm::outs() << "      \"interface_version\": \"" << TOKA_INTERFACE_FORMAT_VERSION << "\",\n";
        llvm::outs() << "      \"source_hash\": \"" << getFileHash(info.CanonicalPath) << "\",\n";
        llvm::outs() << "      \"outputs\": {\n";
        llvm::outs() << "        \"interface\": \"" << escapeJsonString(toka::PathUtils::getInterfacePath(outputFile, info.CanonicalPath)) << "\",\n";

        std::string objPath;
        if (info.CanonicalPath == rootPath && !outputFile.empty()) {
            objPath = outputFile;
        } else {
            objPath = toka::PathUtils::normalize(std::filesystem::path(info.CanonicalPath).replace_extension(".o").string());
        }
        llvm::outs() << "        \"object\": \"" << escapeJsonString(objPath) << "\"\n";
        llvm::outs() << "      },\n";
        llvm::outs() << "      \"dependencies\": [";

        auto depIt = dependencies.find(info.CanonicalPath);
        if (depIt != dependencies.end()) {
          bool firstDep = true;
          for (const auto &dep : depIt->second) {
            if (!firstDep) {
              llvm::outs() << ", ";
            }
            firstDep = false;
            llvm::outs() << "\"" << escapeJsonString(dep) << "\"";
          }
        }
        llvm::outs() << "]\n";
        llvm::outs() << "    }";
      }

      llvm::outs() << "\n  }\n";
      llvm::outs() << "}\n";
      return 0;
    }

    for (const auto &pair : resolver.getDependencies()) {
      llvm::outs() << pair.first << ":";
      for (const auto &dep : pair.second) {
        llvm::outs() << " " << dep;
      }
      llvm::outs() << "\n";
    }
    return 0;
  }

  if (!parseSuccess || astModules.empty() || toka::DiagnosticEngine::hasErrors()) {
    llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted due to previous syntax or I/O errors.\n";
    return 1;
  }

  if (verboseMode) llvm::errs() << "Parse Successful. Running Semantic Analysis...\n";

  toka::Sema sema;
  sema.setBorrowCheckEnabled(!disableBorrowCheck);
  
  // Pass 1: Declare all global symbols across all modules to build the global module map
  for (const auto &ast : astModules) {
    sema.declareGlobals(*ast);
  }

  // Pass 2: Run full semantic analysis on all modules
  for (const auto &ast : astModules) {
    if (!sema.checkModule(*ast) || toka::DiagnosticEngine::hasErrors()) {
      llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted due to previous semantic errors.\n";
      return 1;
    }
  }

  // Pass 3: Run global shape sovereignty checks once all modules are resolved
  sema.checkShapeSovereignty();
  if (toka::DiagnosticEngine::hasErrors()) {
    llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted due to previous semantic errors.\n";
    return 1;
  }

  if (emitInterface) {
    for (const auto &ast : astModules) {
      if (ast->IsRootModule) {
        std::string outPath = toka::PathUtils::getInterfacePath(outputFile, ast->SourcePath);
        
        if (verboseMode) llvm::errs() << "Exporting TKI Interface to " << outPath << "...\n";
        
        std::error_code EC;
        llvm::raw_fd_ostream os(outPath, EC, llvm::sys::fs::OF_None);
        if (EC) {
          llvm::errs() << "Error writing TKI file " << outPath << ": " << EC.message() << "\n";
          return 1;
        }
        
        toka::TKIExporter exporter(os);
        exporter.exportModule(*ast);
      }
    }
  }

  if (verboseMode) fprintf(stderr, "Sema Successful. Merging and Generating IR...\n");
  fflush(stderr);

  if (verboseMode) fprintf(stderr, "Initializing LLVM Context...\n");
  fflush(stderr);
  llvm::LLVMContext context;
  if (verboseMode) fprintf(stderr, "Instantiating CodeGen for module: %s\n", argv[1]);
  fflush(stderr);
  toka::CodeGen codegen(context, argv[1]);
  codegen.importParenthesizedRecordTypes(sema.getParenthesizedRecordTypes());
  if (verboseMode) fprintf(stderr, "CodeGen instantiated.\n");
  fflush(stderr);

  // --- Initialize TargetMachine & DataLayout early to avoid CodeGen crashes ---
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllAsmPrinters();

  auto TargetTriple = toka::Parser::TargetTriple;
  std::string Error;
  llvm::Triple TheTriple(TargetTriple);
  auto Target = llvm::TargetRegistry::lookupTarget("", TheTriple, Error);
  if (!Target) {
    llvm::errs() << "Target lookup error: " << Error;
    return 1;
  }

  auto CPU = "generic";
  auto Features = "";
  llvm::TargetOptions opt;
  std::optional<llvm::Reloc::Model> RM = llvm::Reloc::PIC_;
#if defined(_WIN32) || defined(__MINGW32__)
  auto TargetMachine = Target->createTargetMachine(llvm::Triple(TargetTriple), CPU, Features, opt, RM);
#else
  auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);
#endif

  codegen.getModule()->setDataLayout(TargetMachine->createDataLayout());
#if defined(_WIN32) || defined(__MINGW32__)
  codegen.getModule()->setTargetTriple(llvm::Triple(TargetTriple));
#else
  codegen.getModule()->setTargetTriple(TargetTriple);
#endif
  // ----------------------------------------------------------------------------

  std::unique_ptr<toka::Module> genericModule = sema.extractGenericRegistry();
  
  if (verboseMode) fprintf(stderr, "Pass 1: Discovery (Registration)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.discover(*ast);
  }
  if (genericModule) codegen.discover(*genericModule);

  if (verboseMode) fprintf(stderr, "Pass 2: Resolution (Signatures)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.resolveSignatures(*ast);
  }
  if (genericModule) codegen.resolveSignatures(*genericModule);

  if (verboseMode) fprintf(stderr, "Pass 3: Generation (Emission)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.generate(*ast);
  }
  if (genericModule) codegen.generate(*genericModule);
  
  codegen.finalizeGlobals();

  if (codegen.hasErrors() || toka::DiagnosticEngine::hasErrors()) {
    llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted during code generation.\n";
    return 1;
  }

  if (!compileOnly && emitObj) {
    if (!codegen.getModule()->getFunction("main") && !codegen.getModule()->getFunction("__main_void") && !codegen.getModule()->getFunction("__main_argc_argv")) {
      llvm::errs() << "\033[1;31merror[E0601]\033[0m: Entry function 'main' not found.\n"
                   << "  \033[1;34m|\033[0m\n"
                   << "  \033[1;34m=\033[0m note: Are you trying to compile a library? Try using the '-c' flag to skip linking.\n\n";
      return 1;
    }
  }

  if (verboseMode) fprintf(stderr, "Running Module Verifier...\n");
  fflush(stderr);
  if (llvm::verifyModule(*codegen.getModule(), &llvm::errs())) {
    llvm::errs() << "Fatal Error: LLVM IR Verification Failed!\n";
    return 1;
  }
  
  if (verboseMode) fprintf(stderr, "Pass 4: Optimization (Coroutines & O2)...\n");
  fflush(stderr);

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PassBuilder PB;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  
  PB.registerPipelineStartEPCallback(
      [](llvm::ModulePassManager &MPM, llvm::OptimizationLevel Level) {
        MPM.addPass(llvm::CoroEarlyPass());
        llvm::CGSCCPassManager CGPM;
        CGPM.addPass(llvm::CoroSplitPass());
        MPM.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
        llvm::FunctionPassManager FPM;
        FPM.addPass(llvm::CoroElidePass());
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        MPM.addPass(llvm::CoroCleanupPass());
      });

  llvm::ModulePassManager MPM;
  if (optLevel == llvm::OptimizationLevel::O0) {
    MPM = PB.buildO0DefaultPipeline(optLevel);
  } else {
    MPM = PB.buildPerModuleDefaultPipeline(optLevel);
  }
  MPM.run(*codegen.getModule(), MAM);

  std::string finalOutput = outputFile;
  std::string objFile = outputFile;
  if (emitObj && !compileOnly) {
    if (finalOutput.empty()) {
      finalOutput = "a.out";
    }
    llvm::SmallString<128> TempPath;
    if (auto Err = llvm::sys::fs::createTemporaryFile("toka_tmp", "o", TempPath)) {
      llvm::errs() << "Error creating temporary file: " << Err.message() << "\n";
      return 1;
    }
    objFile = std::string(TempPath.c_str());
  }

  if (objFile.empty() && emitObj) {
    llvm::errs() << "Error: compilation requires an output file specified with -o\n";
    return 1;
  }

  if (emitObj) {
    if (verboseMode) fprintf(stderr, "Initializing TargetMachine for native emission...\n");
    fflush(stderr);

    // TargetMachine is already initialized above.

    std::error_code EC;
    llvm::raw_fd_ostream dest(objFile, EC, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::errs() << "Could not open file: " << EC.message() << "\n";
      return 1;
    }

    llvm::legacy::PassManager pass;
    if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
      llvm::errs() << "TargetMachine can't emit a file of this type\n";
      return 1;
    }

    pass.run(*codegen.getModule());
    dest.flush();
    dest.close();
    if (verboseMode) fprintf(stderr, "Object file emitted successfully.\n");

    if (!compileOnly) {
      if (verboseMode) fprintf(stderr, "Linking executable (internal LLD): %s\n", finalOutput.c_str());
      fflush(stderr);
      
      llvm::Triple triple(toka::Parser::TargetTriple);
      std::string rtExtension = (triple.isOSWindows()) ? ".obj" : ".o";
      std::string rtFileName = "toka_rt" + rtExtension;
      if (triple.isOSWASI() || triple.getArch() == llvm::Triple::wasm32 || triple.getArch() == llvm::Triple::wasm64) {
          rtFileName = "toka_rt.wasm.o";
      }
      std::string tokaRtPath;
      
      // 1. Prioritize local relative paths to ensure local development overrides global installations
      {
        llvm::SmallString<128> localPath1("lib");
        llvm::sys::path::append(localPath1, "sys", rtFileName);
        if (llvm::sys::fs::exists(localPath1)) {
          tokaRtPath = std::string(localPath1.str());
        } else {
          llvm::SmallString<128> localPath2("../lib");
          llvm::sys::path::append(localPath2, "sys", rtFileName);
          if (llvm::sys::fs::exists(localPath2)) {
            tokaRtPath = std::string(localPath2.str());
          }
        }
      }
      
      // 2. Search in searchPaths (including TOKA_LIB and -I)
      if (tokaRtPath.empty()) {
        for (const auto &p : searchPaths) {
          llvm::SmallString<128> testPath(p);
          llvm::sys::path::append(testPath, "sys", rtFileName);
          if (llvm::sys::fs::exists(testPath)) {
            tokaRtPath = std::string(testPath.str());
            break;
          }
        }
      }

      // 3. Absolute fallback
      if (tokaRtPath.empty()) {
        llvm::SmallString<128> fallbackPath("/usr/local/lib/toka");
        llvm::sys::path::append(fallbackPath, "sys", rtFileName);
        if (llvm::sys::fs::exists(fallbackPath)) {
          tokaRtPath = std::string(fallbackPath.str());
        }
      }

      if (tokaRtPath.empty()) {
        llvm::errs() << "\033[1;31m[FAILED]\033[0m Core runtime '" << rtFileName << "' not found in search paths. Please ensure TOKA_LIB is set correctly.\n";
        return 1;
      }

      // Convert all backslashes to forward slashes to prevent escape sequences in LLD / shells
      for (char &c : tokaRtPath) {
        if (c == '\\') c = '/';
      }
      
      bool hasRt = false;
      for (const auto &obj : objectFiles) {
        if (obj.find("toka_rt") != std::string::npos) {
          hasRt = true;
          break;
        }
      }
      
      if (!hasRt) {
        objectFiles.push_back(tokaRtPath);
      }

      if (!linkWithLLD(objFile, objectFiles, finalOutput)) {
        llvm::errs() << "Linker error: LLD failed\n";
        return 1;
      }
      std::remove(objFile.c_str());
    }
  } else {
    if (outputFile.empty()) {
      codegen.print(llvm::outs());
    } else {
      std::error_code EC;
      llvm::raw_fd_ostream dest(outputFile, EC, llvm::sys::fs::OF_None);
      if (EC) {
        llvm::errs() << "Could not open file: " << EC.message() << "\n";
        return 1;
      }
      codegen.print(dest);
    }
  }


  return 0;
}
