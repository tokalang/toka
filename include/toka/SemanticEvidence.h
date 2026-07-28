// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "toka/SourceLocation.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace toka {

enum class SemanticRuleID {
  PALCall001,
  PALBorrow001,
  PALBorrow002,
  PALPath001,
  PALCFG001,
  OwnMove001,
  OwnCede001,
  OwnCede002,
  OwnResource001,
  EffRet001,
  EffMember001,
  EffShape001,
  AsyncEffect001,
  AsyncCapture001,
  AsyncSuspend001,
  TKIReplay001,
  TKICache001,
  UnsafePub001,
  ErrorProp001,
};

enum class SemanticOperation {
  PayloadWrite,
  SharedPayloadBorrow,
  ExclusivePayloadBorrow,
  HandleViewBorrow,
  HandleRebind,
  ExclusiveMutation,
  Invalidation,
  OwnershipTransfer,
  CedeObligation,
  ResourceCopy,
  EscapingDependency,
  MemberDependency,
  EffectConsumption,
  ExecutionBoundaryCapture,
  ExecutionBoundaryArgument,
  InterfaceReplay,
  ErrorPropagation,
};

enum class SemanticDecision {
  Allow,
  Reject,
  ConservativeReject,
};

enum class SemanticReason {
  NoConflict,
  DisjointPaths,
  CompatibleSharedAccess,
  OverlappingExclusiveAccess,
  ActiveExclusiveBorrow,
  ActiveSharedBorrow,
  BorrowedPathInvalidation,
  AlreadyMoved,
  MissingExplicitCede,
  UnconsumedCede,
  CedeConsumed,
  MissingCedeReturn,
  ResourceCopyForbidden,
  MissingReturnDependency,
  LocalEscape,
  LifetimeDepthViolation,
  MemberDependencyMismatch,
  DanglingEffect,
  ImplicitBoundaryCapture,
  BorrowedBoundaryArgument,
  BorrowedBoundaryDependency,
  InterfaceContractApplied,
  UnknownProvenance,
  DirectErrorMatch,
  ExplicitErrorConversion,
  MissingErrorConversion,
  PartialMoveUnsupported,
};

struct SemanticEvidenceLocation {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;

  bool operator<(const SemanticEvidenceLocation &rhs) const;
  bool operator==(const SemanticEvidenceLocation &rhs) const;
};

struct SemanticDecisionRecord {
  SemanticRuleID Rule = SemanticRuleID::PALPath001;
  SemanticOperation Operation = SemanticOperation::SharedPayloadBorrow;
  SemanticDecision Decision = SemanticDecision::Allow;
  SemanticReason Reason = SemanticReason::NoConflict;
  std::string Subject;
  std::string Origin;
  SemanticEvidenceLocation PrimaryLocation;
  SemanticEvidenceLocation OriginLocation;

  bool operator<(const SemanticDecisionRecord &rhs) const;
  bool operator==(const SemanticDecisionRecord &rhs) const;
};

enum class CedeObligationStage { CallerTransfer, CalleeConsumption, ReturnTransfer };
enum class CedeObligationStatus { Fulfilled, Violated };

struct CedeObligationRecord {
  CedeObligationStage Stage = CedeObligationStage::CallerTransfer;
  CedeObligationStatus Status = CedeObligationStatus::Fulfilled;
  SemanticReason Reason = SemanticReason::CedeConsumed;
  std::string Subject;
  std::string Origin;
  SemanticEvidenceLocation Location;
  SemanticEvidenceLocation ContractLocation;

  bool operator<(const CedeObligationRecord &rhs) const;
  bool operator==(const CedeObligationRecord &rhs) const;
};

class SemanticEvidence {
public:
  static constexpr unsigned SchemaVersion = 1;

  static void enable(bool value);
  static bool isEnabled();
  static void reset();
  static void record(SemanticRuleID rule, SemanticOperation operation,
                     SemanticDecision decision, SemanticReason reason,
                     std::string subject, std::string origin,
                     SourceLocation primaryLoc,
                     SourceLocation originLoc = {});
  static void recordCedeObligation(CedeObligationStage stage,
                                   CedeObligationStatus status,
                                   SemanticReason reason,
                                   std::string subject, std::string origin,
                                   SourceLocation location,
                                   SourceLocation contractLocation = {});
  static void dumpJSON(std::ostream &out);
  static void dumpCedeObligationsJSON(std::ostream &out);

private:
  static bool Enabled;
  static std::vector<SemanticDecisionRecord> Records;
  static std::vector<CedeObligationRecord> CedeObligations;
};

const char *toString(SemanticRuleID value);
const char *toString(SemanticOperation value);
const char *toString(SemanticDecision value);
const char *toString(SemanticReason value);

} // namespace toka
