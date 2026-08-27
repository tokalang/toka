// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/DirectCallObservation.h"
#include "toka/SemanticEvidence.h"
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace toka {

enum class D3GateExclusionReason : uint8_t {
  WrongRoute,
  NonSameLexical,
  CandidateProbeOrSpeculativeContext,
  NonFinalSemanticTraversal,
  NestedObservationContext,
};

const char *toString(D3GateExclusionReason value);

struct D3ObservationSentinel {
  uintptr_t CallNodeIdentity = 0;
  uintptr_t ActualNodeIdentity = 0;
  uintptr_t ResolvedFunctionIdentity = 0;
  size_t ArgumentCount = 0;
  std::string Callee;
  std::string ActualSyntax;
  std::string ActualResolvedType;
  uint64_t SourceSymbolId = 0;
  std::string SourceIdentity;
  std::string SourcePlaceState;
  uint64_t SourceInitMask = 0;
  bool SourceMoved = false;
  bool SourceUsed = false;
  bool SourceMutated = false;
  bool SourcePlaceAlias = false;
  std::string SourceBorrowedPath;
  std::vector<std::string> SourceDependencies;
  std::string PALState;
  int DiagnosticErrorCount = 0;
  std::vector<std::string> DiagnosticCodes;
  SemanticEvidenceAuditState Evidence;
  size_t Slice4CopyProofCount = 0;
  size_t CopyProofFactCount = 0;
  std::string ReferencedCopyProof;
  std::vector<std::string> LastDependencies;
  bool PrecomputingCaptures = false;
  bool ExpectedCedeTransfer = false;
  bool AllowPermissionSuffix = false;

  friend bool operator==(const D3ObservationSentinel &lhs,
                         const D3ObservationSentinel &rhs);
};

std::vector<std::string>
differingD3SentinelFields(const D3ObservationSentinel &before,
                          const D3ObservationSentinel &after);

struct D3ObservationEnvelope {
  explicit D3ObservationEnvelope(D3FactoryObservationRecord record)
      : FactoryRecord(std::move(record)) {}
  D3FactoryObservationRecord FactoryRecord;
  bool PreFactCaptureUnchanged = false;
  bool PostCacheAndFactoryUnchanged = false;
  std::vector<std::string> DifferingSentinelFields;
};

class D3ObservationSession;

class D3ObservationScope final {
public:
  explicit D3ObservationScope(D3ObservationSession *session = nullptr);
  D3ObservationScope(const D3ObservationScope &) = delete;
  D3ObservationScope &operator=(const D3ObservationScope &) = delete;
  D3ObservationScope(D3ObservationScope &&other) noexcept;
  D3ObservationScope &operator=(D3ObservationScope &&other) noexcept;
  ~D3ObservationScope();

private:
  D3ObservationSession *Session = nullptr;
};

// One instance belongs to one tokac command. It owns no semantic authority and
// never escapes the command that created it.
class D3ObservationSession final {
public:
  bool hasActiveObservation() const { return ActiveObservationDepth != 0; }
  bool claimFinalTraversal(std::string callIdentity) {
    return FinalTraversalCalls.insert(std::move(callIdentity)).second;
  }
  void noteGateExclusion(D3GateExclusionReason reason);
  void noteFactoryInvocation() { ++FactoryInvocationCount; }
  void append(D3ObservationEnvelope envelope);
  size_t consideredCallCount() const { return ConsideredCallCount; }
  size_t factoryInvocationCount() const { return FactoryInvocationCount; }
  const std::vector<D3ObservationEnvelope> &envelopes() const {
    return Envelopes;
  }
  void dumpJSON(std::ostream &out) const;

private:
  friend class D3ObservationScope;
  void beginObservation();
  void endObservation();

  size_t ActiveObservationDepth = 0;
  size_t ConsideredCallCount = 0;
  size_t FactoryInvocationCount = 0;
  size_t WrongRouteCount = 0;
  size_t NonSameLexicalCount = 0;
  size_t CandidateProbeCount = 0;
  size_t NonFinalTraversalCount = 0;
  size_t NestedContextCount = 0;
  std::set<std::string> FinalTraversalCalls;
  std::vector<D3ObservationEnvelope> Envelopes;
};

} // namespace toka
