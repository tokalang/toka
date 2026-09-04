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
#include <map>
#include <set>
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
  std::string DeclaredFormalMorphology;
  std::string FormalMorphology;
  std::string FormalOwnership;
  std::string FormalTransferClass;
  std::string FormalContractOrigin;
  bool FormalDeclarationFactsComplete = false;
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
  std::string DestinationObligationAction;
  std::string DestinationObligationAfter;
  std::string SourceView;
  std::string Reachability;
  std::string SemanticRoot;

  bool operator<(const ExplicitCedeStage0ShadowRecord &rhs) const;
  bool operator==(const ExplicitCedeStage0ShadowRecord &rhs) const;
};

struct CallTransferShadowRecord {
  std::string Callee;
  std::string SpecializationIdentity;
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

struct ExplicitCedeStage0TransactionItemRecord {
  std::string Role;
  unsigned Index = 0;
  unsigned FormalIndex = 0;
  std::string ActualType;
  std::string FormalType;
  std::string FormalContract;
  std::string Outcome;
  std::string Rejection;
  std::string ExactPath;
  std::string ReferentPath;
  std::vector<std::string> DependencyRoots;
  std::string SourceView;
  std::string SurfaceSpelling;
  std::string CopyProof;
  std::string Eligibility;
  std::string ObligationBefore;
  std::string Reachability;
  std::string SourceLiveness;
  bool HasInitMask = false;
  uint64_t InitMask = 0;
  bool HasCleanupMask = false;
  uint64_t CleanupMask = 0;
  std::string LiabilityIdentity;
  std::string ValueProduction;
  std::string Source;
  std::string Destination;
  std::string Drop;
  std::string SourceObligationAction;
  std::string SourceObligationAfter;
  std::string DestinationObligationAction;
  std::string DestinationObligationAfter;

  bool operator<(const ExplicitCedeStage0TransactionItemRecord &rhs) const;
  bool operator==(const ExplicitCedeStage0TransactionItemRecord &rhs) const;
};

struct ExplicitCedeStage0TransactionRecord {
  std::string Callee;
  std::string SpecializationIdentity;
  std::string Route;
  std::string Outcome;
  std::string Rejection;
  bool LocalPlanAdmitted = false;
  bool CommitAllowed = false;
  bool ArityComplete = false;
  bool ValidationComplete = false;
  bool PreparedBeforeLegacyMutation = false;
  bool HasReceiver = false;
  uint64_t SnapshotRevision = 0;
  uint64_t PALRevision = 0;
  unsigned ExpectedArgumentCount = 0;
  unsigned ActualArgumentCount = 0;
  unsigned ArgumentCount = 0;
  std::vector<ExplicitCedeStage0TransactionItemRecord> Items;
  SemanticEvidenceLocation Location;

  bool operator<(const ExplicitCedeStage0TransactionRecord &rhs) const;
  bool operator==(const ExplicitCedeStage0TransactionRecord &rhs) const;
};

struct ExplicitCedeStage0NonCallRecord {
  std::string Boundary;
  std::string GroupIdentity;
  std::string Edge;
  unsigned EdgeIndex = 0;
  std::string GroupOutcome;
  std::string GroupRejection;
  bool GroupPlanAdmitted = false;
  std::string PlanOrigin;
  std::string SyntaxPurpose;
  std::string SourceCategory;
  std::string Dependency;
  std::string TypeCompatibility;
  std::string EligibilityContext;
  std::string DestinationExactPath;
  std::string DestinationView;
  std::string DestinationReachability;
  std::string DestinationMorphology;
  bool DestinationCapabilitiesComplete = false;
  bool DestinationHandleRebindable = false;
  bool DestinationPayloadWritable = false;
  bool DestinationFlowCeilingComplete = false;
  bool DestinationFlowHandleRebindable = false;
  bool DestinationFlowPayloadWritable = false;
  bool SourceFlowCeilingComplete = false;
  bool SourceFlowHandleRebindable = false;
  bool SourceFlowPayloadWritable = false;
  bool PreparedBeforeLegacyMutation = false;
  uint64_t SnapshotRevision = 0;
  ExplicitCedeStage0TransactionItemRecord Plan;
  SemanticEvidenceLocation Location;

  bool operator<(const ExplicitCedeStage0NonCallRecord &rhs) const;
  bool operator==(const ExplicitCedeStage0NonCallRecord &rhs) const;
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
  bool GenericBodyCallQualificationEnabled = false;
  bool NonCallTransferShadowEnabled = false;
  size_t DecisionCount = 0;
  size_t CedeObligationCount = 0;
  size_t CallTransferShadowCount = 0;
  size_t ExplicitCedeStage0TransactionCount = 0;
  size_t ExplicitCedeStage0NonCallCount = 0;
  size_t CapabilityCount = 0;
  size_t TodoGoalCount = 0;
  size_t ConditionalFactCount = 0;
  std::vector<SemanticDecisionRecord> Decisions;
  std::vector<CedeObligationRecord> CedeObligations;
  std::vector<CallTransferShadowRecord> CallTransferShadows;
  std::vector<ExplicitCedeStage0TransactionRecord>
      ExplicitCedeStage0Transactions;
  std::vector<ExplicitCedeStage0NonCallRecord> ExplicitCedeStage0NonCalls;
  std::vector<CapabilityCallRecord> Capabilities;
  std::vector<TodoGoalRecord> TodoGoals;
  std::vector<ConditionalFactRecord> ConditionalFacts;

  bool operator==(const SemanticEvidenceAuditState &rhs) const {
    return Enabled == rhs.Enabled &&
           CallTransferShadowEnabled == rhs.CallTransferShadowEnabled &&
           GenericBodyCallQualificationEnabled ==
               rhs.GenericBodyCallQualificationEnabled &&
           NonCallTransferShadowEnabled == rhs.NonCallTransferShadowEnabled &&
           DecisionCount == rhs.DecisionCount &&
           CedeObligationCount == rhs.CedeObligationCount &&
           CallTransferShadowCount == rhs.CallTransferShadowCount &&
           ExplicitCedeStage0TransactionCount ==
               rhs.ExplicitCedeStage0TransactionCount &&
           ExplicitCedeStage0NonCallCount ==
               rhs.ExplicitCedeStage0NonCallCount &&
           CapabilityCount == rhs.CapabilityCount &&
           TodoGoalCount == rhs.TodoGoalCount &&
           ConditionalFactCount == rhs.ConditionalFactCount &&
           Decisions == rhs.Decisions &&
           CedeObligations == rhs.CedeObligations &&
           CallTransferShadows == rhs.CallTransferShadows &&
           ExplicitCedeStage0Transactions ==
               rhs.ExplicitCedeStage0Transactions &&
           ExplicitCedeStage0NonCalls == rhs.ExplicitCedeStage0NonCalls &&
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

  struct CallTransferJournalCheckpoint {
    size_t ShadowCount = 0;
    size_t TransactionCount = 0;
    size_t NonCallCount = 0;
  };
  struct NonCallGroupToken {
    size_t Begin = 0;
    std::string Identity;
    bool Valid = false;
  };

  static void enable(bool value);
  static bool isEnabled();
  static void enableCallTransferShadow(bool value);
  static bool isCallTransferShadowEnabled();
  static void enableGenericBodyCallQualification(bool value);
  static bool isGenericBodyCallQualificationEnabled();
  static void rollbackGenericBodyCallQualification(
      const std::string &specializationIdentity);
  static void recordGenericBodyCallQualificationDependency(
      const std::string &parentIdentity, const std::string &childIdentity);
  static std::vector<std::string> promoteGenericBodyCallQualification(
      const std::string &specializationIdentity);
  static void enableNonCallTransferShadow(bool value);
  static bool isNonCallTransferShadowEnabled();
  static CallTransferJournalCheckpoint checkpointCallTransferJournal();
  static void
  rollbackCallTransferJournal(CallTransferJournalCheckpoint checkpoint);
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
      std::string callee, std::string specializationIdentity, std::string route,
      std::string parameter, unsigned argumentIndex, unsigned formalIndex,
      std::string valueCategory, std::string spelling, std::string transfer,
      std::string source, std::string dependency, std::string placeEligibility,
      std::string drop, std::string executionBoundary, uint64_t sourceRootID,
      std::string sourcePath, std::string sourceIdentity,
      std::string referentPath, std::string referentIdentity,
      std::vector<std::string> dependencyPaths, bool hasCleanupMask,
      uint64_t cleanupMask, bool formalCeded, bool formalInit, bool actualInit,
      bool legacyCallerRuleApplied, bool legacyCedeExempt,
      bool legacyMissingCede, bool async, ExplicitCedeStage0ShadowRecord stage0,
      SourceLocation location, SourceLocation contractLocation = {});
  static void dumpCallTransferShadowJSON(std::ostream &out);
  static void recordExplicitCedeStage0Transaction(
      ExplicitCedeStage0TransactionRecord record, SourceLocation location);
  static void
  recordExplicitCedeStage0NonCall(ExplicitCedeStage0NonCallRecord record,
                                  SourceLocation location);
  static NonCallGroupToken
  beginExplicitCedeStage0NonCallGroup(const std::string &groupIdentity);
  static void
  finalizeExplicitCedeStage0NonCallGroup(const NonCallGroupToken &token,
                                         std::string outcome,
                                         std::string rejection, bool admitted);
  static void dumpExplicitCedeStage0NonCallJSON(std::ostream &out);
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
  static bool GenericBodyCallQualificationEnabled;
  static bool NonCallTransferShadowEnabled;
  static std::vector<SemanticDecisionRecord> Records;
  static std::vector<CedeObligationRecord> CedeObligations;
  static std::vector<CallTransferShadowRecord> CallTransferShadows;
  static std::vector<CallTransferShadowRecord> GenericBodyCallTransferShadows;
  static std::vector<CallTransferShadowRecord>
      PendingGenericBodyCallTransferShadows;
  static std::vector<ExplicitCedeStage0TransactionRecord>
      ExplicitCedeStage0Transactions;
  static std::vector<ExplicitCedeStage0TransactionRecord>
      GenericBodyExplicitCedeStage0Transactions;
  static std::vector<ExplicitCedeStage0TransactionRecord>
      PendingGenericBodyExplicitCedeStage0Transactions;
  static std::set<std::string> GenericBodyQualifiedSpecializations;
  static std::map<std::string, std::set<std::string>>
      GenericBodyQualificationDependencies;
  static std::vector<ExplicitCedeStage0NonCallRecord>
      ExplicitCedeStage0NonCalls;
  static std::vector<CapabilityCallRecord> CapabilityCalls;
  static std::vector<TodoGoalRecord> TodoGoals;
  static std::vector<ConditionalFactRecord> ConditionalFacts;
};

const char *toString(SemanticRuleID value);
const char *toString(SemanticOperation value);
const char *toString(SemanticDecision value);
const char *toString(SemanticReason value);

} // namespace toka
