// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
}

namespace toka {

class Module;
struct MemoryContractPrepared;

enum class MemoryContractKind {
  NoCapture,
  ReadOnly,
  WriteOnly,
  NoAlias,
};

enum class MemoryContractDecision { Candidate, Reject };

enum class MemoryContractReason {
  ProvenBySummary,
  ProvenByTrustedCache,
  MissingIRFunction,
  MissingIRParameter,
  SignatureOnly,
  NonPointerABI,
  BorrowCheckDisabled,
  SuspendBoundary,
  UnsafeBoundary,
  RawProvenance,
  UnknownBoundary,
  UnknownRoot,
  IRCaptureDetected,
  ReadsMemory,
  NoWrites,
  WritesMemory,
  RebindsHandle,
  InvalidatesRoot,
  CapturesRoot,
  EscapesRoot,
  TransfersOwnership,
  SeparateNoAliasGate,
};

struct MemoryContractRecord {
  std::string FunctionName;
  std::string ParameterName;
  unsigned ParameterIndex = 0;
  MemoryContractKind Kind = MemoryContractKind::NoCapture;
  MemoryContractDecision Decision = MemoryContractDecision::Reject;
  MemoryContractReason Reason = MemoryContractReason::UnknownBoundary;
  bool Emitted = false;
};

class MemoryContractShadow {
public:
  static constexpr unsigned SchemaVersion = 3;

  MemoryContractShadow();
  ~MemoryContractShadow();
  MemoryContractShadow(MemoryContractShadow &&) noexcept;
  MemoryContractShadow &operator=(MemoryContractShadow &&) noexcept;
  MemoryContractShadow(const MemoryContractShadow &) = delete;
  MemoryContractShadow &operator=(const MemoryContractShadow &) = delete;

  static MemoryContractShadow analyze(const std::vector<Module *> &modules,
                                      const llvm::Module &irModule,
                                      bool borrowCheckEnabled);
  bool verify(const std::vector<Module *> &modules,
              const llvm::Module &irModule,
              bool borrowCheckEnabled,
              std::vector<std::string> &errors) const;
  unsigned emitExperimental(llvm::Module &irModule,
                            MemoryContractKind kind);
  bool verifyExperimental(const llvm::Module &irModule,
                          MemoryContractKind kind, bool enabled,
                          std::vector<std::string> &errors) const;
  void dumpJSON(std::ostream &out) const;

  const std::vector<MemoryContractRecord> &records() const { return Records; }

private:
  std::vector<MemoryContractRecord> Records;
  // One-shot, local preparation for the analyze -> verify interval only.
  mutable std::unique_ptr<MemoryContractPrepared> Prepared;
};

const char *toString(MemoryContractKind value);
const char *toString(MemoryContractDecision value);
const char *toString(MemoryContractReason value);

} // namespace toka
