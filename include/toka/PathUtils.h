#ifndef TOKA_PATH_UTILS_H
#define TOKA_PATH_UTILS_H

#include <string>
#include <filesystem>
#include <algorithm>
#include <vector>

namespace toka {
namespace PathUtils {

// Normalize path: lexically normal and replace backslashes with slashes
inline std::string normalize(const std::string &path) {
  std::string p = std::filesystem::path(path).lexically_normal().string();
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

inline std::string stripInterfaceSuffix(std::string path) {
  size_t pos = path.find("#interface");
  if (pos != std::string::npos) {
    path.resize(pos);
  }
  return path;
}

inline bool endsWith(const std::string &path, const std::string &suffix) {
  return path.size() >= suffix.size() &&
         path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string stripTokaModuleExtension(std::string path) {
  path = stripInterfaceSuffix(std::move(path));
  if (endsWith(path, ".tk_lib")) {
    path.resize(path.size() - 7);
  } else if (endsWith(path, ".tki")) {
    path.resize(path.size() - 4);
  } else if (endsWith(path, ".tk")) {
    path.resize(path.size() - 3);
  }
  return path;
}

inline std::string trimModulePathSlashes(std::string path) {
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  while (!path.empty() && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

// Canonicalize path: weakly_canonical (physical absolute path) with absolute fallback
inline std::string canonicalize(const std::string &path) {
  try {
    std::string p = std::filesystem::weakly_canonical(stripInterfaceSuffix(path)).string();
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
  } catch (...) {
    std::string p = std::filesystem::absolute(stripInterfaceSuffix(path)).lexically_normal().string();
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
  }
}

inline void addModulePathCandidate(std::vector<std::string> &candidates,
                                   std::string candidate) {
  candidate = trimModulePathSlashes(
      normalize(stripTokaModuleExtension(std::move(candidate))));
  if (candidate.empty()) {
    return;
  }
  if (std::find(candidates.begin(), candidates.end(), candidate) ==
      candidates.end()) {
    candidates.push_back(std::move(candidate));
  }
}

inline std::vector<std::string> modulePathIdentityCandidates(
    const std::string &moduleFile) {
  std::vector<std::string> candidates;
  std::string raw = stripInterfaceSuffix(moduleFile);
  std::string canonical = canonicalize(raw);

  addModulePathCandidate(candidates, raw);
  addModulePathCandidate(candidates, canonical);

  try {
    addModulePathCandidate(
        candidates,
        std::filesystem::relative(canonical, std::filesystem::current_path())
            .string());
  } catch (...) {
  }

  std::vector<std::string> baseCandidates = candidates;
  for (const auto &candidate : baseCandidates) {
    size_t libPos = candidate.find("/lib/");
    if (libPos != std::string::npos) {
      addModulePathCandidate(candidates, candidate.substr(libPos + 5));
    } else if (candidate.rfind("lib/", 0) == 0) {
      addModulePathCandidate(candidates, candidate.substr(4));
    }

    size_t testsPos = candidate.find("/tests/");
    if (testsPos != std::string::npos) {
      addModulePathCandidate(candidates,
                             "tests/" + candidate.substr(testsPos + 7));
    } else if (candidate.rfind("tests/", 0) == 0) {
      addModulePathCandidate(candidates, candidate);
    }

    size_t slash = candidate.find_last_of('/');
    addModulePathCandidate(
        candidates,
        slash == std::string::npos ? candidate : candidate.substr(slash + 1));
  }

  return candidates;
}

inline bool modulePathMatchesTarget(const std::string &moduleFile,
                                    const std::string &targetPath) {
  std::string target = trimModulePathSlashes(
      normalize(stripTokaModuleExtension(targetPath)));
  if (target.empty()) {
    return false;
  }

  for (const auto &candidate : modulePathIdentityCandidates(moduleFile)) {
    if (candidate == target) {
      return true;
    }
    if (candidate.rfind(target + "/", 0) == 0) {
      return true;
    }
  }
  return false;
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
