// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "toka/SourceLocation.h"
#include <cstddef>
#include <cstdint>
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
  std::string Spelling;
  std::string Transfer;
  std::string SourceDisposition;
  SemanticEvidenceLocation Location;
  SemanticEvidenceLocation ContractLocation;

  bool operator<(const CedeObligationRecord &rhs) const;
  bool operator==(const CedeObligationRecord &rhs) const;
};

struct ExplicitCedeStage0ShadowRecord {
  std::string PlanOrigin;
  std::string SyntaxPurpose;
  std::string SurfaceSpelling;
  std::string SourceCategory;
  std::string ExactPath;
  std::string ReferentRoot;
  std::string ReferentPath;
  std::vector<std::string> DependencyRoots;
  std::string Dependency;
  bool DependencyFactsComplete = false;
  std::string ActualType;
  std::string FormalType;
  std::string FormalContract;
  std::string FormalMorphology;
  bool FormalCapabilitiesComplete = false;
  bool FormalHandleRebindable = false;
  bool FormalPayloadWritable = false;
  bool ActualCapabilitiesComplete = false;
  bool ActualHandleRebindable = false;
  bool ActualPayloadWritable = false;
  std::string Ownership;
  std::string CopyProof;
  std::string Eligibility;
  std::string TemporaryEligibility;
  std::string TypeCompatibility;
  std::string EligibilityContext;
  std::string ObligationBefore;
  std::string Outcome;
  std::string Rejection;
  std::string ValueProduction;
  std::string Source;
  std::string Destination;
  std::string Drop;
  std::string ObligationAction;
  std::string ObligationAfter;
  std::string SourceView;
  std::string Reachability;
  std::string SemanticRoot;

  bool operator<(const ExplicitCedeStage0ShadowRecord &rhs) const;
  bool operator==(const ExplicitCedeStage0ShadowRecord &rhs) const;
};

struct CallTransferShadowRecord {
  std::string Callee;
  std::string Route;
  std::string Parameter;
  unsigned ArgumentIndex = 0;
  unsigned FormalIndex = 0;
  std::string ValueCategory;
  std::string Spelling;
  std::string Transfer;
  std::string Source;
  std::string Dependency;
  std::string PlaceEligibility;
  std::string Drop;
  std::string ExecutionBoundary;
  uint64_t SourceRootID = 0;
  std::string SourcePath;
  std::string SourceIdentity;
  std::string ReferentPath;
  std::string ReferentIdentity;
  std::vector<std::string> DependencyPaths;
  bool HasCleanupMask = false;
  uint64_t CleanupMask = 0;
  bool FormalCeded = false;
  bool FormalInit = false;
  bool ActualInit = false;
  bool LegacyCallerRuleApplied = false;
  bool LegacyCedeExempt = false;
  bool LegacyMissingCede = false;
  bool Async = false;
  ExplicitCedeStage0ShadowRecord Stage0;
  SemanticEvidenceLocation Location;
  SemanticEvidenceLocation ContractLocation;

  bool operator<(const CallTransferShadowRecord &rhs) const;
  bool operator==(const CallTransferShadowRecord &rhs) const;
};

struct CapabilityCallRecord {
  std::string Callee;
  std::string Parameter;
  std::string Subject;
  bool DeclaredHandleRebindable = false;
  bool DeclaredPayloadWritable = false;
  bool InferredHandleRebindable = false;
  bool InferredPayloadWritable = false;
  bool RequestHandleRebind = false;
  bool RequestPayloadWrite = false;
  bool RequiredHandleRebind = false;
  bool RequiredPayloadWrite = false;
  bool GrantedHandleRebind = false;
  bool GrantedPayloadWrite = false;
  bool IndependentCede = false;
  SemanticEvidenceLocation Location;
  SemanticEvidenceLocation ContractLocation;

  bool operator<(const CapabilityCallRecord &rhs) const;
  bool operator==(const CapabilityCallRecord &rhs) const;
};

enum class TodoGoalStatus { Incomplete, Underconstrained, Unsupported };

struct TodoGoalRecord {
  uint64_t Id = 0;
  TodoGoalStatus Status = TodoGoalStatus::Incomplete;
  bool HasContract = false;
  std::string Type;
  std::string Morphology;
  std::string Transfer;
  bool HandleRebind = false;
  bool PayloadWrite = false;
  bool Nullable = false;
  std::vector<std::string> RequiredDependencies;
  SemanticEvidenceLocation Location;

  bool operator<(const TodoGoalRecord &rhs) const;
  bool operator==(const TodoGoalRecord &rhs) const;
};

struct ConditionalFactRecord {
  std::string Symbol;
  std::string Type;
  std::vector<uint64_t> ConditionalOn;
  SemanticEvidenceLocation Location;

  bool operator<(const ConditionalFactRecord &rhs) const;
  bool operator==(const ConditionalFactRecord &rhs) const;
};

struct SemanticEvidenceAuditState {
  bool Enabled = false;
  bool CallTransferShadowEnabled = false;
  size_t DecisionCount = 0;
  size_t CedeObligationCount = 0;
  size_t CallTransferShadowCount = 0;
  size_t CapabilityCount = 0;
  size_t TodoGoalCount = 0;
  size_t ConditionalFactCount = 0;
  std::vector<SemanticDecisionRecord> Decisions;
  std::vector<CedeObligationRecord> CedeObligations;
  std::vector<CallTransferShadowRecord> CallTransferShadows;
  std::vector<CapabilityCallRecord> Capabilities;
  std::vector<TodoGoalRecord> TodoGoals;
  std::vector<ConditionalFactRecord> ConditionalFacts;

  bool operator==(const SemanticEvidenceAuditState &rhs) const {
    return Enabled == rhs.Enabled &&
           CallTransferShadowEnabled == rhs.CallTransferShadowEnabled &&
           DecisionCount == rhs.DecisionCount &&
           CedeObligationCount == rhs.CedeObligationCount &&
           CallTransferShadowCount == rhs.CallTransferShadowCount &&
           CapabilityCount == rhs.CapabilityCount &&
           TodoGoalCount == rhs.TodoGoalCount &&
           ConditionalFactCount == rhs.ConditionalFactCount &&
           Decisions == rhs.Decisions &&
           CedeObligations == rhs.CedeObligations &&
           CallTransferShadows == rhs.CallTransferShadows &&
           Capabilities == rhs.Capabilities && TodoGoals == rhs.TodoGoals &&
           ConditionalFacts == rhs.ConditionalFacts;
  }
  bool operator!=(const SemanticEvidenceAuditState &rhs) const {
    return !(*this == rhs);
  }
};

class SemanticEvidence {
public:
  static constexpr unsigned SchemaVersion = 1;

  static void enable(bool value);
  static bool isEnabled();
  static void enableCallTransferShadow(bool value);
  static bool isCallTransferShadowEnabled();
  static SemanticEvidenceAuditState auditState();
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
                                   SourceLocation contractLocation = {},
                                   std::string spelling = {},
                                   std::string transfer = {},
                                   std::string sourceDisposition = {});
  static void dumpJSON(std::ostream &out);
  static void dumpCedeObligationsJSON(std::ostream &out);
  static void dumpCedeObligationsV2JSON(std::ostream &out);
  static void recordCallTransferShadow(
      std::string callee, std::string route, std::string parameter,
      unsigned argumentIndex, unsigned formalIndex, std::string valueCategory,
      std::string spelling, std::string transfer, std::string source,
      std::string dependency, std::string placeEligibility, std::string drop,
      std::string executionBoundary, uint64_t sourceRootID,
      std::string sourcePath, std::string sourceIdentity,
      std::string referentPath, std::string referentIdentity,
      std::vector<std::string> dependencyPaths, bool hasCleanupMask,
      uint64_t cleanupMask, bool formalCeded, bool formalInit,
      bool actualInit, bool legacyCallerRuleApplied, bool legacyCedeExempt,
      bool legacyMissingCede, bool async,
      ExplicitCedeStage0ShadowRecord stage0, SourceLocation location,
      SourceLocation contractLocation = {});
  static void dumpCallTransferShadowJSON(std::ostream &out);
  static void recordCapabilityCall(
      std::string callee, std::string parameter, std::string subject,
      bool declaredHandleRebindable, bool declaredPayloadWritable,
      bool inferredHandleRebindable, bool inferredPayloadWritable,
      bool requestHandleRebind, bool requestPayloadWrite,
      bool requiredHandleRebind, bool requiredPayloadWrite,
      bool grantedHandleRebind, bool grantedPayloadWrite,
      bool independentCede, SourceLocation location,
      SourceLocation contractLocation = {});
  static void dumpCapabilityCallsJSON(std::ostream &out);
  static void recordTodoGoal(
      uint64_t id, TodoGoalStatus status, bool hasContract, std::string type,
      std::string morphology, std::string transfer, bool handleRebind,
      bool payloadWrite, bool nullable,
      std::vector<std::string> requiredDependencies, SourceLocation location);
  static void dumpTodoGoalsJSON(std::ostream &out);
  static void recordConditionalFact(std::string symbol, std::string type,
                                    std::vector<uint64_t> conditionalOn,
                                    SourceLocation location);
  static void dumpConditionalFactsJSON(std::ostream &out);

private:
  static bool Enabled;
  static bool CallTransferShadowEnabled;
  static std::vector<SemanticDecisionRecord> Records;
  static std::vector<CedeObligationRecord> CedeObligations;
  static std::vector<CallTransferShadowRecord> CallTransferShadows;
  static std::vector<CapabilityCallRecord> CapabilityCalls;
  static std::vector<TodoGoalRecord> TodoGoals;
  static std::vector<ConditionalFactRecord> ConditionalFacts;
};

const char *toString(SemanticRuleID value);
const char *toString(SemanticOperation value);
const char *toString(SemanticDecision value);
const char *toString(SemanticReason value);

} // namespace toka
