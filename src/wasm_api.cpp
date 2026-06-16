#include <emscripten.h>
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/DiagnosticEngine.h"
#include "toka/PathUtils.h"
#include <string>
#include <sstream>
#include <iostream>

bool g_JsonDiagnostics = false;
bool verboseMode = false;

// Intercept std::cout to capture JSON output
class StringbufStream : public std::stringbuf {
public:
    std::string getString() {
        std::string s = this->str();
        this->str(""); // clear
        return s;
    }
};


#include <fstream>
#include <vector>
#include <set>
#include <memory>

static std::string resolveSourcePath(const std::string &rawFilename,
                                     const std::vector<std::string> &searchPaths,
                                     const std::vector<std::string> &recursionStack) {
  std::string filename = toka::PathUtils::normalize(rawFilename);

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
  if (!recursionStack.empty()) {
    std::string rootBasename = getBasename(recursionStack[0]);
    if (rootBasename == "build.tk" || rootBasename == "Project.tk") {
      isCompilingBuildSystem = true;
    }
  } else {
    std::string rootBasename = getBasename(filename);
    if (rootBasename == "build.tk" || rootBasename == "Project.tk") {
      isCompilingBuildSystem = true;
    }
  }

  bool hasExt = toka::PathUtils::hasTokaSourceExtension(filename);

  std::string resolvedPath = filename;
  bool found = false;

  // 1. Try exact filename
  if (fileExists(filename)) {
    bool shouldPoison = false;
    if (isCompilingBuildSystem && recursionStack.size() > 1) {
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
    
    if (isCompilingBuildSystem && recursionStack.size() > 1) {
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
    for (const auto &p : searchPaths) {
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
    return toka::PathUtils::normalize(resolvedPath);
  }
  return "";
}

void parseSource(const std::string &filename,
                 std::vector<std::unique_ptr<toka::Module>> &astModules,
                 std::set<std::string> &visited,
                 std::vector<std::string> &recursionStack,
                 toka::SourceManager &sm,
                 const std::vector<std::string> &searchPaths,
                 const std::string &code_cstr = "") {
  std::string resolvedPath = filename;
  if (code_cstr.empty()) {
      resolvedPath = resolveSourcePath(filename, searchPaths, recursionStack);
      if (resolvedPath.empty()) {
          toka::DiagnosticEngine::report(toka::DiagLoc{}, toka::DiagID::ERR_FILE_IO, "Could not open file: " + toka::PathUtils::normalize(filename));
          return;
      }
  } else {
      resolvedPath = toka::PathUtils::normalize(filename);
  }

  std::string canonicalPath = toka::PathUtils::canonicalize(resolvedPath);

  // Check recursion stack for circular dependency
  for (const auto &f : recursionStack) {
    if (f == canonicalPath) {
      std::string dir1 = toka::PathUtils::normalize(std::filesystem::absolute(canonicalPath).parent_path().string());
      std::string dir2 = toka::PathUtils::normalize(std::filesystem::absolute(f).parent_path().string());
      if (dir1 == dir2) {
        // Allow circular imports within the same physical directory to allow modular file splitting
        return;
      }
      std::string chain;
      for (const auto &s : recursionStack)
        chain += s + " -> ";
      chain += canonicalPath;
      toka::DiagnosticEngine::report(toka::DiagLoc{}, toka::DiagID::ERR_FILE_IO,
                                     "Circular dependency detected: " + chain);
      return;
    }
  }

  if (visited.count(canonicalPath)) return;
  visited.insert(canonicalPath);
  recursionStack.push_back(canonicalPath);

  toka::SourceLocation startLoc = code_cstr.empty() ? sm.loadFile(resolvedPath) : sm.addFile(resolvedPath, code_cstr);
  if (startLoc.isInvalid()) return;
  std::string code(sm.getBufferData(startLoc));

  toka::Lexer lexer(code.c_str(), startLoc);
  auto tokens = lexer.tokenize();

  toka::Parser parser(tokens, resolvedPath);
  auto module = parser.parseModule();

  if (!module) return;

  for (const auto &imp : module->Imports) {
      std::string importPath = imp->PhysicalPath;
      if (importPath.rfind("./", 0) == 0 || importPath.rfind("../", 0) == 0) {
          size_t lastSlash = resolvedPath.find_last_of('/');
          std::string parentDir = (lastSlash == std::string::npos) ? "." : resolvedPath.substr(0, lastSlash);
          importPath = parentDir + "/" + importPath;
      }
      
      std::string subResolved = resolveSourcePath(importPath, searchPaths, recursionStack);
      if (!subResolved.empty()) {
          imp->ResolvedPath = toka::PathUtils::canonicalize(subResolved);
      } else {
          imp->ResolvedPath = "";
      }
      
      parseSource(subResolved.empty() ? importPath : subResolved, astModules, visited, recursionStack, sm, searchPaths);
  }

  astModules.push_back(std::move(module));
  recursionStack.pop_back();
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* check_toka_code(const char* code_cstr) {
    static std::string lastResult;
    
    // Enable JSON output
    g_JsonDiagnostics = true;
    
    // Reset errors
    toka::DiagnosticEngine::ErrorCount = 0;
    
    // Capture stdout
    std::streambuf* oldCout = std::cout.rdbuf();
    StringbufStream captureBuf;
    std::cout.rdbuf(&captureBuf);

    toka::SourceManager sm;
    toka::DiagnosticEngine::init(sm);

    std::vector<std::unique_ptr<toka::Module>> astModules;
    std::set<std::string> visited;
    std::vector<std::string> recursionStack;
    std::vector<std::string> searchPaths; // Emscripten will have access to virtual /lib

    parseSource("playground.tk", astModules, visited, recursionStack, sm, searchPaths, code_cstr);

    if (!toka::DiagnosticEngine::hasErrors()) {
        toka::Sema sema;
        sema.setBorrowCheckEnabled(true);
        for (auto &mod : astModules) {
            sema.checkModule(*mod);
        }
    }

    // Restore stdout
    std::cout.rdbuf(oldCout);

    std::string errors = captureBuf.getString();
    
    if (errors.empty()) {
        lastResult = "{\"status\": \"ok\"}";
    } else {
        // Output might contain multiple JSON objects separated by newlines
        // We wrap them in a JSON array
        std::stringstream jsonArr;
        jsonArr << "{\"status\": \"error\", \"diagnostics\": [";
        
        bool first = true;
        std::istringstream iss(errors);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (!first) jsonArr << ",";
            jsonArr << line;
            first = false;
        }
        jsonArr << "]}";
        
        lastResult = jsonArr.str();
    }

    return lastResult.c_str();
}

} // extern "C"
