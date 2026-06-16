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

// Check if a path is an interface file (.tki)
inline bool isInterfaceFile(const std::string &path) {
  return path.length() > 4 && path.substr(path.length() - 4) == ".tki";
}

// Get interface path next to outputFile (same directory, same stem, replace suffix with .tki)
// or CWD if outputFile is empty.
inline std::string getInterfacePath(const std::string &outputFile, const std::string &sourcePath) {
  std::string outPath;
  if (!outputFile.empty()) {
    outPath = outputFile;
    size_t dotPos = outPath.find_last_of('.');
    if (dotPos != std::string::npos) {
      outPath = outPath.substr(0, dotPos) + ".tki";
    } else {
      outPath += ".tki";
    }
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
