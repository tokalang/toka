// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/DirectCallObservationAudit.h"
#include <cstddef>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class D5GateExclusionReason : uint8_t {
  WrongRoute,
  NonSameLexical,
  OverloadOrCandidateProbe,
  SpeculativeOrNonFinalTraversal,
  NestedPreparation,
  ExistingCallDiagnostic,
};

enum class D5ParityError : uint8_t {
  PrePostFactMismatch,
  PreparedPlanMismatch,
  LegacyOutcomeMismatch,
  LegacyCheckCountMismatch,
  NonEmptyEvaluationDelta,
};

enum class D5InfrastructureError : uint8_t {
  InvalidCallSiteIdentity,
  InvalidCalleeIdentity,
  InvalidFormalOrDestinationIdentity,
  InvalidSourcePlaceIdentity,
  ConflictingPatchPayload,
  MalformedPreparedResult,
};

const char *toString(D5GateExclusionReason value);
const char *toString(D5ParityError value);
const char *toString(D5InfrastructureError value);
std::optional<D5InfrastructureError>
parseD5InfrastructureError(const std::string &value);
std::optional<D5ParityError> parseD5ParityError(const std::string &value);

struct D5PreparedPlanSummary {
  std::string TypeProof;
  std::string Transfer;
  std::string Source;
  std::string BoundaryAccess;
  std::string Dependency;
  std::string Spelling;
  std::string LiabilitySource;
  std::string LiabilityTarget;
  size_t EvaluationEntries = 0;
  size_t BoundaryEntries = 0;
  size_t FinalizationEntries = 0;
  std::vector<std::string> NormalizedBoundaryEntries;
  std::vector<std::string> NormalizedFinalizationEntries;
  std::vector<std::string> PatchPayloads;
  std::string RegionTerminal;

  friend bool operator==(const D5PreparedPlanSummary &lhs,
                         const D5PreparedPlanSummary &rhs);
};

D5PreparedPlanSummary summarizeD5PreparedCall(const D3ValidatedCall &call);

struct D5PreparedCallReceipt {
  D3SourceLocation Location;
  std::string Callee;
  std::string FormalIdentity;
  std::string SourceIdentity;
  std::string ActualType;
  std::string FormalType;
  std::string SourceStateBefore;
  std::string PALStateBefore;
  uint64_t SourceInitMask = 0;
  bool DependencyBearingActual = false;
  std::optional<LegacyCedeRequirement> LegacyRequirement;
  D3AdmissionKind PreAdmission = D3AdmissionKind::Rejected;
  D3AdmissionKind PostAdmission = D3AdmissionKind::Rejected;
  std::optional<D5PreparationExclusionReason> PreparationExclusion;
  std::optional<D5PreparationError> PreparationError;
  std::optional<D5ParityError> ParityError;
  std::optional<D5InfrastructureError> InfrastructureError;
  std::optional<D5PreparedPlanSummary> PrePlan;
  std::optional<D5PreparedPlanSummary> PostPlan;
  std::vector<std::string> LegacyDiagnosticCodes;
  size_t FinalLegacyCheckCount = 0;
  bool SameCallStructuralParity = false;
  bool PreFactoryParentUnchanged = false;
  bool PostFactoryParentUnchanged = false;
  std::vector<std::string> PreDifferingParentFields;
  std::vector<std::string> PostDifferingParentFields;
};

class D5PreparedCallAuditSession final {
public:
  void noteGateExclusion(D5GateExclusionReason reason);
  void notePreFactoryInvocation() { ++PreFactoryInvocationCount; }
  void notePostOracleInvocation() { ++PostOracleInvocationCount; }
  void setStrictQualification(bool value) { StrictQualification = value; }
  bool strictQualification() const { return StrictQualification; }
  void
  setInjectedInfrastructureError(std::optional<D5InfrastructureError> value) {
    InjectedInfrastructureError = value;
  }
  std::optional<D5InfrastructureError> injectedInfrastructureError() const {
    return InjectedInfrastructureError;
  }
  std::optional<D5InfrastructureError> takeInjectedInfrastructureError() {
    auto result = InjectedInfrastructureError;
    InjectedInfrastructureError.reset();
    return result;
  }
  void setInjectedParityError(std::optional<D5ParityError> value) {
    InjectedParityError = value;
  }
  std::optional<D5ParityError> takeInjectedParityError() {
    auto result = InjectedParityError;
    InjectedParityError.reset();
    return result;
  }
  void append(D5PreparedCallReceipt receipt);
  void dumpJSON(std::ostream &out) const;

private:
  size_t ConsideredCallCount = 0;
  size_t PreFactoryInvocationCount = 0;
  size_t PostOracleInvocationCount = 0;
  size_t PreparedCount = 0;
  std::map<D5GateExclusionReason, size_t> GateExclusions;
  std::map<D5PreparationExclusionReason, size_t> PreparationExclusions;
  std::map<D5PreparationError, size_t> PreparationErrors;
  std::map<D5ParityError, size_t> ParityErrors;
  std::map<D5InfrastructureError, size_t> InfrastructureErrors;
  std::optional<D5InfrastructureError> InjectedInfrastructureError;
  std::optional<D5ParityError> InjectedParityError;
  bool StrictQualification = false;
  std::vector<D5PreparedCallReceipt> Receipts;
};

} // namespace toka
