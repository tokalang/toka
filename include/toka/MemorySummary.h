// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace llvm {
class Module;
}

namespace toka {

class FunctionDecl;
class Module;

enum class MemoryRootEffect : uint32_t {
  None = 0,
  Read = 1u << 0,
  Write = 1u << 1,
  Rebind = 1u << 2,
  Invalidate = 1u << 3,
  Capture = 1u << 4,
  Escape = 1u << 5,
  Transfer = 1u << 6,
  Unknown = 1u << 7,
};

enum class FunctionMemoryEffect : uint32_t {
  None = 0,
  Allocate = 1u << 0,
  Free = 1u << 1,
  TouchGlobal = 1u << 2,
  UnknownCall = 1u << 3,
  RawProvenance = 1u << 4,
  UnsafeBoundary = 1u << 5,
  Suspend = 1u << 6,
  UnknownBoundary = 1u << 7,
};

enum class MemorySummaryOrigin { SourceBody, SignatureOnly, TrustedCache };

struct MemoryRootSummary {
  uint32_t LocalEffects = 0;
  uint32_t Effects = 0;
};

struct FunctionMemorySummary {
  static constexpr unsigned SchemaVersion = 2;

  std::string FunctionName;
  MemorySummaryOrigin Origin = MemorySummaryOrigin::SourceBody;
  std::map<std::string, MemoryRootSummary> Roots;
  uint32_t LocalEffects = 0;
  uint32_t Effects = 0;
};

class MemorySummaryAnalysis {
public:
  static std::vector<FunctionDecl *>
  collectFunctions(const std::vector<Module *> &modules);
  static void run(const std::vector<Module *> &modules,
                  bool borrowCheckEnabled,
                  bool activateTrustedEvidence = false);
  static bool verify(const std::vector<Module *> &modules,
                     bool borrowCheckEnabled,
                     std::vector<std::string> &errors);
  static bool verifyIR(const std::vector<Module *> &modules,
                       const llvm::Module &irModule,
                       std::vector<std::string> &errors);
  static void dumpJSON(const std::vector<Module *> &modules,
                       std::ostream &out);
};

} // namespace toka
