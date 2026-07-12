// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include "toka/MemorySummary.h"
#include <map>
#include <string>
#include <vector>

namespace toka {

class Module;

enum class MemoryEvidenceStatus {
  NotApplicable,
  Missing,
  ReadError,
  InvalidSchema,
  IdentityMismatch,
  MissingObject,
  ObjectMismatch,
  EvidenceMismatch,
  InvalidRecord,
  Valid,
};

class MemoryEvidenceCache {
public:
  static constexpr unsigned SchemaVersion = 1;

  static std::string sidecarPath(const std::string &interfacePath);
  static std::string sourceHash(const std::string &sourcePath);
  static bool bindObject(llvm::Module &irModule, const Module &module,
                         const std::string &sourceHash,
                         const std::string &targetTriple,
                         std::vector<std::string> &errors);
  static bool write(const std::string &path, const std::string &objectPath,
                    const Module &module, const std::string &sourceHash,
                    const std::string &targetTriple,
                    std::vector<std::string> &errors);
  static MemoryEvidenceStatus load(
      const std::string &path, const std::string &objectPath,
      const std::string &expectedSourceHash,
      const std::string &expectedTargetTriple,
      std::map<std::string, FunctionMemorySummary> &summaries,
      std::string &reason);
};

const char *toString(MemoryEvidenceStatus status);

} // namespace toka
