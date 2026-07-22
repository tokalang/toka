// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "toka/DiagnosticEngine.h"
#include "toka/SemanticIndex.h"
#include "toka/SourceManager.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

struct AnalysisStats {
  uint64_t Revision = 0;
  size_t TotalModules = 0;
  size_t ReusedModules = 0;
  size_t RecheckedModules = 0;
  double ElapsedMilliseconds = 0.0;
  std::vector<std::string> InvalidatedModules;
};

struct AnalysisResult {
  bool Success = false;
  bool HasFreshIndex = false;
  SemanticIndex Index;
  std::vector<DiagnosticRecord> Diagnostics;
  AnalysisStats Stats;
};

class AnalysisSession {
public:
  AnalysisSession(std::vector<std::string> searchPaths = {},
                  std::map<std::string, std::string> packageMap = {},
                  std::vector<std::string> trustedSystemRoots = {});

  void openDocument(const std::string &path, std::string content);
  void updateDocument(const std::string &path, std::string content);
  void closeDocument(const std::string &path);
  bool hasDocument(const std::string &path) const;

  AnalysisResult analyze(const std::string &rootPath);

  const std::map<std::string, std::vector<std::string>> &dependencies() const {
    return Dependencies;
  }

private:
  std::vector<std::string> SearchPaths;
  std::map<std::string, std::string> PackageMap;
  std::vector<std::string> TrustedSystemRoots;
  std::map<std::string, std::string> Overlays;
  std::unique_ptr<SourceManager> Sources;
  std::vector<std::unique_ptr<Module>> Modules;
  std::map<std::string, std::string> Fingerprints;
  std::map<std::string, std::vector<std::string>> Dependencies;
  SemanticIndex LastIndex;
  bool HasIndex = false;
  uint64_t Revision = 0;

  static std::string modulePath(const Module &module);
  static std::map<std::string, std::set<std::string>> reverseDependencies(
      const std::map<std::string, std::vector<std::string>> &dependencies);
};

} // namespace toka
