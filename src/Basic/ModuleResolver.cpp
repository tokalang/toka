#include "toka/ModuleResolver.h"
#include "toka/MemoryEvidence.h"
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/DiagnosticEngine.h"
#include "toka/PathUtils.h"
#include "toka/InterfaceVersion.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <sstream>

namespace toka {

static std::string calculateFNV1a(const std::string &str);

ModuleResolver::ModuleResolver(SourceManager &sm,
                               std::vector<std::string> searchPaths,
                               std::map<std::string, std::string> pkgMap,
                               bool preferSource,
                               std::vector<std::string> trustedSystemRoots)
    : m_SourceManager(sm), m_SearchPaths(std::move(searchPaths)),
      m_PkgMap(std::move(pkgMap)), m_PreferSource(preferSource) {
    for (const auto &root : trustedSystemRoots) {
        m_TrustedSystemRoots.push_back(PathUtils::canonicalize(root));
    }
    const char *useBuildCache = std::getenv("TOKA_USE_LIB_CACHE");
    m_UseBuildCache = useBuildCache && std::string(useBuildCache) == "1";
}

static bool isWithinRoot(const std::string &path,
                         const std::vector<std::string> &roots) {
    std::string canonical = PathUtils::canonicalize(path);
    for (std::string root : roots) {
        while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) {
            root.pop_back();
        }
        if (canonical == root ||
            (canonical.size() > root.size() &&
             canonical.compare(0, root.size(), root) == 0 &&
             (canonical[root.size()] == '/' || canonical[root.size()] == '\\'))) {
            return true;
        }
    }
    return false;
}

bool ModuleResolver::resolveAndParse(const std::string &rawFilename,
                                     std::vector<std::unique_ptr<Module>> &astModules,
                                     const std::string &overrideSourceCode,
                                     bool accumulate) {
    if (!accumulate) {
        m_Visited.clear();
        m_ResolutionRecords.clear();
        m_Dependencies.clear();
        m_Roots.clear();
    }
    m_RecursionStack.clear();
    std::string actualPath;
    if (overrideSourceCode.empty()) {
        actualPath = resolveSourcePath(rawFilename, m_SearchPaths);
    } else {
        actualPath = PathUtils::normalize(rawFilename);
    }
    std::string initialCanonical;
    if (!actualPath.empty()) {
        initialCanonical = PathUtils::canonicalize(actualPath);
        if (std::find(m_Roots.begin(), m_Roots.end(), initialCanonical) == m_Roots.end()) {
            m_Roots.push_back(initialCanonical);
        }
    }
    bool ok = parseRecursive(rawFilename, astModules, overrideSourceCode, &actualPath);
    if (!actualPath.empty()) {
        std::string finalCanonical = PathUtils::canonicalize(actualPath);
        if (finalCanonical != initialCanonical) {
            auto it = std::find(m_Roots.begin(), m_Roots.end(), initialCanonical);
            if (it != m_Roots.end()) {
                *it = finalCanonical;
            }
        }
    }
    return ok;
}

std::string ModuleResolver::resolveSourcePath(const std::string &rawFilename,
                                             const std::vector<std::string> &activeSearchPaths) {
  std::string filename = PathUtils::normalize(rawFilename);

  auto fileExists = [](const std::string &p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
  };

  auto getBasename = [](const std::string &path) -> std::string {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string::npos) return path;
    return path.substr(lastSlash + 1);
  };

  bool isStdOrCore = (filename.rfind("std/", 0) == 0) || (filename.rfind("std\\", 0) == 0) ||
                     (filename.rfind("core/", 0) == 0) || (filename.rfind("core\\", 0) == 0);

  bool isCompilingBuildSystem = false;
  if (!m_RecursionStack.empty()) {
    std::string rootBasename = getBasename(m_RecursionStack[0]);
    if (rootBasename == "build.tk" || rootBasename == "Project.tk") {
      isCompilingBuildSystem = true;
    }
  } else {
    std::string rootBasename = getBasename(filename);
    if (rootBasename == "build.tk" || rootBasename == "Project.tk") {
      isCompilingBuildSystem = true;
    }
  }

  bool hasExt = PathUtils::hasTokaSourceExtension(filename);

  std::string resolvedPath = filename;
  bool found = false;

  // 0. Check package aliases
  auto pkgIt = m_PkgMap.find(filename);
  if (pkgIt != m_PkgMap.end()) {
    std::string mapped = pkgIt->second;
    bool mappedHasExt = PathUtils::hasTokaSourceExtension(mapped);
    if (!mappedHasExt) {
      std::string mappedTk = mapped + ".tk";
      std::string mappedTki = mapped + ".tki";
      bool canUseTk = fileExists(mappedTk);
      bool canUseTki = fileExists(mappedTki);

      if (m_PreferSource) {
        if (canUseTk) {
          resolvedPath = mappedTk;
          found = true;
        } else if (canUseTki) {
          resolvedPath = mappedTki;
          found = true;
        } else if (fileExists(mapped)) {
          resolvedPath = mapped;
          found = true;
        }
      } else {
        if (canUseTki) {
          resolvedPath = mappedTki;
          found = true;
        } else if (canUseTk) {
          resolvedPath = mappedTk;
          found = true;
        } else if (fileExists(mapped)) {
          resolvedPath = mapped;
          found = true;
        }
      }
    } else {
      if (fileExists(mapped)) {
        resolvedPath = mapped;
        found = true;
      }
    }
  }
  // 1. Try exact filename
  else if (fileExists(filename)) {
    bool shouldPoison = false;
    if (isCompilingBuildSystem && m_RecursionStack.size() >= 1) {
      std::string resBasename = getBasename(filename);
      if (resBasename == "build.tk" || resBasename == "Project.tk" ||
          resBasename == "build.tki" || resBasename == "Project.tki") {
        std::string tkFromFile = filename;
        if (tkFromFile.length() >= 4 && tkFromFile.substr(tkFromFile.length() - 4) == ".tki") {
          tkFromFile.replace(tkFromFile.length() - 4, 4, ".tk");
        }
        if (PathUtils::canonicalize(tkFromFile) == m_RecursionStack[0]) {
          shouldPoison = true;
        }
      }
    }
    if (!isStdOrCore && !shouldPoison) {
      found = true;
    }
  }
  // 2. Try adding .tk or .tki if no extension
  else if (!hasExt && !isStdOrCore) {
    std::string resolvedTki = filename + ".tki";
    std::string resolvedTk = filename + ".tk";

    bool canUseTki = fileExists(resolvedTki);
    bool canUseTk = fileExists(resolvedTk);

    if (isCompilingBuildSystem && m_RecursionStack.size() >= 1) {
      if (getBasename(resolvedTki) == "build.tki" || getBasename(resolvedTki) == "Project.tki") {
        std::string tkFromTki = resolvedTki;
        tkFromTki.replace(tkFromTki.length() - 4, 4, ".tk");
        if (PathUtils::canonicalize(tkFromTki) == m_RecursionStack[0]) {
          canUseTki = false;
        }
      }
      if (getBasename(resolvedTk) == "build.tk" || getBasename(resolvedTk) == "Project.tk") {
        if (PathUtils::canonicalize(resolvedTk) == m_RecursionStack[0]) {
          canUseTk = false;
        }
      }
    }

    if (m_PreferSource) {
      if (canUseTk) {
        resolvedPath = resolvedTk;
        found = true;
      } else if (canUseTki) {
        resolvedPath = resolvedTki;
        found = true;
      }
    } else {
      if (canUseTki) {
        resolvedPath = resolvedTki;
        found = true;
      } else if (canUseTk) {
        resolvedPath = resolvedTk;
        found = true;
      }
    }
  }

  // 3. Try search paths and lib/ paths
  if (!found) {
    std::vector<std::string> pathsToTry;
    pathsToTry.push_back("lib/");
    pathsToTry.push_back("../lib/");
    for (const auto &p : activeSearchPaths) {
      if (!p.empty() && p.back() != '/' && p.back() != '\\') {
        pathsToTry.push_back(p + "/");
      } else {
        pathsToTry.push_back(p);
      }
    }
    for (const auto &p : pathsToTry) {
      std::string libPath = p + filename;
      if (fileExists(libPath)) {
        resolvedPath = libPath;
        found = true;
        break;
      }
      if (!hasExt) {
        if (m_PreferSource) {
          if (fileExists(libPath + ".tk")) {
            resolvedPath = libPath + ".tk";
            found = true;
            break;
          }
          if (fileExists(libPath + ".tki")) {
            resolvedPath = libPath + ".tki";
            found = true;
            break;
          }
        } else {
          if (fileExists(libPath + ".tki")) {
            resolvedPath = libPath + ".tki";
            found = true;
            break;
          }
          if (fileExists(libPath + ".tk")) {
            resolvedPath = libPath + ".tk";
            found = true;
            break;
          }
        }
      }
    }
  }

  if (found) {
    std::string norm = PathUtils::normalize(resolvedPath);
    return norm;
  }
  return "";
}

bool ModuleResolver::parseRecursive(const std::string &filename,
                                    std::vector<std::unique_ptr<Module>> &astModules,
                                    const std::string &overrideSourceCode,
                                    std::string *outActualPath) {
  std::string resolvedPath = filename;
  std::string originalTkPath = "";
  std::string selectedCachedObjectPath;
  bool selectedCachedInterfaceHasBacking = false;
  if (overrideSourceCode.empty()) {
      resolvedPath = resolveSourcePath(filename, m_SearchPaths);
      if (resolvedPath.empty()) {
          DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                                   "Could not open file: " + PathUtils::normalize(filename));
          return false;
      }
      originalTkPath = PathUtils::canonicalize(resolvedPath);
      std::string canonical = originalTkPath;
      const char *envBuildDir = std::getenv("TOKA_BUILD_DIR");
      std::string buildDir = envBuildDir ? envBuildDir : ".toka/build";
      std::string expectedObj = buildDir + "/objects/" + calculateFNV1a(canonical) + ".o";
      std::string expectedTki = buildDir + "/interfaces/" + calculateFNV1a(canonical) + ".tki";
      bool isObjProvided = (m_ProvidedObjects.find(PathUtils::canonicalize(expectedObj)) != m_ProvidedObjects.end());
      bool cacheObjExists = (std::ifstream(expectedObj).good());
      bool cacheTkiExists = (std::ifstream(expectedTki).good());
      bool isRoot = (std::find(m_Roots.begin(), m_Roots.end(), canonical) != m_Roots.end());
      bool shouldUseCachedInterface =
          isObjProvided || ((!m_PreferSource || m_UseBuildCache) && cacheTkiExists);
      if (!isRoot && shouldUseCachedInterface) {
          if (cacheTkiExists) {
              resolvedPath = expectedTki;
              selectedCachedInterfaceHasBacking = isObjProvided || cacheObjExists;
              if (selectedCachedInterfaceHasBacking)
                  selectedCachedObjectPath = PathUtils::canonicalize(expectedObj);
          }
      }
  } else {
      resolvedPath = PathUtils::normalize(filename);
      originalTkPath = resolvedPath;
  }

  // Validate TKI metadata and potentially fallback to .tk
  bool isTki = (resolvedPath.length() >= 4 && resolvedPath.substr(resolvedPath.length() - 4) == ".tki");
  bool fallbackTriggered = false;
  TKICacheStatus cacheStatus = TKICacheStatus::Ok;
  std::string cacheStatusReason = "";
  std::string sourceHash = "";
  TKIMetadata meta;

  if (isTki) {
      if (readTKIMetadata(resolvedPath, meta)) {
          sourceHash = meta.SourceHash;
      }
      std::string reason;
      TKICacheStatus status = validateTKIMetadata(resolvedPath, reason);
      cacheStatus = status;
      cacheStatusReason = reason;
      if (status != TKICacheStatus::Ok) {
          std::string tkPath = meta.SourcePath;
          if (tkPath.empty()) {
              if (originalTkPath.length() >= 4 && originalTkPath.substr(originalTkPath.length() - 4) != ".tki") {
                  tkPath = originalTkPath;
              }
          }
          if (tkPath.empty()) {
              tkPath = resolvedPath;
              tkPath.replace(tkPath.length() - 4, 4, ".tk");
          }
          if (std::ifstream(tkPath).good()) {
              resolvedPath = tkPath;
              fallbackTriggered = true;
              selectedCachedInterfaceHasBacking = false;
          } else {
              DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                                       "Incompatible or stale interface file: " + PathUtils::normalize(resolvedPath) + " (" + reason + ")");
              return false;
          }
      }
  }

  std::string canonicalPath = PathUtils::canonicalize(resolvedPath);
  if (outActualPath) {
      *outActualPath = canonicalPath;
  }

  auto getFileHash = [&](const std::string &filePath) -> std::string {
      std::ifstream ifs(filePath, std::ios::binary);
      if (!ifs) return "";
      std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
      return calculateFNV1a(content);
  };

  bool finalIsInterface = (resolvedPath.length() >= 4 && resolvedPath.substr(resolvedPath.length() - 4) == ".tki");
  std::string contentHash = getFileHash(resolvedPath);
  if (!finalIsInterface) {
      sourceHash = contentHash;
  }

  ModuleResolutionInfo info;
  info.CanonicalPath = canonicalPath;
  info.IsInterface = finalIsInterface;
  info.FallbackTriggered = fallbackTriggered;
  info.CacheStatus = cacheStatus;
  info.CacheStatusReason = cacheStatusReason;
  info.SourceHash = sourceHash;
  info.ContentHash = contentHash;
  info.MemoryEvidenceStatus = "NotApplicable";
  if (finalIsInterface) {
      info.SourcePath = meta.SourcePath;
  } else {
      info.SourcePath = canonicalPath;
  }
  m_ResolutionRecords[canonicalPath] = info;

  // Check recursion stack for circular dependency
  for (const auto &f : m_RecursionStack) {
    if (f == canonicalPath) {
      std::string dir1 = PathUtils::normalize(std::filesystem::absolute(canonicalPath).parent_path().string());
      std::string dir2 = PathUtils::normalize(std::filesystem::absolute(f).parent_path().string());
      if (dir1 == dir2) {
        // Allow circular imports within the same physical directory to allow modular file splitting
        return true;
      }
      std::string chain;
      for (const auto &s : m_RecursionStack)
        chain += s + " -> ";
      chain += canonicalPath;
      DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                               "Circular dependency detected: " + chain);
      return false;
    }
  }

  if (m_Visited.count(canonicalPath))
    return true;
  m_Visited.insert(canonicalPath);
  m_RecursionStack.push_back(canonicalPath);

  struct StackGuard {
      std::vector<std::string> &stack;
      StackGuard(std::vector<std::string> &s) : stack(s) {}
      ~StackGuard() { stack.pop_back(); }
  } guard(m_RecursionStack);

  std::string parserPath = (finalIsInterface && !meta.SourcePath.empty())
      ? PathUtils::canonicalize(meta.SourcePath)
      : resolvedPath;

  SourceLocation startLoc;
  if (!overrideSourceCode.empty()) {
      startLoc = m_SourceManager.addFile(resolvedPath, overrideSourceCode);
  } else if (finalIsInterface && !meta.SourcePath.empty()) {
      std::ifstream input(resolvedPath);
      if (!input) {
          DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                                   "Failed to load interface file: " + resolvedPath);
          return false;
      }
      std::stringstream buffer;
      buffer << input.rdbuf();
      startLoc = m_SourceManager.addFile(parserPath + "#interface", buffer.str());
  } else {
      startLoc = m_SourceManager.loadFile(resolvedPath);
  }

  if (startLoc.isInvalid()) {
    if (overrideSourceCode.empty()) {
      DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                               "Failed to load file via SourceManager: " + resolvedPath);
    }
    return false;
  }
  std::string code(m_SourceManager.getBufferData(startLoc));

  Lexer lexer(code.c_str(), startLoc);
  auto tokens = lexer.tokenize();

  Parser parser(tokens, parserPath);
  auto module = parser.parseModule();
  if (!module) {
    return false;
  }

  module->SourcePath = (finalIsInterface && !meta.SourcePath.empty())
      ? PathUtils::canonicalize(meta.SourcePath)
      : resolvedPath;
  module->ResolvedPath = canonicalPath;
  module->IsRootModule = (m_RecursionStack.size() == 1);
  module->IsInterface = finalIsInterface;
  module->IsTrustedSystemModule =
      finalIsInterface &&
      (isWithinRoot(originalTkPath, m_TrustedSystemRoots) ||
       isWithinRoot(canonicalPath, m_TrustedSystemRoots));
  module->HasBackingObject = finalIsInterface && selectedCachedInterfaceHasBacking;
  if (module->HasBackingObject) {
    std::string evidenceReason;
    MemoryEvidenceStatus evidenceStatus = MemoryEvidenceCache::load(
        MemoryEvidenceCache::sidecarPath(resolvedPath),
        selectedCachedObjectPath, meta.SourceHash, Parser::TargetTriple,
        module->TrustedMemorySummaries, evidenceReason);
    module->MemoryEvidenceStatus = toString(evidenceStatus);
    module->MemoryEvidenceReason = evidenceReason;
    auto record = m_ResolutionRecords.find(canonicalPath);
    if (record != m_ResolutionRecords.end()) {
      record->second.MemoryEvidenceStatus = module->MemoryEvidenceStatus;
      record->second.MemoryEvidenceReason = evidenceReason;
    }
  }

  // Recursively parse imports
  for (const auto &imp : module->Imports) {
    if (!imp->Items.empty()) {
      // TODO: Handle logic import symbol filtering
    }

    std::string importPath = imp->PhysicalPath;
    if (importPath.rfind("./", 0) == 0 || importPath.rfind("../", 0) == 0) {
        std::string baseSourcePath = canonicalPath;
        auto recIt = m_ResolutionRecords.find(canonicalPath);
        if (recIt != m_ResolutionRecords.end() && !recIt->second.SourcePath.empty()) {
            baseSourcePath = recIt->second.SourcePath;
        }
        size_t lastSlash = baseSourcePath.find_last_of("/\\");
        std::string parentDir = (lastSlash == std::string::npos) ? "." : baseSourcePath.substr(0, lastSlash);
        importPath = parentDir + "/" + importPath;
    }

    std::vector<std::string> localSearchPaths = m_SearchPaths;
    size_t libPos = resolvedPath.find("/lib/");
    if (libPos == std::string::npos) {
        libPos = resolvedPath.find("\\lib\\");
    }
    if (libPos != std::string::npos) {
        std::string pkgRoot = resolvedPath.substr(0, libPos);
        localSearchPaths.push_back(pkgRoot);
    }

    std::string subResolved = resolveSourcePath(importPath, localSearchPaths);
    if (!subResolved.empty()) {
        imp->ResolvedPath = PathUtils::canonicalize(subResolved);
    } else {
        imp->ResolvedPath = "";
    }

    std::string actualSubPath;
    if (!parseRecursive(subResolved.empty() ? importPath : subResolved, astModules, "", &actualSubPath)) {
        return false;
    }

    if (!subResolved.empty()) {
        std::string depPath = actualSubPath.empty() ? PathUtils::canonicalize(subResolved) : actualSubPath;
        auto &deps = m_Dependencies[canonicalPath];
        if (std::find(deps.begin(), deps.end(), depPath) == deps.end()) {
            deps.push_back(depPath);
        }
        imp->ResolvedPath = depPath;
    }
  }

  astModules.push_back(std::move(module));
  return true;
}

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

bool ModuleResolver::readTKIMetadata(const std::string &path, TKIMetadata &meta) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("// @meta ", 0) == 0) {
            std::string content = line.substr(9);
            size_t colon = content.find(':');
            if (colon != std::string::npos) {
                std::string key = content.substr(0, colon);
                std::string val = content.substr(colon + 1);
                auto trim = [](std::string &s) {
                    s.erase(0, s.find_first_not_of(" \t"));
                    s.erase(s.find_last_not_of(" \t") + 1);
                };
                trim(key);
                trim(val);
                if (key == "compiler_version") meta.CompilerVersion = val;
                else if (key == "format_version") meta.FormatVersion = val;
                else if (key == "target_triple") meta.TargetTriple = val;
                else if (key == "source_hash") meta.SourceHash = val;
                else if (key == "source_path") meta.SourcePath = val;
            }
        } else if (line.rfind("//", 0) == 0 || line.empty()) {
            continue;
        } else {
            break;
        }
    }
    return true;
}

TKICacheStatus ModuleResolver::validateTKIMetadata(const std::string &path, std::string &reason) {
    TKIMetadata meta;
    if (!readTKIMetadata(path, meta)) {
        reason = "Could not read interface file";
        return TKICacheStatus::ReadError;
    }
    if (meta.CompilerVersion.empty()) {
        reason = "Missing compiler_version metadata";
        return TKICacheStatus::MissingCompilerVersion;
    }
    if (meta.FormatVersion.empty()) {
        reason = "Missing format_version metadata";
        return TKICacheStatus::MissingFormatVersion;
    }
    if (meta.TargetTriple.empty()) {
        reason = "Missing target_triple metadata";
        return TKICacheStatus::MissingTargetTriple;
    }
    if (meta.SourceHash.empty()) {
        reason = "Missing source_hash metadata";
        return TKICacheStatus::MissingSourceHash;
    }
    if (meta.SourceHash != "any" && meta.SourcePath.empty()) {
        reason = "Missing source_path metadata";
        return TKICacheStatus::MissingSourcePath;
    }

    if (meta.CompilerVersion != "any" && meta.CompilerVersion != TOKA_COMPILER_INTERFACE_VERSION) {
        reason = "Compiler version mismatch (expected " + std::string(TOKA_COMPILER_INTERFACE_VERSION) + ", got " + meta.CompilerVersion + ")";
        return TKICacheStatus::CompilerVersionMismatch;
    }
    if (meta.FormatVersion != TOKA_INTERFACE_FORMAT_VERSION) {
        reason = "Interface format version mismatch (expected " + std::string(TOKA_INTERFACE_FORMAT_VERSION) + ", got " + meta.FormatVersion + ")";
        return TKICacheStatus::FormatVersionMismatch;
    }
    if (meta.TargetTriple != "any" && meta.TargetTriple != Parser::TargetTriple) {
        reason = "Target triple mismatch (expected " + Parser::TargetTriple + ", got " + meta.TargetTriple + ")";
        return TKICacheStatus::TargetTripleMismatch;
    }

    // Check source hash if corresponding .tk file exists
    if (meta.SourceHash != "any") {
        std::string tkPath = meta.SourcePath;
        if (tkPath.empty()) {
            tkPath = path;
            if (tkPath.length() >= 4 && tkPath.substr(tkPath.length() - 4) == ".tki") {
                tkPath.replace(tkPath.length() - 4, 4, ".tk");
            }
        }
        std::ifstream tkFile(tkPath, std::ios::binary);
        if (tkFile.good()) {
            std::string content((std::istreambuf_iterator<char>(tkFile)), std::istreambuf_iterator<char>());
            std::string currentHash = calculateFNV1a(content);
            if (meta.SourceHash != currentHash) {
                reason = "Source file has changed (hash mismatch)";
                return TKICacheStatus::SourceHashMismatch;
            }
        }
    }
    return TKICacheStatus::Ok;
}

void ModuleResolver::setProvidedObjects(const std::vector<std::string> &objs) {
    for (const auto &obj : objs) {
        m_ProvidedObjects.insert(PathUtils::canonicalize(obj));
    }
}

} // namespace toka
