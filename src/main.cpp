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
#include "toka/AssignmentStats.h"
#include "toka/DiagnosticEngine.h"
#include "toka/HandleSurfaceStats.h"
#include "toka/Lexer.h"
#include "toka/MemoryContract.h"
#include "toka/MemoryEvidence.h"
#include "toka/MemorySummary.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/SemanticDependencyClosure.h"
#include "toka/SemanticIndex.h"
#include "toka/SemanticEvidence.h"
#include "toka/SemanticManifestEnvelope.h"
#include "toka/SemanticManifestAttestation.h"
#include "toka/TopologyCacheEval.h"
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
#include <chrono>
#include <charconv>
#include <cctype>
#include <set>
#include <list>
#include <sstream>
#include <vector>

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

static std::string calculateFNV1a(const std::string &str) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(buf);
}

namespace {

void printHelp() {
  llvm::outs()
      << "Usage: tokac [options] <source.tk> [objects...]\n\n"
      << "Options:\n"
      << "  -h, --help                      Show this help\n"
      << "  -V, --version                   Show compiler version\n"
      << "  -I <path>                       Add an import search path\n"
      << "  -P, --pkg <name=path>           Map a package name to a path\n"
      << "  -o <file>                       Write output to file\n"
      << "  -c                              Compile without linking\n"
      << "  -g                              Emit DWARF debug information\n"
      << "  -O0|-O1|-O2|-O3|-Os|-Oz        Select optimization level\n"
      << "  --target <triple>               Select target triple\n"
      << "  --emit-obj                      Emit native object code\n"
      << "  --emit-llvm                     Emit LLVM IR\n"
      << "  --emit-interface                Emit a TKI interface\n"
      << "  --encap-slice1-facts=json       Dump audit-only @Encap Slice 1 facts\n"
      << "  --link-search <path>            Add a native library search path\n"
      << "  --link-lib <name>               Link a native library by name\n"
      << "  --link-framework <name>         Link a macOS system framework by name\n"
      << "  --check-json                    Emit JSON Lines diagnostics\n"
      << "  --diagnostics-json              Emit structured diagnostics JSON\n"
      << "  --check-only                    Stop after semantic checking\n"
      << "  --validate-semantic-manifests   Fail-closed validation of admitted source-less TKI sidecars\n"
      << "  --validate-semantic-manifest-attestations\n"
      << "                                  Validate bodyless Outcome providers against local object attestations\n"
      << "  --semantic-manifest-provenance-dir <dir>\n"
      << "                                  Emit local object-bound Outcome attestations into .tki.tsm\n"
      << "  --explain[=json] <code>         Explain a diagnostic code\n"
      << "  --semantic-evidence=json        Emit public semantic evidence v1\n"
      << "  --cede-obligations=json         Emit cede obligation evidence v1\n"
      << "  --capabilities=json             Emit H/P call capability pilot v1\n"
      << "  --todo-goals=json               Emit typed-todo goals v1\n"
      << "  --conditional-facts=json        Emit typed-todo conditional facts v1\n"
      << "  --semantic-index=json           Emit the compiler semantic index\n"
      << "  --semantic-context=json         Emit bounded semantic context\n"
      << "  --semantic-query <kind>         Query the semantic index\n"
      << "  --query-file <file>             File for a semantic query\n"
      << "  --line <n> --character <n>      Zero-based query position\n"
      << "  --rename-to <name>              New name for a rename query\n"
      << "  -v, --verbose                   Enable progress output\n";
}

bool parseUnsignedArgument(const char *option, const char *value,
                           unsigned &result) {
  std::string text(value);
  auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size())
    return true;
  llvm::errs() << option << " requires a non-negative integer\n";
  return false;
}

class SemanticEvidenceDumpGuard {
public:
  SemanticEvidenceDumpGuard(bool &cedeObligations, bool &capabilities,
                            bool &todoGoals, bool &conditionalFacts)
      : CedeObligations(cedeObligations), Capabilities(capabilities),
        TodoGoals(todoGoals), ConditionalFacts(conditionalFacts) {}
  ~SemanticEvidenceDumpGuard() {
    if (toka::SemanticEvidence::isEnabled()) {
      if (ConditionalFacts)
        toka::SemanticEvidence::dumpConditionalFactsJSON(std::cout);
      else if (TodoGoals)
        toka::SemanticEvidence::dumpTodoGoalsJSON(std::cout);
      else if (Capabilities)
        toka::SemanticEvidence::dumpCapabilityCallsJSON(std::cout);
      else if (CedeObligations)
        toka::SemanticEvidence::dumpCedeObligationsJSON(std::cout);
      else
        toka::SemanticEvidence::dumpJSON(std::cout);
    }
  }

private:
  bool &CedeObligations;
  bool &Capabilities;
  bool &TodoGoals;
  bool &ConditionalFacts;
};

class StructuredDiagnosticsDumpGuard {
public:
  explicit StructuredDiagnosticsDumpGuard(bool &enabled) : Enabled(enabled) {}
  ~StructuredDiagnosticsDumpGuard() {
    if (Enabled)
      llvm::outs() << toka::DiagnosticEngine::structuredJSON() << '\n';
  }

private:
  bool &Enabled;
};

class MachineFailureDiagnosticsDumpGuard {
public:
  explicit MachineFailureDiagnosticsDumpGuard(bool &enabled) : Enabled(enabled) {}
  ~MachineFailureDiagnosticsDumpGuard() {
    if (Enabled && toka::DiagnosticEngine::hasErrors())
      llvm::outs() << toka::DiagnosticEngine::structuredJSON() << '\n';
  }

private:
  bool &Enabled;
};

} // namespace

static std::string getFinalInterfacePath(const std::string &outputFile, const std::string &sourcePath) {
    const char *envBuildDir = std::getenv("TOKA_BUILD_DIR");
    if (envBuildDir && envBuildDir[0] != '\0') {
        std::string canonical = toka::PathUtils::canonicalize(sourcePath);
        return toka::PathUtils::canonicalize(std::string(envBuildDir) + "/interfaces/" + calculateFNV1a(canonical) + ".tki");
    }
    return toka::PathUtils::getInterfacePath(outputFile, sourcePath);
}

static bool readExactFile(const std::string &path, std::string &content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    content.assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
    return true;
}

static bool validateSemanticManifestImports(
    const std::vector<std::unique_ptr<toka::Module>> &astModules) {
  std::vector<toka::Module *> resolvedModules;
  resolvedModules.reserve(astModules.size());
  for (const auto &ast : astModules)
    resolvedModules.push_back(ast.get());

  for (const auto &ast : astModules) {
    if (!ast->IsInterface || !ast->ShadowCoordinateKnown ||
        ast->CanonicalOutcomeDeclarationWitnesses.empty())
      continue;

    std::string reason;
    std::vector<std::string> closureErrors;
    auto closure = toka::SemanticDependencyClosure::calculate(
        *ast, resolvedModules, closureErrors);
    if (!closure) {
      reason = "semantic dependency closure could not be reconstructed";
      if (!closureErrors.empty())
        reason += ": " + closureErrors.front();
    } else {
      std::string interfaceContent;
      if (!readExactFile(ast->ResolvedPath, interfaceContent)) {
        reason = "resolved interface could not be read";
      } else {
        std::vector<std::string> records;
        std::string manifestClosure;
        const std::string manifestPath =
            toka::SemanticManifestEnvelope::sidecarPath(ast->ResolvedPath);
        const auto manifestStatus = toka::SemanticManifestEnvelope::load(
            manifestPath, interfaceContent,
            {ast->ShadowCrateId, ast->ShadowLogicalModulePath},
            toka::Parser::TargetTriple, *closure, records, manifestClosure,
            reason);
        if (manifestStatus == toka::SemanticManifestEnvelopeStatus::Valid) {
          std::vector<std::string> expected =
              ast->CanonicalOutcomeDeclarationWitnesses;
          std::sort(expected.begin(), expected.end());
          if (records != expected)
            reason =
                "semantic manifest CDW1 records do not match reconstructed declarations";
          else
            reason.clear();
        } else {
          reason = std::string(toka::toString(manifestStatus)) + ": " +
                   reason;
        }
      }
    }

    if (!reason.empty()) {
      toka::DiagnosticEngine::report(
          ast->Loc, toka::DiagID::ERR_SEMANTIC_MANIFEST_VALIDATION,
          ast->ResolvedPath, reason);
    }
  }
  return !toka::DiagnosticEngine::hasErrors();
}

struct SemanticManifestAttestationLinkObligation {
  toka::SourceLocation Loc;
  std::string InterfacePath;
  std::string InterfaceContent;
  toka::SemanticManifestCoordinate Module;
  std::string TargetTriple;
  std::string SemanticDependencyClosureDigest;
  std::string ObjectPath;
  std::vector<std::string> CDW1Records;
};

static bool validateSemanticManifestAttestationImports(
    const std::vector<std::unique_ptr<toka::Module>> &astModules,
    const std::string &provenanceStateDirectory,
    std::vector<SemanticManifestAttestationLinkObligation> &obligations) {
  obligations.clear();
  std::vector<toka::Module *> resolvedModules;
  resolvedModules.reserve(astModules.size());
  for (const auto &ast : astModules)
    resolvedModules.push_back(ast.get());

  for (const auto &ast : astModules) {
    if (!ast->RequiresSemanticManifestAttestation)
      continue;

    std::string reason;
    std::vector<std::string> closureErrors;
    const auto closure = toka::SemanticDependencyClosure::calculate(
        *ast, resolvedModules, closureErrors,
        std::set<const toka::Module *>{ast.get()});
    if (!closure) {
      reason = "semantic dependency closure could not be reconstructed";
      if (!closureErrors.empty())
        reason += ": " + closureErrors.front();
    } else {
      std::string interfaceContent;
      if (!readExactFile(ast->ResolvedPath, interfaceContent)) {
        reason = "resolved interface could not be read";
      } else {
        toka::SemanticManifestAttestationResult attestation;
        const auto status = toka::SemanticManifestAttestation::load(
            toka::SemanticManifestEnvelope::sidecarPath(ast->ResolvedPath),
            interfaceContent,
            {ast->ShadowCrateId, ast->ShadowLogicalModulePath},
            toka::Parser::TargetTriple, *closure, ast->BackingObjectPath,
            provenanceStateDirectory, attestation, reason);
        if (status == toka::SemanticManifestAttestationStatus::Valid) {
          std::vector<std::string> expected =
              ast->CanonicalOutcomeDeclarationWitnesses;
          std::sort(expected.begin(), expected.end());
          if (attestation.CDW1Records != expected) {
            reason = "Outcome fulfilment records do not match reconstructed declarations";
          } else {
            obligations.push_back({
                ast->Loc,
                ast->ResolvedPath,
                std::move(interfaceContent),
                {ast->ShadowCrateId, ast->ShadowLogicalModulePath},
                toka::Parser::TargetTriple,
                *closure,
                ast->BackingObjectPath,
                std::move(expected),
            });
            reason.clear();
          }
        } else {
          reason = std::string(toka::toString(status)) + ": " + reason;
        }
      }
    }

    if (!reason.empty()) {
      toka::DiagnosticEngine::report(
          ast->Loc, toka::DiagID::ERR_SEMANTIC_MANIFEST_ATTESTATION,
          ast->ResolvedPath, reason);
    }
  }
  return !toka::DiagnosticEngine::hasErrors();
}

static bool validateSemanticManifestAttestationLinkObligations(
    const std::vector<SemanticManifestAttestationLinkObligation> &obligations,
    const std::vector<std::string> &objectFiles,
    const std::string &provenanceStateDirectory) {
  std::set<std::string> selectedObjects;
  for (const std::string &object : objectFiles)
    selectedObjects.insert(toka::PathUtils::canonicalize(object));

  for (const auto &obligation : obligations) {
    std::string reason;
    if (selectedObjects.count(toka::PathUtils::canonicalize(
            obligation.ObjectPath)) == 0) {
      reason = "attested backing object is absent from final linker inputs";
    } else {
      toka::SemanticManifestAttestationResult attestation;
      const auto status = toka::SemanticManifestAttestation::load(
          toka::SemanticManifestEnvelope::sidecarPath(obligation.InterfacePath),
          obligation.InterfaceContent, obligation.Module, obligation.TargetTriple,
          obligation.SemanticDependencyClosureDigest, obligation.ObjectPath,
          provenanceStateDirectory, attestation, reason);
      if (status == toka::SemanticManifestAttestationStatus::Valid) {
        if (attestation.CDW1Records != obligation.CDW1Records)
          reason = "Outcome fulfilment records changed after import validation";
        else
          reason.clear();
      } else {
        reason = std::string(toka::toString(status)) + ": " + reason;
      }
    }
    if (!reason.empty()) {
      toka::DiagnosticEngine::report(
          obligation.Loc, toka::DiagID::ERR_SEMANTIC_MANIFEST_ATTESTATION,
          obligation.InterfacePath, reason);
    }
  }
  return !toka::DiagnosticEngine::hasErrors();
}

struct PendingSemanticManifestAttestation {
  std::string InterfacePath;
  toka::SemanticManifestAttestationPrepared Prepared;
};

struct TokaProfile {
    using Clock = std::chrono::steady_clock;

    bool Enabled = false;
    Clock::time_point Start;
    Clock::time_point Last;
    std::vector<std::pair<std::string, double>> Entries;

    explicit TokaProfile(bool enabled)
        : Enabled(enabled), Start(Clock::now()), Last(Start) {}

    void mark(const std::string &name) {
        if (!Enabled) return;
        auto now = Clock::now();
        std::chrono::duration<double, std::milli> elapsed = now - Last;
        Entries.push_back({name, elapsed.count()});
        Last = now;
    }

    void finish(const std::string &label) {
        if (!Enabled) return;
        auto now = Clock::now();
        std::chrono::duration<double, std::milli> total = now - Start;
        llvm::errs() << "[profile] " << label << "\n";
        for (const auto &entry : Entries) {
            llvm::errs() << "[profile]   " << entry.first << ": " << entry.second << " ms\n";
        }
        llvm::errs() << "[profile]   total: " << total.count() << " ms\n";
    }
};

#include "lld/Common/Driver.h"

LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(macho)
LLD_HAS_DRIVER(mingw)
LLD_HAS_DRIVER(wasm)

bool isSafeLinkLibraryName(const std::string &name) {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '+' || c == '.' || c == '-';
    });
}

bool isSafeFrameworkName(const std::string &name) {
    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front()))) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

bool linkWithLLD(std::string objFile, std::vector<std::string> extraObjs,
                 std::vector<std::string> linkSearchPaths,
                 std::vector<std::string> linkLibraries,
                 std::vector<std::string> linkFrameworks,
                 std::string outputFile) {
#if !defined(__APPLE__)
    if (!linkFrameworks.empty()) {
        llvm::errs() << "--link-framework is only supported on macOS\n";
        return false;
    }
#endif
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
        std::vector<std::string> nativeSearchFlags;
        std::vector<std::string> nativeLibraryFlags;
        nativeSearchFlags.reserve(linkSearchPaths.size());
        nativeLibraryFlags.reserve(linkLibraries.size());
        for (const auto &path : linkSearchPaths)
            nativeSearchFlags.push_back("-L" + path);
        for (const auto &library : linkLibraries)
            nativeLibraryFlags.push_back("-l" + library);
        for (const auto &flag : nativeSearchFlags) args.push_back(flag.c_str());
        for (const auto &flag : nativeLibraryFlags) args.push_back(flag.c_str());
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
    for (const auto &path : linkSearchPaths) {
        cmd += " -L\"" + path + "\"";
    }
#ifdef TOKA_OPENSSL_LIB_DIR
    if (llvm::sys::fs::exists(TOKA_OPENSSL_LIB_DIR)) {
        cmd += " -L\"" TOKA_OPENSSL_LIB_DIR "\"";
    }
#endif
    cmd += " -o \"" + outputFile + "\" -lws2_32 -lshell32 -lbcrypt";
#ifdef TOKA_HAS_OPENSSL
    cmd += " -lssl -lcrypto";
#endif
    for (const auto &library : linkLibraries) {
        cmd += " -l" + library;
    }
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

    std::string cmake_openssl_flag;
#ifdef TOKA_OPENSSL_LIB_DIR
    if (llvm::sys::fs::exists(TOKA_OPENSSL_LIB_DIR)) {
        cmake_openssl_flag = std::string("-L") + TOKA_OPENSSL_LIB_DIR;
        args.push_back(cmake_openssl_flag.c_str());
    }
#endif
#ifdef TOKA_HAS_OPENSSL
    if (llvm::sys::fs::exists("/opt/homebrew/opt/openssl@3/lib")) {
        args.push_back("-L/opt/homebrew/opt/openssl@3/lib");
    } else if (llvm::sys::fs::exists("/opt/homebrew/lib")) {
        args.push_back("-L/opt/homebrew/lib");
    } else if (llvm::sys::fs::exists("/usr/local/opt/openssl/lib")) {
        args.push_back("-L/usr/local/opt/openssl/lib");
    } else if (llvm::sys::fs::exists("/usr/local/lib")) {
        args.push_back("-L/usr/local/lib");
    }
#endif

    args.push_back(objFile.c_str());
    for (const auto &extra : extraObjs) {
        args.push_back(extra.c_str());
    }
    std::vector<std::string> nativeSearchFlags;
    std::vector<std::string> nativeLibraryFlags;
    std::vector<std::string> nativeFrameworkFlags;
    nativeSearchFlags.reserve(linkSearchPaths.size());
    nativeLibraryFlags.reserve(linkLibraries.size());
    for (const auto &path : linkSearchPaths)
        nativeSearchFlags.push_back("-L" + path);
    for (const auto &flag : nativeSearchFlags) args.push_back(flag.c_str());
    args.push_back("-o");
    args.push_back(outputFile.c_str());
#ifdef TOKA_HAS_OPENSSL
    args.push_back("-lssl");
    args.push_back("-lcrypto");
#endif
    for (const auto &library : linkLibraries)
        nativeLibraryFlags.push_back("-l" + library);
    for (const auto &flag : nativeLibraryFlags) args.push_back(flag.c_str());
    nativeFrameworkFlags.reserve(linkFrameworks.size());
    for (const auto &framework : linkFrameworks) {
        nativeFrameworkFlags.push_back("-framework");
        nativeFrameworkFlags.push_back(framework);
    }
    for (const auto &flag : nativeFrameworkFlags) args.push_back(flag.c_str());
    args.push_back("-lSystem");
    return lld::macho::link(args, llvm::outs(), llvm::errs(), false, false);
#else
    std::string cmd = "cc \"" + objFile + "\"";
    for (const auto &extra : extraObjs) {
        cmd += " \"" + extra + "\"";
    }
    for (const auto &path : linkSearchPaths) {
        cmd += " -L\"" + path + "\"";
    }
#ifdef TOKA_OPENSSL_LIB_DIR
    if (llvm::sys::fs::exists(TOKA_OPENSSL_LIB_DIR)) {
        cmd += " -L\"" TOKA_OPENSSL_LIB_DIR "\"";
    }
#endif
    cmd += " -o \"" + outputFile + "\"";
#ifdef TOKA_HAS_OPENSSL
    cmd += " -lssl -lcrypto";
#endif
    for (const auto &library : linkLibraries) {
        cmd += " -l" + library;
    }
    cmd += " -lm -lc";
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
  toka::DiagnosticEngine::reset();
  std::vector<std::string> searchPaths;
  std::vector<std::string> trustedSystemRoots = {"lib", "../lib"};
  auto splitEnvPaths = [&](const char* envName, std::vector<std::string> &out) {
    if (const char* env_p = std::getenv(envName)) {
      std::string envStr(env_p);
      std::stringstream ss(envStr);
      std::string item;
      std::vector<std::string> parsed;
      while (std::getline(ss, item, llvm::sys::EnvPathSeparator)) {
        if (!item.empty()) {
          parsed.push_back(item);
        }
      }

      // Dual-Separator Fallback for MSYS2/MinGW mixed environments on Windows
      if (llvm::sys::EnvPathSeparator == ';' && parsed.size() <= 1) {
        std::string singlePath = parsed.empty() ? envStr : parsed[0];
        size_t colonCount = 0;
        for (char c : singlePath) if (c == ':') colonCount++;

        // If it contains colons and doesn't look like a standard Windows drive letter path (like C:\)
        if (colonCount > 0 && (singlePath.size() < 2 || singlePath[1] != ':')) {
          parsed.clear();
          std::stringstream ss2(singlePath);
          std::string item2;
          while (std::getline(ss2, item2, ':')) {
            if (!item2.empty()) {
              parsed.push_back(item2);
            }
          }
        }
      }
      out.insert(out.end(), parsed.begin(), parsed.end());
    }
  };

  std::vector<std::string> tokaLibPaths;
  splitEnvPaths("TOKA_LIB", tokaLibPaths);
  searchPaths.insert(searchPaths.end(), tokaLibPaths.begin(), tokaLibPaths.end());
  trustedSystemRoots.insert(trustedSystemRoots.end(), tokaLibPaths.begin(),
                            tokaLibPaths.end());
  splitEnvPaths("TOKA_PATH", searchPaths);

  // An installed compiler is self-contained: discover the standard library
  // beside the SDK before falling back to cwd-relative development paths.
  std::string executable = llvm::sys::fs::getMainExecutable(
      argv[0], reinterpret_cast<void *>(&main));
  if (!executable.empty()) {
    std::filesystem::path binDir = std::filesystem::path(executable).parent_path();
    std::vector<std::filesystem::path> bundledLibs = {
        binDir.parent_path() / "share" / "toka" / "lib",
        binDir.parent_path() / "lib" / "toka",
        binDir.parent_path() / "lib",
        binDir / "lib",
    };
    for (const auto &candidate : bundledLibs) {
      if (std::filesystem::exists(candidate / "core" / "prelude.tk")) {
        std::string normalized = normalizePath(candidate.string());
        searchPaths.push_back(normalized);
        trustedSystemRoots.push_back(normalized);
        break;
      }
    }
  }

  std::vector<std::string> sourceFiles;
  std::vector<std::string> objectFiles;
  std::vector<std::string> linkSearchPaths;
  std::vector<std::string> linkLibraries;
  std::vector<std::string> linkFrameworks;
  std::map<std::string, std::string> pkgMap;
  std::map<std::string, std::string> packageNodeIds;
  std::string workspaceNodeId;
  std::string workspaceRoot;
  std::string toolchainNodeId;
  bool disableBorrowCheck = false;
  bool emitObj = false;
  bool compileOnly = false;
  bool emitDebugInfo = false;
  bool emitInterface = false;
  bool dumpDependencies = false;
  bool dumpJson = false;
  bool dumpAssignmentStats = false;
  bool dumpHandleSurfaceStats = false;
  bool dumpSemanticEvidence = false;
  bool dumpCedeObligations = false;
  bool dumpCapabilities = false;
  bool dumpTodoGoals = false;
  bool dumpConditionalFacts = false;
  bool dumpMemorySummaries = false;
  bool dumpMemoryContracts = false;
  bool dumpEncapSlice1Facts = false;
  bool dumpSemanticIndex = false;
  bool dumpSemanticContext = false;
  bool structuredDiagnostics = false;
  bool checkOnly = false;
  bool validateSemanticManifests = false;
  bool validateSemanticManifestAttestations = false;
  std::string semanticManifestProvenanceDirectory;
  bool explainJson = false;
  std::string semanticQuery;
  std::string explainCode;
  std::string queryFile;
  std::string renameTo;
  unsigned queryLine = 0;
  unsigned queryCharacter = 0;
  bool experimentalNoCapture = false;
  bool experimentalReadOnly = false;
  bool machineFailureDiagnostics = false;
  SemanticEvidenceDumpGuard semanticEvidenceGuard(dumpCedeObligations,
                                                   dumpCapabilities,
                                                   dumpTodoGoals,
                                                   dumpConditionalFacts);
  StructuredDiagnosticsDumpGuard structuredDiagnosticsGuard(
      structuredDiagnostics);
  MachineFailureDiagnosticsDumpGuard machineFailureDiagnosticsGuard(
      machineFailureDiagnostics);
  bool runTopologyEval = false;
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
    } else if (arg == "--help" || arg == "-h") {
      printHelp();
      return 0;
    } else if (arg == "--version" || arg == "-V") {
      llvm::outs() << "toka version " << TOKA_VERSION_STRING << " (Built: " << __DATE__ << " " << __TIME__ << ")\n";
      return 0;
    } else if (arg == "--check-json") {
      g_JsonDiagnostics = true;
    } else if (arg == "--diagnostics-json") {
      structuredDiagnostics = true;
      toka::DiagnosticEngine::setPrintingEnabled(false);
    } else if (arg == "--check-only") {
      checkOnly = true;
    } else if (arg == "--validate-semantic-manifests") {
      validateSemanticManifests = true;
    } else if (arg == "--validate-semantic-manifest-attestations") {
      validateSemanticManifestAttestations = true;
    } else if (arg == "--semantic-manifest-provenance-dir") {
      if (i + 1 >= argc || std::string(argv[i + 1]).empty()) {
        llvm::errs() << "--semantic-manifest-provenance-dir requires an absolute directory\n";
        return 1;
      }
      semanticManifestProvenanceDirectory = argv[++i];
    } else if (arg == "--explain" || arg == "--explain=json") {
      if (i + 1 >= argc) {
        llvm::errs() << arg << " requires a diagnostic code\n";
        return 1;
      }
      explainJson = arg == "--explain=json";
      explainCode = argv[++i];
    } else if (arg == "--semantic-index=json") {
      dumpSemanticIndex = true;
    } else if (arg == "--semantic-context=json") {
      dumpSemanticContext = true;
    } else if (arg == "--semantic-query") {
      if (i + 1 >= argc) {
        llvm::errs() << "--semantic-query requires an argument\n";
        return 1;
      }
      semanticQuery = argv[++i];
    } else if (arg == "--query-file") {
      if (i + 1 >= argc) {
        llvm::errs() << "--query-file requires an argument\n";
        return 1;
      }
      queryFile = argv[++i];
    } else if (arg == "--line") {
      if (i + 1 >= argc ||
          !parseUnsignedArgument("--line", argv[++i], queryLine))
        return 1;
    } else if (arg == "--character") {
      if (i + 1 >= argc ||
          !parseUnsignedArgument("--character", argv[++i], queryCharacter))
        return 1;
    } else if (arg == "--rename-to") {
      if (i + 1 >= argc) {
        llvm::errs() << "--rename-to requires an argument\n";
        return 1;
      }
      renameTo = argv[++i];
    } else if (arg == "--disable-borrow-check") {
      disableBorrowCheck = true;
    } else if (arg == "--dump-dependencies") {
      dumpDependencies = true;
    } else if (arg == "--dump-dependencies=json" || arg == "--dump-module-graph-json") {
      dumpDependencies = true;
      dumpJson = true;
    } else if (arg == "--dump-assignment-stats=json") {
      dumpAssignmentStats = true;
    } else if (arg == "--dump-handle-surface-stats=json") {
      dumpHandleSurfaceStats = true;
    } else if (arg == "--semantic-evidence=json" ||
               arg == "--dump-semantic-evidence=json") {
      dumpSemanticEvidence = true;
    } else if (arg == "--cede-obligations=json") {
      dumpCedeObligations = true;
    } else if (arg == "--capabilities=json") {
      dumpCapabilities = true;
    } else if (arg == "--todo-goals=json") {
      dumpTodoGoals = true;
    } else if (arg == "--conditional-facts=json") {
      dumpConditionalFacts = true;
    } else if (arg == "--dump-memory-summaries=json") {
      dumpMemorySummaries = true;
    } else if (arg == "--dump-memory-contracts=json") {
      dumpMemoryContracts = true;
    } else if (arg == "--encap-slice1-facts=json") {
      dumpEncapSlice1Facts = true;
    } else if (arg == "--experimental-memory-contracts=nocapture") {
      experimentalNoCapture = true;
    } else if (arg == "--experimental-memory-contracts=readonly") {
      experimentalReadOnly = true;
    } else if (arg.rfind("--experimental-memory-contracts=", 0) == 0) {
      llvm::errs() << "unsupported experimental memory contract: "
                   << arg.substr(std::string("--experimental-memory-contracts=").size())
                   << '\n';
      return 1;
    } else if (arg == "--topology-eval") {
      runTopologyEval = true;
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
    } else if (arg == "--pkg-node") {
      if (i + 1 >= argc) {
        llvm::errs() << "--pkg-node requires format import-prefix=opaque-node-id\n";
        return 1;
      }
      std::string mapping = argv[++i];
      size_t eqPos = mapping.find('=');
      if (eqPos == std::string::npos || eqPos == 0 || eqPos + 1 >= mapping.size()) {
        llvm::errs() << "--pkg-node requires format import-prefix=opaque-node-id\n";
        return 1;
      }
      packageNodeIds[mapping.substr(0, eqPos)] = mapping.substr(eqPos + 1);
    } else if (arg == "--workspace-node") {
      if (i + 1 >= argc) {
        llvm::errs() << "--workspace-node requires an opaque node id\n";
        return 1;
      }
      workspaceNodeId = argv[++i];
    } else if (arg == "--workspace-root") {
      if (i + 1 >= argc) {
        llvm::errs() << "--workspace-root requires a path\n";
        return 1;
      }
      workspaceRoot = argv[++i];
    } else if (arg == "--toolchain-node") {
      if (i + 1 >= argc) {
        llvm::errs() << "--toolchain-node requires an opaque node id\n";
        return 1;
      }
      toolchainNodeId = argv[++i];
    } else if (arg == "-v" || arg == "--verbose") {
      verboseMode = true;
    } else if (arg == "-o") {
      if (i + 1 < argc) {
        outputFile = argv[++i];
        if ((outputFile.length() > 2 && outputFile.substr(outputFile.length() - 2) == ".o") ||
            (outputFile.length() > 4 && outputFile.substr(outputFile.length() - 4) == ".obj")) {
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
    } else if (arg == "-g") {
      emitDebugInfo = true;
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
    } else if (arg == "--link-search") {
      if (i + 1 >= argc || std::string(argv[i + 1]).empty()) {
        llvm::errs() << "--link-search requires a non-empty path\n";
        return 1;
      }
      linkSearchPaths.emplace_back(argv[++i]);
    } else if (arg == "--link-lib") {
      if (i + 1 >= argc || !isSafeLinkLibraryName(argv[i + 1])) {
        llvm::errs() << "--link-lib requires a library name containing only letters, digits, '_', '+', '.', or '-'\n";
        return 1;
      }
      linkLibraries.emplace_back(argv[++i]);
    } else if (arg == "--link-framework") {
      if (i + 1 >= argc || !isSafeFrameworkName(argv[i + 1])) {
        llvm::errs() << "--link-framework requires a framework name containing only letters, digits, or '_'\n";
        return 1;
      }
      linkFrameworks.emplace_back(argv[++i]);
    } else if (arg.rfind("-", 0) == 0) {
      llvm::errs() << "error: unknown option '" << arg
                   << "' (use --help for available options)\n";
      return 1;
    } else {
      if ((arg.length() > 2 &&
           (arg.substr(arg.length() - 2) == ".o" || arg.substr(arg.length() - 2) == ".a")) ||
          (arg.length() > 4 && arg.substr(arg.length() - 4) == ".obj")) {
        objectFiles.push_back(arg);
      } else {
        sourceFiles.push_back(arg);
      }
    }
  }

  if (compileOnly) {
    emitInterface = true;
  }
  if (validateSemanticManifestAttestations &&
      semanticManifestProvenanceDirectory.empty()) {
    llvm::errs() << "--validate-semantic-manifest-attestations requires "
                 << "--semantic-manifest-provenance-dir\n";
    return 1;
  }
  const char *configuredBuildDir = std::getenv("TOKA_BUILD_DIR");
  bool emitTrustedMemoryEvidence =
      compileOnly && emitInterface && configuredBuildDir &&
      configuredBuildDir[0] != '\0';

  if (structuredDiagnostics &&
      (g_JsonDiagnostics || dumpDependencies || dumpAssignmentStats ||
       dumpHandleSurfaceStats || dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities || dumpTodoGoals || dumpConditionalFacts || dumpMemorySummaries ||
       dumpMemoryContracts || dumpEncapSlice1Facts || dumpSemanticIndex || dumpSemanticContext ||
       !semanticQuery.empty() || runTopologyEval || !explainCode.empty())) {
    llvm::errs() << "--diagnostics-json cannot be combined with another "
                    "JSON, semantic, or evaluation output mode\n";
    structuredDiagnostics = false;
    return 1;
  }

  if (dumpSemanticEvidence &&
      (dumpCapabilities || dumpTodoGoals || dumpConditionalFacts || dumpAssignmentStats || dumpHandleSurfaceStats || dumpDependencies ||
       runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--semantic-evidence=json cannot be combined with "
                    "another JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpCedeObligations &&
      (dumpSemanticEvidence || dumpCapabilities || dumpTodoGoals || dumpConditionalFacts || dumpAssignmentStats || dumpHandleSurfaceStats ||
       dumpDependencies || runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--cede-obligations=json cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpCapabilities &&
      (dumpSemanticEvidence || dumpCedeObligations || dumpTodoGoals || dumpConditionalFacts || dumpAssignmentStats ||
       dumpHandleSurfaceStats || dumpDependencies || runTopologyEval ||
       g_JsonDiagnostics)) {
    llvm::errs() << "--capabilities=json cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpTodoGoals &&
      (dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities || dumpConditionalFacts ||
       dumpAssignmentStats || dumpHandleSurfaceStats || dumpDependencies ||
       runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--todo-goals=json cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpConditionalFacts &&
      (dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities ||
       dumpTodoGoals || dumpAssignmentStats || dumpHandleSurfaceStats ||
       dumpDependencies || runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--conditional-facts=json cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpMemorySummaries &&
      (dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities || dumpTodoGoals || dumpConditionalFacts || dumpAssignmentStats || dumpHandleSurfaceStats ||
       dumpDependencies || runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--dump-memory-summaries=json cannot be combined with "
                    "another JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpMemoryContracts &&
      (dumpMemorySummaries || dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities || dumpTodoGoals || dumpConditionalFacts || dumpAssignmentStats ||
       dumpHandleSurfaceStats || dumpDependencies || runTopologyEval ||
       g_JsonDiagnostics)) {
    llvm::errs() << "--dump-memory-contracts=json cannot be combined with "
                    "another JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpEncapSlice1Facts &&
      (dumpMemoryContracts || dumpMemorySummaries || dumpSemanticEvidence ||
       dumpCedeObligations || dumpCapabilities || dumpTodoGoals ||
       dumpConditionalFacts || dumpAssignmentStats || dumpHandleSurfaceStats ||
       dumpDependencies || runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "--encap-slice1-facts=json cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if ((dumpSemanticIndex || dumpSemanticContext || !semanticQuery.empty()) &&
      (dumpMemoryContracts || dumpMemorySummaries || dumpSemanticEvidence || dumpCedeObligations || dumpCapabilities || dumpTodoGoals || dumpConditionalFacts ||
       dumpAssignmentStats || dumpHandleSurfaceStats || dumpDependencies ||
       runTopologyEval || g_JsonDiagnostics)) {
    llvm::errs() << "semantic index output cannot be combined with another "
                    "JSON or evaluation output mode\n";
    return 1;
  }
  if (dumpSemanticIndex && !semanticQuery.empty()) {
    llvm::errs() << "--semantic-index=json cannot be combined with "
                    "--semantic-query\n";
    return 1;
  }
  if (dumpSemanticContext && (dumpSemanticIndex || !semanticQuery.empty())) {
    llvm::errs() << "--semantic-context=json cannot be combined with another "
                    "semantic output mode\n";
    return 1;
  }
  toka::SemanticEvidence::enable(dumpSemanticEvidence || dumpCedeObligations ||
                                 dumpCapabilities || dumpTodoGoals ||
                                 dumpConditionalFacts);

  if (!explainCode.empty()) {
    auto explanation = toka::DiagnosticEngine::explain(explainCode);
    if (!explanation) {
      if (explainJson) {
        llvm::outs() << llvm::json::Object{
                            {"schema", "toka.command-error"},
                            {"version", 1},
                            {"success", false},
                            {"command", "explain"},
                            {"kind", "unknown-diagnostic-code"},
                            {"requested", explainCode}}
                     << '\n';
      } else {
        llvm::errs() << "error: unknown diagnostic code '" << explainCode << "'\n";
      }
      return 1;
    }
    if (explainJson) {
      llvm::outs() << llvm::json::Object{
                          {"schema", "toka.diagnostic-explanation"},
                          {"version", 1},
                          {"code", explanation->Code},
                          {"id", explanation->ID},
                          {"severity", toka::DiagnosticEngine::levelName(
                                           explanation->Level)},
                          {"messageTemplate", explanation->MessageTemplate},
                          {"guidance", explanation->Guidance}}
                   << '\n';
    } else {
      llvm::outs() << explanation->Code << " ("
                   << toka::DiagnosticEngine::levelName(explanation->Level)
                   << ", " << explanation->ID << ")\n"
                   << explanation->MessageTemplate << "\n\n"
                   << explanation->Guidance << '\n';
    }
    return 0;
  }

  if (runTopologyEval) {
    std::vector<std::string> testFiles;
    if (std::filesystem::exists("tests/pass")) {
      for (const auto &entry : std::filesystem::recursive_directory_iterator("tests/pass")) {
        if (entry.is_regular_file() && entry.path().extension() == ".tk") {
          testFiles.push_back(entry.path().string());
        }
      }
    }
    std::sort(testFiles.begin(), testFiles.end());

    toka::SourceManager sm;
    toka::DiagnosticEngine::init(sm);

    toka::runTopologyCacheEvaluation(testFiles, searchPaths, pkgMap);
    return 0;
  }

  if (sourceFiles.empty()) {
    llvm::errs() << "error: no input files (use --help for usage)\n";
    return 1;
  }

  machineFailureDiagnostics =
      dumpSemanticIndex || dumpSemanticContext || !semanticQuery.empty();
  if (machineFailureDiagnostics)
    toka::DiagnosticEngine::setPrintingEnabled(false);

  std::string resolvedTargetTriple = llvm::sys::getDefaultTargetTriple();
  if (!cliTargetTriple.empty()) {
    resolvedTargetTriple = cliTargetTriple;
  } else if (const char* envTriple = std::getenv("TOKA_TARGET_TRIPLE")) {
    resolvedTargetTriple = envTriple;
  }
  toka::Parser::TargetTriple = resolvedTargetTriple;
  if (toolchainNodeId.empty()) {
    toolchainNodeId = std::string("toolchain-v1-") + TOKA_COMPILER_INTERFACE_VERSION;
  }

  toka::SourceManager sm;
  toka::DiagnosticEngine::init(sm);
  const char *assignmentStatsEnv = std::getenv("TOKA_ASSIGNMENT_STATS");
  bool assignmentStatsFromEnv =
      assignmentStatsEnv && assignmentStatsEnv[0] != '\0' &&
      std::string(assignmentStatsEnv) != "0";
  toka::enableAssignmentStats(dumpAssignmentStats || assignmentStatsFromEnv);
  const char *handleSurfaceStatsEnv = std::getenv("TOKA_HANDLE_SURFACE_STATS");
  bool handleSurfaceStatsFromEnv =
      handleSurfaceStatsEnv && handleSurfaceStatsEnv[0] != '\0' &&
      std::string(handleSurfaceStatsEnv) != "0";
  toka::enableHandleSurfaceStats(dumpHandleSurfaceStats ||
                                 handleSurfaceStatsFromEnv);
  const char *profileEnv = std::getenv("TOKA_PROFILE");
  TokaProfile profile(profileEnv && profileEnv[0] != '\0' && std::string(profileEnv) != "0");

  std::vector<std::unique_ptr<toka::Module>> astModules;
  bool preferSource = !compileOnly;
  toka::ModuleResolver resolver(sm, searchPaths, pkgMap, preferSource,
                                trustedSystemRoots, packageNodeIds,
                                workspaceNodeId, workspaceRoot,
                                toolchainNodeId,
                                validateSemanticManifestAttestations,
                                semanticManifestProvenanceDirectory);
  resolver.setProvidedObjects(objectFiles);
  bool parseSuccess = true;
  for (size_t i = 0; i < sourceFiles.size(); ++i) {
    if (!resolver.resolveAndParse(sourceFiles[i], astModules, "", i > 0)) {
      parseSuccess = false;
    }
  }
  profile.mark("parse_resolve");

  const std::vector<std::string> &roots = resolver.getRoots();
  if (emitInterface) {
    std::set<std::string> interfacePaths;
    for (const auto &r : roots) {
      std::string outPath = toka::PathUtils::canonicalize(getFinalInterfacePath(outputFile, r));
      if (interfacePaths.count(outPath)) {
        llvm::errs() << "error: cannot emit multiple interfaces to a single output path: " << outPath << "\n";
        return 1;
      }
      interfacePaths.insert(outPath);
    }
  }

  if (dumpDependencies) {
    if (!parseSuccess || toka::DiagnosticEngine::hasErrors()) {
      return 1;
    }
    if (dumpJson) {

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
              case toka::TKICacheStatus::MissingSourcePath: return "MissingSourcePath";
              case toka::TKICacheStatus::MissingIdentitySchema: return "MissingIdentitySchema";
              case toka::TKICacheStatus::MissingModuleIdentity: return "MissingModuleIdentity";
              case toka::TKICacheStatus::InterfaceIdentityMismatch: return "InterfaceIdentityMismatch";
              case toka::TKICacheStatus::CompilerVersionMismatch: return "CompilerVersionMismatch";
              case toka::TKICacheStatus::FormatVersionMismatch: return "FormatVersionMismatch";
              case toka::TKICacheStatus::TargetTripleMismatch: return "TargetTripleMismatch";
              case toka::TKICacheStatus::SourceHashMismatch: return "SourceHashMismatch";
          }
          return "Unknown";
      };

      llvm::outs() << "{\n";
      llvm::outs() << "  \"manifest_version\": \"1.0.0\",\n";
      llvm::outs() << "  \"roots\": [";
      bool firstRoot = true;
      for (const auto &r : roots) {
        if (!firstRoot) {
          llvm::outs() << ", ";
        }
        firstRoot = false;
        llvm::outs() << "\"" << escapeJsonString(r) << "\"";
      }
      llvm::outs() << "],\n";
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
        llvm::outs() << "      \"source_hash\": \"" << escapeJsonString(info.SourceHash) << "\",\n";
        llvm::outs() << "      \"content_hash\": \"" << escapeJsonString(info.ContentHash) << "\",\n";
        llvm::outs() << "      \"memory_evidence_status\": \""
                     << escapeJsonString(info.MemoryEvidenceStatus) << "\",\n";
        llvm::outs() << "      \"memory_evidence_reason\": \""
                     << escapeJsonString(info.MemoryEvidenceReason) << "\",\n";
        llvm::outs() << "      \"semantic_manifest_attestation_status\": \""
                     << escapeJsonString(info.SemanticManifestAttestationStatus)
                     << "\",\n";
        llvm::outs() << "      \"semantic_manifest_attestation_reason\": \""
                     << escapeJsonString(info.SemanticManifestAttestationReason)
                     << "\",\n";
        llvm::outs() << "      \"shadow_coordinate\": {\n";
        llvm::outs() << "        \"status\": \""
                     << (info.ShadowCoordinate.Known ? "known" : "unknown") << "\",\n";
        llvm::outs() << "        \"crate_id\": \""
                     << escapeJsonString(info.ShadowCoordinate.CrateId) << "\",\n";
        llvm::outs() << "        \"logical_module_path\": \""
                     << escapeJsonString(info.ShadowCoordinate.LogicalModulePath) << "\",\n";
        llvm::outs() << "        \"origin\": \""
                     << escapeJsonString(info.ShadowCoordinate.Origin) << "\",\n";
        llvm::outs() << "        \"reason\": \""
                     << escapeJsonString(info.ShadowCoordinate.Reason) << "\"\n";
        llvm::outs() << "      },\n";

        bool isRoot = (std::find(roots.begin(), roots.end(), info.CanonicalPath) != roots.end());
        std::string interfaceOut = "";
        std::string objectOut = "";
        std::string executableOut = "";

        if (isRoot) {
            if (emitInterface) {
                interfaceOut = toka::PathUtils::canonicalize(getFinalInterfacePath(outputFile, info.CanonicalPath));
            }
            if (emitObj) {
                if (compileOnly) {
                    objectOut = outputFile.empty() ? "" : toka::PathUtils::canonicalize(outputFile);
                } else {
                    executableOut = outputFile.empty() ? toka::PathUtils::canonicalize("a.out") : toka::PathUtils::canonicalize(outputFile);
                }
            }
        }

        llvm::outs() << "      \"outputs\": {\n";
        llvm::outs() << "        \"interface\": \"" << escapeJsonString(interfaceOut) << "\",\n";
        llvm::outs() << "        \"object\": \"" << escapeJsonString(objectOut) << "\",\n";
        llvm::outs() << "        \"executable\": \"" << escapeJsonString(executableOut) << "\"\n";
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
      profile.mark("dependency_dump_json");
      profile.finish("tokac");
      return 0;
    }

    for (const auto &pair : resolver.getDependencies()) {
      llvm::outs() << pair.first << ":";
      for (const auto &dep : pair.second) {
        llvm::outs() << " " << dep;
      }
      llvm::outs() << "\n";
    }
    profile.mark("dependency_dump");
    profile.finish("tokac");
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
  profile.mark("sema_declare");

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
  profile.mark("sema_check");

  if (validateSemanticManifests &&
      !validateSemanticManifestImports(astModules)) {
    llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted due to semantic manifest validation errors.\n";
    return 1;
  }
  if (validateSemanticManifests)
    profile.mark("semantic_manifest_validation");

  std::vector<SemanticManifestAttestationLinkObligation>
      semanticManifestAttestationObligations;
  if (validateSemanticManifestAttestations &&
      !validateSemanticManifestAttestationImports(
          astModules, semanticManifestProvenanceDirectory,
          semanticManifestAttestationObligations)) {
    llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted due to semantic manifest attestation errors.\n";
    return 1;
  }
  if (validateSemanticManifestAttestations) {
    if (!checkOnly && !semanticManifestAttestationObligations.empty() &&
        (compileOnly || !emitObj)) {
      for (const auto &obligation : semanticManifestAttestationObligations) {
        toka::DiagnosticEngine::report(
            obligation.Loc, toka::DiagID::ERR_SEMANTIC_MANIFEST_ATTESTATION,
            obligation.InterfacePath,
            "a bodyless Outcome provider may be consumed only by the same compiler invocation that performs final linking");
      }
      llvm::errs() << "\033[1;31m[FAILED]\033[0m Compilation aborted because P2 link obligations cannot survive this output mode.\n";
      return 1;
    }
    profile.mark("semantic_manifest_attestation_validation");
  }

  if (dumpEncapSlice1Facts) {
    sema.dumpEncapSlice1FactsJSON(std::cout);
    std::cout << '\n';
    profile.finish("tokac");
    return 0;
  }

  if (dumpSemanticIndex || dumpSemanticContext || !semanticQuery.empty()) {
    std::vector<toka::Module *> modules;
    modules.reserve(astModules.size());
    for (const auto &ast : astModules)
      modules.push_back(ast.get());
    toka::SemanticIndex index = toka::SemanticIndex::build(modules, sm);
    llvm::json::Value output = dumpSemanticIndex
                                   ? index.toJSON()
                                   : index.queryJSON(
                                         dumpSemanticContext ? "context"
                                                             : semanticQuery,
                                         toka::PathUtils::canonicalize(
                                             queryFile.empty()
                                                 ? sourceFiles.front()
                                                 : queryFile),
                                         queryLine, queryCharacter, renameTo);
    llvm::outs() << output << '\n';
    profile.mark("semantic_index");
    profile.finish("tokac");
    return 0;
  }

  if (checkOnly) {
    profile.finish("tokac");
    return 0;
  }

  std::vector<PendingSemanticManifestAttestation>
      pendingSemanticManifestAttestations;
  if (emitInterface) {
    std::vector<toka::Module *> resolvedModules;
    resolvedModules.reserve(astModules.size());
    for (const auto &ast : astModules) {
      resolvedModules.push_back(ast.get());
    }
    for (const auto &ast : astModules) {
      std::string canonicalSourcePath =
          toka::PathUtils::canonicalize(ast->SourcePath);
      std::string canonicalResolvedPath =
          toka::PathUtils::canonicalize(ast->ResolvedPath);
      bool isRoot =
          (std::find(roots.begin(), roots.end(), canonicalSourcePath) !=
               roots.end() ||
           std::find(roots.begin(), roots.end(), canonicalResolvedPath) !=
               roots.end());
      if (isRoot) {
        std::string outPath = getFinalInterfacePath(outputFile, ast->SourcePath);

        if (verboseMode) llvm::errs() << "Exporting TKI Interface to " << outPath << "...\n";

        {
          std::error_code EC;
          llvm::raw_fd_ostream os(outPath, EC, llvm::sys::fs::OF_None);
          if (EC) {
            llvm::errs() << "Error writing TKI file " << outPath << ": " << EC.message() << "\n";
            return 1;
          }

          toka::TKIExporter exporter(os);
          exporter.setRetainOutcomeBodies(
              semanticManifestProvenanceDirectory.empty());
          exporter.exportModule(*ast);
        }

        if (!ast->CanonicalOutcomeDeclarationWitnesses.empty()) {
          const bool emitP2Attestation =
              !semanticManifestProvenanceDirectory.empty();
          if (!ast->ShadowCoordinateKnown) {
            if (emitP2Attestation) {
              llvm::errs() << "Semantic manifest attestation requires a "
                           << "resolver-known module coordinate for " << outPath
                           << "\n";
              return 1;
            }
            if (verboseMode)
              llvm::errs() << "Semantic manifest omitted for " << outPath
                           << ": admitted CDW1 record lacks a module coordinate\n";
            continue;
          }
          if (emitP2Attestation &&
              (!compileOnly || !emitObj || ast->IsInterface)) {
            llvm::errs() << "Semantic manifest attestation requires a source "
                         << "provider compiled with -c and --emit-interface: "
                         << outPath << "\n";
            return 1;
          }
          std::vector<std::string> closureErrors;
          auto closure = toka::SemanticDependencyClosure::calculate(
              *ast, resolvedModules, closureErrors,
              emitP2Attestation ? std::set<const toka::Module *>{ast.get()}
                                : std::set<const toka::Module *>{});
          if (!closure) {
            if (verboseMode) {
              llvm::errs() << "Semantic manifest omitted for " << outPath;
              for (const std::string &error : closureErrors)
                llvm::errs() << ": " << error;
              llvm::errs() << "\n";
            }
            continue;
          }
          std::string interfaceContent;
          if (!readExactFile(outPath, interfaceContent)) {
            llvm::errs() << "Error reading emitted TKI file " << outPath
                         << " for semantic manifest\n";
            return 1;
          }
          if (emitP2Attestation) {
            toka::SemanticManifestAttestationInput attestation;
            attestation.TargetTriple = toka::Parser::TargetTriple;
            attestation.Module = {ast->ShadowCrateId,
                                  ast->ShadowLogicalModulePath};
            attestation.InterfaceContent = std::move(interfaceContent);
            attestation.SemanticDependencyClosureDigest = std::move(*closure);
            attestation.CDW1Records = ast->CanonicalOutcomeDeclarationWitnesses;
            PendingSemanticManifestAttestation pending;
            pending.InterfacePath = outPath;
            std::vector<std::string> attestationErrors;
            if (!toka::SemanticManifestAttestation::prepare(
                    attestation, pending.Prepared, attestationErrors)) {
              llvm::errs() << "Error preparing semantic manifest attestation "
                           << outPath;
              for (const std::string &error : attestationErrors)
                llvm::errs() << ": " << error;
              llvm::errs() << "\n";
              return 1;
            }
            pendingSemanticManifestAttestations.push_back(std::move(pending));
          } else {
            toka::SemanticManifestEnvelopeInput manifest;
            manifest.TargetTriple = toka::Parser::TargetTriple;
            manifest.Module = {ast->ShadowCrateId,
                               ast->ShadowLogicalModulePath};
            manifest.InterfaceContent = std::move(interfaceContent);
            manifest.SemanticDependencyClosureDigest = std::move(*closure);
            manifest.CDW1Records = ast->CanonicalOutcomeDeclarationWitnesses;
            std::vector<std::string> manifestErrors;
            const std::string manifestPath =
                toka::SemanticManifestEnvelope::sidecarPath(outPath);
            if (!toka::SemanticManifestEnvelope::write(manifestPath, manifest,
                                                        manifestErrors)) {
              llvm::errs() << "Error writing semantic manifest " << manifestPath;
              for (const std::string &error : manifestErrors)
                llvm::errs() << ": " << error;
              llvm::errs() << "\n";
              return 1;
            }
          }
        }
      }
    }

  }
  profile.mark("interface_export");

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
  if (emitDebugInfo)
    codegen.enableDebugInfo(sourceFiles.front(),
                            optLevel != llvm::OptimizationLevel::O0);
  // ----------------------------------------------------------------------------

  std::unique_ptr<toka::Module> genericModule = sema.extractGenericRegistry();
  std::vector<toka::Module *> summaryModules;
  summaryModules.reserve(astModules.size() + (genericModule ? 1 : 0));
  for (const auto &ast : astModules)
    summaryModules.push_back(ast.get());
  if (genericModule)
    summaryModules.push_back(genericModule.get());

  bool activateTrustedEvidence = experimentalNoCapture || experimentalReadOnly;
  toka::MemorySummaryAnalysis::run(summaryModules, !disableBorrowCheck,
                                   activateTrustedEvidence);
  std::vector<std::string> memorySummaryErrors;
  if (!toka::MemorySummaryAnalysis::verify(
          summaryModules, !disableBorrowCheck, memorySummaryErrors)) {
    for (const auto &error : memorySummaryErrors)
      llvm::errs() << "Memory summary verification error: " << error << '\n';
    return 1;
  }
  profile.mark("codegen_setup");

  if (verboseMode) fprintf(stderr, "Pass 1: Discovery (Registration)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.discover(*ast);
  }
  if (genericModule) codegen.discover(*genericModule);
  profile.mark("codegen_discover");

  if (verboseMode) fprintf(stderr, "Pass 2: Resolution (Signatures)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.resolveSignatures(*ast);
  }
  if (genericModule) codegen.resolveSignatures(*genericModule);
  profile.mark("codegen_signatures");

  if (verboseMode) fprintf(stderr, "Pass 3: Generation (Emission)...\n");
  fflush(stderr);
  for (const auto &ast : astModules) {
    codegen.generate(*ast);
  }
  if (genericModule) codegen.generate(*genericModule);

  codegen.finalizeGlobals();
  codegen.finalizeDebugInfo();
  profile.mark("codegen_generate");

  // The object does not exist until native emission, but the payload marker
  // must be retained in IR before optimization so the final sidecar can bind
  // its exact object bytes to the checked Outcome records.
  for (const auto &pending : pendingSemanticManifestAttestations) {
    std::vector<std::string> attestationErrors;
    if (!toka::SemanticManifestAttestation::bindObjectMarker(
            *codegen.getModule(), pending.Prepared, attestationErrors)) {
      llvm::errs() << "Error binding semantic manifest attestation marker "
                   << pending.InterfacePath;
      for (const std::string &error : attestationErrors)
        llvm::errs() << ": " << error;
      llvm::errs() << "\n";
      return 1;
    }
  }

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
  // codegen.getModule()->print(llvm::errs(), nullptr);
  if (llvm::verifyModule(*codegen.getModule(), &llvm::errs())) {
    llvm::errs() << "Fatal Error: LLVM IR Verification Failed!\n";
    return 1;
  }
  memorySummaryErrors.clear();
  if (!toka::MemorySummaryAnalysis::verifyIR(
          summaryModules, *codegen.getModule(), memorySummaryErrors)) {
    for (const auto &error : memorySummaryErrors)
      llvm::errs() << "Memory summary IR verification error: " << error << '\n';
    return 1;
  }
  if (emitTrustedMemoryEvidence) {
    for (const auto &ast : astModules) {
      std::string canonicalPath =
          toka::PathUtils::canonicalize(ast->SourcePath);
      bool isRoot =
          std::find(roots.begin(), roots.end(), canonicalPath) != roots.end();
      if (!isRoot || ast->IsInterface)
        continue;
      memorySummaryErrors.clear();
      if (!toka::MemoryEvidenceCache::bindObject(
              *codegen.getModule(), *ast,
              toka::MemoryEvidenceCache::sourceHash(ast->SourcePath),
              toka::Parser::TargetTriple, memorySummaryErrors)) {
        for (const auto &error : memorySummaryErrors)
          llvm::errs() << "Memory evidence binding error: " << error << '\n';
        return 1;
      }
    }
  }
  toka::MemoryContractShadow memoryContracts =
      toka::MemoryContractShadow::analyze(
          summaryModules, *codegen.getModule(), !disableBorrowCheck);
  memorySummaryErrors.clear();
  if (!memoryContracts.verify(summaryModules, *codegen.getModule(),
                              !disableBorrowCheck, memorySummaryErrors)) {
    for (const auto &error : memorySummaryErrors)
      llvm::errs() << "Memory contract shadow verification error: " << error
                   << '\n';
    return 1;
  }
  if (experimentalNoCapture)
    memoryContracts.emitExperimental(*codegen.getModule(),
                                     toka::MemoryContractKind::NoCapture);
  if (experimentalReadOnly)
    memoryContracts.emitExperimental(*codegen.getModule(),
                                     toka::MemoryContractKind::ReadOnly);
  memorySummaryErrors.clear();
  if (!memoryContracts.verifyExperimental(
          *codegen.getModule(), toka::MemoryContractKind::NoCapture,
          experimentalNoCapture, memorySummaryErrors) ||
      !memoryContracts.verifyExperimental(
          *codegen.getModule(), toka::MemoryContractKind::ReadOnly,
          experimentalReadOnly, memorySummaryErrors)) {
    for (const auto &error : memorySummaryErrors)
      llvm::errs() << "Experimental nocapture verification error: " << error
                   << '\n';
    return 1;
  }
  if ((experimentalNoCapture || experimentalReadOnly) &&
      llvm::verifyModule(*codegen.getModule(), &llvm::errs())) {
    llvm::errs() << "Fatal Error: Experimental nocapture IR verification failed!\n";
    return 1;
  }
  if (dumpMemorySummaries)
    toka::MemorySummaryAnalysis::dumpJSON(summaryModules, std::cout);
  if (dumpMemoryContracts)
    memoryContracts.dumpJSON(std::cout);
  profile.mark("verify");

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
  profile.mark("optimize");

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
    if (emitTrustedMemoryEvidence) {
      for (const auto &ast : astModules) {
        std::string canonicalPath =
            toka::PathUtils::canonicalize(ast->SourcePath);
        bool isRoot =
            std::find(roots.begin(), roots.end(), canonicalPath) != roots.end();
        if (!isRoot || ast->IsInterface)
          continue;
        std::string interfacePath = toka::PathUtils::canonicalize(
            getFinalInterfacePath(outputFile, ast->SourcePath));
        std::vector<std::string> evidenceErrors;
        if (!toka::MemoryEvidenceCache::write(
                toka::MemoryEvidenceCache::sidecarPath(interfacePath),
                objFile, *ast,
                toka::MemoryEvidenceCache::sourceHash(ast->SourcePath),
                toka::Parser::TargetTriple, evidenceErrors)) {
          for (const auto &evidenceError : evidenceErrors)
            llvm::errs() << "Memory evidence export error: "
                         << evidenceError << '\n';
          return 1;
        }
      }
    }
    for (const auto &pending : pendingSemanticManifestAttestations) {
      std::vector<std::string> attestationErrors;
      const std::string manifestPath =
          toka::SemanticManifestEnvelope::sidecarPath(pending.InterfacePath);
      if (!toka::SemanticManifestAttestation::write(
              manifestPath, objFile, pending.Prepared,
              semanticManifestProvenanceDirectory, attestationErrors)) {
        llvm::errs() << "Error writing semantic manifest attestation "
                     << manifestPath;
        for (const std::string &error : attestationErrors)
          llvm::errs() << ": " << error;
        llvm::errs() << "\n";
        return 1;
      }
    }
    if (verboseMode) fprintf(stderr, "Object file emitted successfully.\n");
    profile.mark("emit_object");

    if (!compileOnly) {
      if (verboseMode) fprintf(stderr, "Linking executable (internal LLD): %s\n", finalOutput.c_str());
      fflush(stderr);

      llvm::Triple triple(toka::Parser::TargetTriple);
      std::string rtExtension = (triple.isOSWindows()) ? ".obj" : ".o";
      std::string rtFileName = "toka_rt" + rtExtension;
      if (triple.isOSWASI() || triple.getArch() == llvm::Triple::wasm32 || triple.getArch() == llvm::Triple::wasm64) {
          rtFileName = "toka_rt.wasm.o";
      }
      bool hasRt = false;
      for (const auto &obj : objectFiles) {
        if (obj.find("toka_rt") != std::string::npos) {
          hasRt = true;
          break;
        }
      }
      std::string tokaRtPath;

      // 1. Prioritize local relative paths to ensure local development overrides global installations
      if (!hasRt) {
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

      // 2. A CMake build keeps the runtime beside its executables under
      //    build/lib even though that directory does not contain source
      //    library files. Discover it directly for an out-of-tree SDK.
      if (!hasRt && tokaRtPath.empty() && !executable.empty()) {
        std::filesystem::path binDir = std::filesystem::path(executable).parent_path();
        std::filesystem::path buildRuntime = binDir.parent_path() / "lib" / "sys" / rtFileName;
        if (std::filesystem::exists(buildRuntime)) {
          tokaRtPath = buildRuntime.string();
        }
      }

      // 3. Search in searchPaths (including TOKA_LIB and -I)
      if (!hasRt && tokaRtPath.empty()) {
        for (const auto &p : searchPaths) {
          llvm::SmallString<128> testPath(p);
          llvm::sys::path::append(testPath, "sys", rtFileName);
          if (llvm::sys::fs::exists(testPath)) {
            tokaRtPath = std::string(testPath.str());
            break;
          }
        }
      }

      // 4. Absolute fallback
      if (!hasRt && tokaRtPath.empty()) {
        llvm::SmallString<128> fallbackPath("/usr/local/lib/toka");
        llvm::sys::path::append(fallbackPath, "sys", rtFileName);
        if (llvm::sys::fs::exists(fallbackPath)) {
          tokaRtPath = std::string(fallbackPath.str());
        }
      }

      if (!hasRt && tokaRtPath.empty()) {
        llvm::errs() << "\033[1;31m[FAILED]\033[0m Core runtime '" << rtFileName << "' not found in search paths. Please ensure TOKA_LIB is set correctly.\n";
        return 1;
      }

      // Convert all backslashes to forward slashes to prevent escape sequences in LLD / shells
      for (char &c : tokaRtPath) {
        if (c == '\\') c = '/';
      }

      if (!hasRt) {
        objectFiles.push_back(tokaRtPath);
      }

      if (!validateSemanticManifestAttestationLinkObligations(
              semanticManifestAttestationObligations, objectFiles,
              semanticManifestProvenanceDirectory)) {
        llvm::errs() << "\033[1;31m[FAILED]\033[0m Linking aborted due to semantic manifest attestation errors.\n";
        return 1;
      }
      if (!linkWithLLD(objFile, objectFiles, linkSearchPaths, linkLibraries, linkFrameworks, finalOutput)) {
        llvm::errs() << "Linker error: LLD failed\n";
        return 1;
      }
#ifdef __APPLE__
      if (emitDebugInfo) {
        auto dsymutil = llvm::sys::findProgramByName("dsymutil");
        if (!dsymutil) {
          llvm::errs() << "Debug info error: dsymutil was not found\n";
          return 1;
        }
        std::vector<llvm::StringRef> args = {*dsymutil, finalOutput};
        if (llvm::sys::ExecuteAndWait(*dsymutil, args) != 0) {
          llvm::errs() << "Debug info error: dsymutil failed\n";
          return 1;
        }
      }
      // macOS requires ad-hoc signing for binaries linked with LLD to avoid immediately crashing with Trace/BPT trap: 5
      std::string signCmd = "codesign -s - \"" + finalOutput + "\" 2>/dev/null";
      (void)std::system(signCmd.c_str());
#endif
      std::remove(objFile.c_str());
      profile.mark("link");
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
    profile.mark("write_ir");
  }

  profile.finish("tokac");
  if (toka::assignmentStatsEnabled()) {
    toka::dumpAssignmentStatsJson(llvm::outs(), astModules.size());
  }
  if (toka::handleSurfaceStatsEnabled()) {
    toka::dumpHandleSurfaceStatsJson(llvm::outs(), astModules.size());
  }

  return 0;
}
