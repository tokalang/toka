#include "toka/ModuleResolver.h"
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/DiagnosticEngine.h"
#include "toka/PathUtils.h"
#include <fstream>
#include <algorithm>
#include <iostream>

namespace toka {

ModuleResolver::ModuleResolver(SourceManager &sm,
                               std::vector<std::string> searchPaths,
                               std::map<std::string, std::string> pkgMap)
    : m_SourceManager(sm), m_SearchPaths(std::move(searchPaths)), m_PkgMap(std::move(pkgMap)) {}

bool ModuleResolver::resolveAndParse(const std::string &rawFilename,
                                     std::vector<std::unique_ptr<Module>> &astModules,
                                     const std::string &overrideSourceCode) {
    m_RecursionStack.clear();
    return parseRecursive(rawFilename, astModules, overrideSourceCode);
}

std::string ModuleResolver::resolveSourcePath(const std::string &rawFilename,
                                             const std::vector<std::string> &activeSearchPaths) {
  std::string filename = PathUtils::normalize(rawFilename);

  auto fileExists = [](const std::string &p) {
    return std::ifstream(p).good();
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
      if (fileExists(mapped + ".tk")) {
        resolvedPath = mapped + ".tk";
        found = true;
      } else if (fileExists(mapped + ".tki")) {
        resolvedPath = mapped + ".tki";
        found = true;
      } else if (fileExists(mapped)) {
        resolvedPath = mapped;
        found = true;
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
    if (isCompilingBuildSystem && m_RecursionStack.size() > 1) {
      std::string resBasename = getBasename(filename);
      if (resBasename == "build.tk" || resBasename == "Project.tk") {
        shouldPoison = true;
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

    if (isCompilingBuildSystem && m_RecursionStack.size() > 1) {
      if (getBasename(resolvedTki) == "build.tk" || getBasename(resolvedTki) == "Project.tk") {
        canUseTki = false;
      }
      if (getBasename(resolvedTk) == "build.tk" || getBasename(resolvedTk) == "Project.tk") {
        canUseTk = false;
      }
    }

    if (canUseTk) {
      resolvedPath = resolvedTk;
      found = true;
    } else if (canUseTki) {
      resolvedPath = resolvedTki;
      found = true;
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
      }
    }
  }

  if (found) {
    return PathUtils::normalize(resolvedPath);
  }
  return "";
}

bool ModuleResolver::parseRecursive(const std::string &filename,
                                    std::vector<std::unique_ptr<Module>> &astModules,
                                    const std::string &overrideSourceCode) {
  std::string resolvedPath = filename;
  if (overrideSourceCode.empty()) {
      resolvedPath = resolveSourcePath(filename, m_SearchPaths);
      if (resolvedPath.empty()) {
          DiagnosticEngine::report(DiagLoc{}, DiagID::ERR_FILE_IO,
                                   "Could not open file: " + PathUtils::normalize(filename));
          return false;
      }
  } else {
      resolvedPath = PathUtils::normalize(filename);
  }

  std::string canonicalPath = PathUtils::canonicalize(resolvedPath);

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

  SourceLocation startLoc = overrideSourceCode.empty()
      ? m_SourceManager.loadFile(resolvedPath)
      : m_SourceManager.addFile(resolvedPath, overrideSourceCode);

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

  Parser parser(tokens, resolvedPath);
  auto module = parser.parseModule();
  if (!module) {
    return false;
  }

  module->SourcePath = resolvedPath;
  module->IsRootModule = (m_RecursionStack.size() == 1);

  // Recursively parse imports
  for (const auto &imp : module->Imports) {
    if (!imp->Items.empty()) {
      // TODO: Handle logic import symbol filtering
    }

    std::string importPath = imp->PhysicalPath;
    if (importPath.rfind("./", 0) == 0 || importPath.rfind("../", 0) == 0) {
        size_t lastSlash = resolvedPath.find_last_of('/');
        std::string parentDir = (lastSlash == std::string::npos) ? "." : resolvedPath.substr(0, lastSlash);
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

    if (!parseRecursive(subResolved.empty() ? importPath : subResolved, astModules, "")) {
        return false;
    }
  }

  astModules.push_back(std::move(module));
  m_RecursionStack.pop_back();
  return true;
}

} // namespace toka
