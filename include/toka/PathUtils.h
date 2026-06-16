#ifndef TOKA_PATH_UTILS_H
#define TOKA_PATH_UTILS_H

#include <string>
#include <filesystem>
#include <algorithm>

namespace toka {
namespace PathUtils {

// Normalize path: lexically normal and replace backslashes with slashes
inline std::string normalize(const std::string &path) {
  std::string p = std::filesystem::path(path).lexically_normal().string();
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

// Canonicalize path: weakly_canonical (physical absolute path) with absolute fallback
inline std::string canonicalize(const std::string &path) {
  try {
    std::string p = std::filesystem::weakly_canonical(path).string();
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
  } catch (...) {
    std::string p = std::filesystem::absolute(path).lexically_normal().string();
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
  }
}

// Check if a path is an interface file (.tki)
inline bool isInterfaceFile(const std::string &path) {
  return path.length() > 4 && path.substr(path.length() - 4) == ".tki";
}

// Check if a path has a Toka source file extension (.tk or .tki or .tk_lib)
inline bool hasTokaSourceExtension(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  return ext == ".tk" || ext == ".tki" || ext == ".tk_lib";
}

// Get interface path next to outputFile (same directory, same stem, replace suffix with .tki)
// or CWD if outputFile is empty.
inline std::string getInterfacePath(const std::string &outputFile, const std::string &sourcePath) {
  std::string outPath;
  if (!outputFile.empty()) {
    outPath = normalize(std::filesystem::path(outputFile).replace_extension(".tki").string());
  } else {
    // Write to CWD, avoiding source directory pollution
    size_t lastSlash = sourcePath.find_last_of("/\\");
    std::string baseName = (lastSlash == std::string::npos) ? sourcePath : sourcePath.substr(lastSlash + 1);
    size_t dotPos = baseName.find_last_of('.');
    if (dotPos != std::string::npos && (baseName.substr(dotPos) == ".tk" || baseName.substr(dotPos) == ".tk_lib")) {
      outPath = baseName.substr(0, dotPos) + ".tki";
    } else {
      outPath = baseName + ".tki";
    }
  }
  return outPath;
}

// Check if a module should be declOnly during CodeGen
inline bool isDeclOnly(bool isRootModule, const std::string &sourcePath) {
  return !isRootModule && isInterfaceFile(sourcePath);
}

} // namespace PathUtils
} // namespace toka

#endif // TOKA_PATH_UTILS_H
