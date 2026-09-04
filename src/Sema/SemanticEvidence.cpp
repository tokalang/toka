// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/SemanticEvidence.h"
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include <algorithm>
#include <ostream>
#include <tuple>

namespace toka {

bool SemanticEvidence::Enabled = false;
bool SemanticEvidence::CallTransferShadowEnabled = false;
bool SemanticEvidence::GenericBodyCallQualificationEnabled = false;
bool SemanticEvidence::NonCallTransferShadowEnabled = false;
std::vector<SemanticDecisionRecord> SemanticEvidence::Records;
std::vector<CedeObligationRecord> SemanticEvidence::CedeObligations;
std::vector<CallTransferShadowRecord> SemanticEvidence::CallTransferShadows;
std::vector<CallTransferShadowRecord>
    SemanticEvidence::GenericBodyCallTransferShadows;
std::vector<ExplicitCedeStage0TransactionRecord>
    SemanticEvidence::ExplicitCedeStage0Transactions;
std::vector<ExplicitCedeStage0TransactionRecord>
    SemanticEvidence::GenericBodyExplicitCedeStage0Transactions;
std::set<std::string> SemanticEvidence::GenericBodyQualifiedSpecializations;
std::vector<ExplicitCedeStage0NonCallRecord>
    SemanticEvidence::ExplicitCedeStage0NonCalls;
std::vector<CapabilityCallRecord> SemanticEvidence::CapabilityCalls;
std::vector<TodoGoalRecord> SemanticEvidence::TodoGoals;
std::vector<ConditionalFactRecord> SemanticEvidence::ConditionalFacts;

namespace {

SemanticEvidenceLocation resolveLocation(SourceLocation loc) {
  if (!loc.isValid() || !DiagnosticEngine::SrcMgr)
    return {};
  FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc);
  if (!full.isValid())
    return {};
  return {full.FileName, full.Line, full.Column};
}

std::string escapeJSON(const std::string &value) {
  std::string result;
  for (unsigned char ch : value) {
    switch (ch) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (ch >= 0x20)
        result += static_cast<char>(ch);
      break;
    }
  }
  return result;
}

void dumpLocation(std::ostream &out, const SemanticEvidenceLocation &loc) {
  out << "{\"file\":\"" << escapeJSON(loc.File) << "\",\"line\":"
      << loc.Line << ",\"column\":" << loc.Column << "}";
}

} // namespace

bool SemanticEvidenceLocation::operator<(
    const SemanticEvidenceLocation &rhs) const {
  return std::tie(File, Line, Column) <
         std::tie(rhs.File, rhs.Line, rhs.Column);
}

bool SemanticEvidenceLocation::operator==(
    const SemanticEvidenceLocation &rhs) const {
  return File == rhs.File && Line == rhs.Line && Column == rhs.Column;
}

bool SemanticDecisionRecord::operator<(
    const SemanticDecisionRecord &rhs) const {
  return std::tie(Rule, Operation, Decision, Reason, Subject, Origin,
                  PrimaryLocation, OriginLocation) <
         std::tie(rhs.Rule, rhs.Operation, rhs.Decision, rhs.Reason,
                  rhs.Subject, rhs.Origin, rhs.PrimaryLocation,
                  rhs.OriginLocation);
}

bool SemanticDecisionRecord::operator==(
    const SemanticDecisionRecord &rhs) const {
  return Rule == rhs.Rule && Operation == rhs.Operation &&
         Decision == rhs.Decision && Reason == rhs.Reason &&
         Subject == rhs.Subject && Origin == rhs.Origin &&
         PrimaryLocation == rhs.PrimaryLocation &&
         OriginLocation == rhs.OriginLocation;
}

bool CedeObligationRecord::operator<(const CedeObligationRecord &rhs) const {
  return std::tie(Stage, Status, Reason, Subject, Origin, Spelling, Transfer,
                  SourceDisposition, Location, ContractLocation) <
         std::tie(rhs.Stage, rhs.Status, rhs.Reason, rhs.Subject, rhs.Origin,
                  rhs.Spelling, rhs.Transfer, rhs.SourceDisposition,
                  rhs.Location, rhs.ContractLocation);
}

bool CedeObligationRecord::operator==(const CedeObligationRecord &rhs) const {
  return Stage == rhs.Stage && Status == rhs.Status && Reason == rhs.Reason &&
         Subject == rhs.Subject && Origin == rhs.Origin &&
         Spelling == rhs.Spelling && Transfer == rhs.Transfer &&
         SourceDisposition == rhs.SourceDisposition &&
         Location == rhs.Location && ContractLocation == rhs.ContractLocation;
}

bool ExplicitCedeStage0ShadowRecord::operator<(
    const ExplicitCedeStage0ShadowRecord &rhs) const {
  return std::tie(
             PlanOrigin, SyntaxPurpose, SurfaceSpelling, SourceCategory,
             ExactPath, ReferentRoot, ReferentPath, DependencyRoots,
             Dependency, DependencyFactsComplete, ActualType, FormalType,
             FormalContract, DeclaredFormalMorphology, FormalMorphology,
             FormalOwnership,
             FormalTransferClass, FormalContractOrigin,
             FormalDeclarationFactsComplete,
             FormalCapabilitiesComplete, FormalHandleRebindable,
             FormalPayloadWritable, ActualCapabilitiesComplete,
             ActualHandleRebindable, ActualPayloadWritable, Ownership,
             CopyProof, Eligibility, TemporaryEligibility, TypeCompatibility,
             EligibilityContext, ObligationBefore, Outcome, Rejection, ValueProduction,
             Source, Destination, Drop, ObligationAction, ObligationAfter,
             DestinationObligationAction, DestinationObligationAfter,
             SourceView, Reachability, SemanticRoot) <
         std::tie(
             rhs.PlanOrigin, rhs.SyntaxPurpose, rhs.SurfaceSpelling,
             rhs.SourceCategory, rhs.ExactPath, rhs.ReferentRoot,
             rhs.ReferentPath, rhs.DependencyRoots, rhs.Dependency,
             rhs.DependencyFactsComplete, rhs.ActualType, rhs.FormalType,
             rhs.FormalContract, rhs.DeclaredFormalMorphology,
             rhs.FormalMorphology,
             rhs.FormalOwnership, rhs.FormalTransferClass,
             rhs.FormalContractOrigin,
             rhs.FormalDeclarationFactsComplete,
             rhs.FormalCapabilitiesComplete, rhs.FormalHandleRebindable,
             rhs.FormalPayloadWritable, rhs.ActualCapabilitiesComplete,
             rhs.ActualHandleRebindable, rhs.ActualPayloadWritable,
             rhs.Ownership, rhs.CopyProof, rhs.Eligibility,
             rhs.TemporaryEligibility, rhs.TypeCompatibility,
             rhs.EligibilityContext, rhs.ObligationBefore, rhs.Outcome,
             rhs.Rejection, rhs.ValueProduction, rhs.Source, rhs.Destination,
             rhs.Drop, rhs.ObligationAction, rhs.ObligationAfter,
             rhs.DestinationObligationAction,
             rhs.DestinationObligationAfter,
             rhs.SourceView, rhs.Reachability, rhs.SemanticRoot);
}

bool ExplicitCedeStage0ShadowRecord::operator==(
    const ExplicitCedeStage0ShadowRecord &rhs) const {
  return !(*this < rhs) && !(rhs < *this);
}

bool CallTransferShadowRecord::operator<(
    const CallTransferShadowRecord &rhs) const {
  return std::tie(Callee, SpecializationIdentity, Route, Parameter,
                  ArgumentIndex, FormalIndex, ValueCategory, Spelling, Transfer,
                  Source, Dependency, PlaceEligibility, Drop, ExecutionBoundary,
                  SourceRootID, SourcePath, SourceIdentity, ReferentPath,
                  ReferentIdentity, DependencyPaths, HasCleanupMask,
                  CleanupMask, FormalCeded, FormalInit, ActualInit,
                  LegacyCallerRuleApplied, LegacyCedeExempt, LegacyMissingCede,
                  Async, Stage0, Location, ContractLocation) <
         std::tie(rhs.Callee, rhs.SpecializationIdentity, rhs.Route,
                  rhs.Parameter, rhs.ArgumentIndex, rhs.FormalIndex,
                  rhs.ValueCategory, rhs.Spelling, rhs.Transfer, rhs.Source,
                  rhs.Dependency, rhs.PlaceEligibility, rhs.Drop,
                  rhs.ExecutionBoundary, rhs.SourceRootID, rhs.SourcePath,
                  rhs.SourceIdentity, rhs.ReferentPath, rhs.ReferentIdentity,
                  rhs.DependencyPaths, rhs.HasCleanupMask, rhs.CleanupMask,
                  rhs.FormalCeded, rhs.FormalInit, rhs.ActualInit,
                  rhs.LegacyCallerRuleApplied, rhs.LegacyCedeExempt,
                  rhs.LegacyMissingCede, rhs.Async, rhs.Stage0, rhs.Location,
                  rhs.ContractLocation);
}

bool CallTransferShadowRecord::operator==(
    const CallTransferShadowRecord &rhs) const {
  return Callee == rhs.Callee &&
         SpecializationIdentity == rhs.SpecializationIdentity &&
         Route == rhs.Route && Parameter == rhs.Parameter &&
         ArgumentIndex == rhs.ArgumentIndex && FormalIndex == rhs.FormalIndex &&
         ValueCategory == rhs.ValueCategory && Spelling == rhs.Spelling &&
         Transfer == rhs.Transfer && Source == rhs.Source &&
         Dependency == rhs.Dependency &&
         PlaceEligibility == rhs.PlaceEligibility && Drop == rhs.Drop &&
         ExecutionBoundary == rhs.ExecutionBoundary &&
         SourceRootID == rhs.SourceRootID && SourcePath == rhs.SourcePath &&
         SourceIdentity == rhs.SourceIdentity &&
         ReferentPath == rhs.ReferentPath &&
         ReferentIdentity == rhs.ReferentIdentity &&
         DependencyPaths == rhs.DependencyPaths &&
         HasCleanupMask == rhs.HasCleanupMask &&
         CleanupMask == rhs.CleanupMask && FormalCeded == rhs.FormalCeded &&
         FormalInit == rhs.FormalInit && ActualInit == rhs.ActualInit &&
         LegacyCallerRuleApplied == rhs.LegacyCallerRuleApplied &&
         LegacyCedeExempt == rhs.LegacyCedeExempt &&
         LegacyMissingCede == rhs.LegacyMissingCede && Async == rhs.Async &&
         Stage0 == rhs.Stage0 && Location == rhs.Location &&
         ContractLocation == rhs.ContractLocation;
}

bool ExplicitCedeStage0TransactionItemRecord::operator<(
    const ExplicitCedeStage0TransactionItemRecord &rhs) const {
  return std::tie(Role, Index, FormalIndex, ActualType, FormalType,
                  FormalContract, Outcome, Rejection, ExactPath, ReferentPath,
                  DependencyRoots, SourceView, SurfaceSpelling, CopyProof, Eligibility,
                  ObligationBefore, Reachability, SourceLiveness, HasInitMask,
                  InitMask, HasCleanupMask, CleanupMask, LiabilityIdentity,
                  ValueProduction, Source, Destination, Drop,
                  SourceObligationAction, SourceObligationAfter,
                  DestinationObligationAction, DestinationObligationAfter) <
         std::tie(rhs.Role, rhs.Index, rhs.FormalIndex, rhs.ActualType,
                  rhs.FormalType, rhs.FormalContract, rhs.Outcome,
                  rhs.Rejection, rhs.ExactPath, rhs.ReferentPath,
                  rhs.DependencyRoots, rhs.SourceView, rhs.SurfaceSpelling,
                  rhs.CopyProof,
                  rhs.Eligibility, rhs.ObligationBefore, rhs.Reachability,
                  rhs.SourceLiveness, rhs.HasInitMask, rhs.InitMask,
                  rhs.HasCleanupMask, rhs.CleanupMask, rhs.LiabilityIdentity,
                  rhs.ValueProduction, rhs.Source, rhs.Destination, rhs.Drop,
                  rhs.SourceObligationAction, rhs.SourceObligationAfter,
                  rhs.DestinationObligationAction,
                  rhs.DestinationObligationAfter);
}

bool ExplicitCedeStage0TransactionItemRecord::operator==(
    const ExplicitCedeStage0TransactionItemRecord &rhs) const {
  return !(*this < rhs) && !(rhs < *this);
}

bool ExplicitCedeStage0TransactionRecord::operator<(
    const ExplicitCedeStage0TransactionRecord &rhs) const {
  return std::tie(Callee, SpecializationIdentity, Route, Outcome, Rejection,
                  LocalPlanAdmitted, CommitAllowed, ArityComplete,
                  ValidationComplete, PreparedBeforeLegacyMutation, HasReceiver,
                  SnapshotRevision, PALRevision, ExpectedArgumentCount,
                  ActualArgumentCount, ArgumentCount, Items, Location) <
         std::tie(rhs.Callee, rhs.SpecializationIdentity, rhs.Route,
                  rhs.Outcome, rhs.Rejection, rhs.LocalPlanAdmitted,
                  rhs.CommitAllowed, rhs.ArityComplete, rhs.ValidationComplete,
                  rhs.PreparedBeforeLegacyMutation, rhs.HasReceiver,
                  rhs.SnapshotRevision, rhs.PALRevision,
                  rhs.ExpectedArgumentCount, rhs.ActualArgumentCount,
                  rhs.ArgumentCount, rhs.Items, rhs.Location);
}

bool ExplicitCedeStage0TransactionRecord::operator==(
    const ExplicitCedeStage0TransactionRecord &rhs) const {
  return !(*this < rhs) && !(rhs < *this);
}

bool ExplicitCedeStage0NonCallRecord::operator<(
    const ExplicitCedeStage0NonCallRecord &rhs) const {
  return std::tie(
             Boundary, GroupIdentity, Edge, EdgeIndex, GroupOutcome,
             GroupRejection, GroupPlanAdmitted, PlanOrigin, SyntaxPurpose,
             SourceCategory, Dependency, TypeCompatibility, EligibilityContext,
             DestinationExactPath, DestinationView, DestinationReachability,
             DestinationMorphology, DestinationCapabilitiesComplete,
             DestinationHandleRebindable, DestinationPayloadWritable,
             DestinationFlowCeilingComplete, DestinationFlowHandleRebindable,
             DestinationFlowPayloadWritable, SourceFlowCeilingComplete,
             SourceFlowHandleRebindable, SourceFlowPayloadWritable,
             PreparedBeforeLegacyMutation, SnapshotRevision, Plan, Location) <
         std::tie(
             rhs.Boundary, rhs.GroupIdentity, rhs.Edge, rhs.EdgeIndex,
             rhs.GroupOutcome, rhs.GroupRejection, rhs.GroupPlanAdmitted,
             rhs.PlanOrigin, rhs.SyntaxPurpose, rhs.SourceCategory,
             rhs.Dependency, rhs.TypeCompatibility, rhs.EligibilityContext,
             rhs.DestinationExactPath, rhs.DestinationView,
             rhs.DestinationReachability, rhs.DestinationMorphology,
             rhs.DestinationCapabilitiesComplete,
             rhs.DestinationHandleRebindable, rhs.DestinationPayloadWritable,
             rhs.DestinationFlowCeilingComplete,
             rhs.DestinationFlowHandleRebindable,
             rhs.DestinationFlowPayloadWritable, rhs.SourceFlowCeilingComplete,
             rhs.SourceFlowHandleRebindable, rhs.SourceFlowPayloadWritable,
             rhs.PreparedBeforeLegacyMutation, rhs.SnapshotRevision, rhs.Plan,
             rhs.Location);
}

bool ExplicitCedeStage0NonCallRecord::operator==(
    const ExplicitCedeStage0NonCallRecord &rhs) const {
  return !(*this < rhs) && !(rhs < *this);
}

bool CapabilityCallRecord::operator<(const CapabilityCallRecord &rhs) const {
  return std::tie(Callee, Parameter, Subject, DeclaredHandleRebindable,
                  DeclaredPayloadWritable, InferredHandleRebindable,
                  InferredPayloadWritable, RequestHandleRebind,
                  RequestPayloadWrite, RequiredHandleRebind,
                  RequiredPayloadWrite, GrantedHandleRebind,
                  GrantedPayloadWrite, IndependentCede, Location,
                  ContractLocation) <
         std::tie(rhs.Callee, rhs.Parameter, rhs.Subject,
                  rhs.DeclaredHandleRebindable, rhs.DeclaredPayloadWritable,
                  rhs.InferredHandleRebindable, rhs.InferredPayloadWritable,
                  rhs.RequestHandleRebind, rhs.RequestPayloadWrite,
                  rhs.RequiredHandleRebind, rhs.RequiredPayloadWrite,
                  rhs.GrantedHandleRebind, rhs.GrantedPayloadWrite,
                  rhs.IndependentCede, rhs.Location, rhs.ContractLocation);
}

bool CapabilityCallRecord::operator==(const CapabilityCallRecord &rhs) const {
  return Callee == rhs.Callee && Parameter == rhs.Parameter &&
         Subject == rhs.Subject &&
         DeclaredHandleRebindable == rhs.DeclaredHandleRebindable &&
         DeclaredPayloadWritable == rhs.DeclaredPayloadWritable &&
         InferredHandleRebindable == rhs.InferredHandleRebindable &&
         InferredPayloadWritable == rhs.InferredPayloadWritable &&
         RequestHandleRebind == rhs.RequestHandleRebind &&
         RequestPayloadWrite == rhs.RequestPayloadWrite &&
         RequiredHandleRebind == rhs.RequiredHandleRebind &&
         RequiredPayloadWrite == rhs.RequiredPayloadWrite &&
         GrantedHandleRebind == rhs.GrantedHandleRebind &&
         GrantedPayloadWrite == rhs.GrantedPayloadWrite &&
         IndependentCede == rhs.IndependentCede && Location == rhs.Location &&
         ContractLocation == rhs.ContractLocation;
}

bool TodoGoalRecord::operator<(const TodoGoalRecord &rhs) const {
  return std::tie(Id, Status, HasContract, Type, Morphology, Transfer,
                  HandleRebind, PayloadWrite, Nullable, RequiredDependencies,
                  Location) <
         std::tie(rhs.Id, rhs.Status, rhs.HasContract, rhs.Type,
                  rhs.Morphology, rhs.Transfer, rhs.HandleRebind,
                  rhs.PayloadWrite, rhs.Nullable, rhs.RequiredDependencies,
                  rhs.Location);
}

bool TodoGoalRecord::operator==(const TodoGoalRecord &rhs) const {
  return Id == rhs.Id && Status == rhs.Status &&
         HasContract == rhs.HasContract && Type == rhs.Type &&
         Morphology == rhs.Morphology && Transfer == rhs.Transfer &&
         HandleRebind == rhs.HandleRebind &&
         PayloadWrite == rhs.PayloadWrite && Nullable == rhs.Nullable &&
         RequiredDependencies == rhs.RequiredDependencies &&
         Location == rhs.Location;
}

bool ConditionalFactRecord::operator<(
    const ConditionalFactRecord &rhs) const {
  return std::tie(Symbol, Type, ConditionalOn, Location) <
         std::tie(rhs.Symbol, rhs.Type, rhs.ConditionalOn, rhs.Location);
}

bool ConditionalFactRecord::operator==(
    const ConditionalFactRecord &rhs) const {
  return Symbol == rhs.Symbol && Type == rhs.Type &&
         ConditionalOn == rhs.ConditionalOn && Location == rhs.Location;
}

void SemanticEvidence::enable(bool value) {
  Enabled = value;
  CallTransferShadowEnabled = false;
  GenericBodyCallQualificationEnabled = false;
  reset();
}

bool SemanticEvidence::isEnabled() { return Enabled; }

void SemanticEvidence::enableCallTransferShadow(bool value) {
  CallTransferShadowEnabled = value;
}

bool SemanticEvidence::isCallTransferShadowEnabled() {
  return CallTransferShadowEnabled;
}

void SemanticEvidence::enableGenericBodyCallQualification(bool value) {
  GenericBodyCallQualificationEnabled = value;
}

bool SemanticEvidence::isGenericBodyCallQualificationEnabled() {
  return GenericBodyCallQualificationEnabled;
}

void SemanticEvidence::rollbackGenericBodyCallQualification(
    const std::string &specializationIdentity) {
  if (specializationIdentity.empty())
    return;
  GenericBodyCallTransferShadows.erase(
      std::remove_if(GenericBodyCallTransferShadows.begin(),
                     GenericBodyCallTransferShadows.end(),
                     [&](const auto &record) {
                       return record.SpecializationIdentity ==
                              specializationIdentity;
                     }),
      GenericBodyCallTransferShadows.end());
  GenericBodyExplicitCedeStage0Transactions.erase(
      std::remove_if(GenericBodyExplicitCedeStage0Transactions.begin(),
                     GenericBodyExplicitCedeStage0Transactions.end(),
                     [&](const auto &record) {
                       return record.SpecializationIdentity ==
                              specializationIdentity;
                     }),
      GenericBodyExplicitCedeStage0Transactions.end());
  GenericBodyQualifiedSpecializations.erase(specializationIdentity);
}

void SemanticEvidence::recordGenericBodyCallQualification(
    const std::string &specializationIdentity) {
  if (Enabled && GenericBodyCallQualificationEnabled &&
      !specializationIdentity.empty())
    GenericBodyQualifiedSpecializations.insert(specializationIdentity);
}

void SemanticEvidence::enableNonCallTransferShadow(bool value) {
  NonCallTransferShadowEnabled = value;
}

bool SemanticEvidence::isNonCallTransferShadowEnabled() {
  return NonCallTransferShadowEnabled;
}

SemanticEvidence::CallTransferJournalCheckpoint
SemanticEvidence::checkpointCallTransferJournal() {
  return {CallTransferShadows.size(), ExplicitCedeStage0Transactions.size(),
          ExplicitCedeStage0NonCalls.size()};
}

void SemanticEvidence::rollbackCallTransferJournal(
    CallTransferJournalCheckpoint checkpoint) {
  if (checkpoint.ShadowCount < CallTransferShadows.size())
    CallTransferShadows.resize(checkpoint.ShadowCount);
  if (checkpoint.TransactionCount < ExplicitCedeStage0Transactions.size())
    ExplicitCedeStage0Transactions.resize(checkpoint.TransactionCount);
  if (checkpoint.NonCallCount < ExplicitCedeStage0NonCalls.size())
    ExplicitCedeStage0NonCalls.resize(checkpoint.NonCallCount);
}

SemanticEvidenceAuditState SemanticEvidence::auditState() {
  return {Enabled,
          CallTransferShadowEnabled,
          GenericBodyCallQualificationEnabled,
          NonCallTransferShadowEnabled,
          Records.size(),
          CedeObligations.size(),
          CallTransferShadows.size(),
          ExplicitCedeStage0Transactions.size(),
          ExplicitCedeStage0NonCalls.size(),
          CapabilityCalls.size(),
          TodoGoals.size(),
          ConditionalFacts.size(),
          Records,
          CedeObligations,
          CallTransferShadows,
          ExplicitCedeStage0Transactions,
          ExplicitCedeStage0NonCalls,
          CapabilityCalls,
          TodoGoals,
          ConditionalFacts};
}

void SemanticEvidence::reset() {
  Records.clear();
  CedeObligations.clear();
  CallTransferShadows.clear();
  GenericBodyCallTransferShadows.clear();
  ExplicitCedeStage0Transactions.clear();
  GenericBodyExplicitCedeStage0Transactions.clear();
  GenericBodyQualifiedSpecializations.clear();
  ExplicitCedeStage0NonCalls.clear();
  CapabilityCalls.clear();
  TodoGoals.clear();
  ConditionalFacts.clear();
}

void SemanticEvidence::record(SemanticRuleID rule,
                              SemanticOperation operation,
                              SemanticDecision decision,
                              SemanticReason reason, std::string subject,
                              std::string origin, SourceLocation primaryLoc,
                              SourceLocation originLoc) {
  if (!Enabled)
    return;
  Records.push_back({rule, operation, decision, reason, std::move(subject),
                     std::move(origin), resolveLocation(primaryLoc),
                     resolveLocation(originLoc)});
}

void SemanticEvidence::recordCedeObligation(
    CedeObligationStage stage, CedeObligationStatus status,
    SemanticReason reason, std::string subject, std::string origin,
    SourceLocation location, SourceLocation contractLocation,
    std::string spelling, std::string transfer,
    std::string sourceDisposition) {
  if (!Enabled)
    return;
  CedeObligations.push_back(
      {stage, status, reason, std::move(subject), std::move(origin),
       std::move(spelling), std::move(transfer),
       std::move(sourceDisposition), resolveLocation(location),
       resolveLocation(contractLocation)});
}

void SemanticEvidence::dumpJSON(std::ostream &out) {
  std::sort(Records.begin(), Records.end());
  Records.erase(std::unique(Records.begin(), Records.end()), Records.end());
  out << "{\"schema\":\"toka.semantic-evidence\",\"version\":"
      << SchemaVersion << ",\"records\":[";
  for (size_t i = 0; i < Records.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = Records[i];
    out << "{\"rule\":\"" << toString(record.Rule)
        << "\",\"operation\":\"" << toString(record.Operation)
        << "\",\"decision\":\"" << toString(record.Decision)
        << "\",\"reason\":\"" << toString(record.Reason)
        << "\",\"subject\":\"" << escapeJSON(record.Subject)
        << "\",\"origin\":\"" << escapeJSON(record.Origin)
        << "\",\"primary_location\":";
    dumpLocation(out, record.PrimaryLocation);
    out << ",\"origin_location\":";
    dumpLocation(out, record.OriginLocation);
    out << '}';
  }
  out << "]}\n";
}

namespace {
const char *cedeStageName(CedeObligationStage stage) {
  switch (stage) {
  case CedeObligationStage::CallerTransfer: return "caller-transfer";
  case CedeObligationStage::CalleeConsumption: return "callee-consumption";
  case CedeObligationStage::ReturnTransfer: return "return-transfer";
  }
  return "caller-transfer";
}

const char *cedeStatusName(CedeObligationStatus status) {
  switch (status) {
  case CedeObligationStatus::Fulfilled: return "fulfilled";
  case CedeObligationStatus::Violated: return "violated";
  }
  return "violated";
}
} // namespace

void SemanticEvidence::dumpCedeObligationsJSON(std::ostream &out) {
  std::sort(CedeObligations.begin(), CedeObligations.end());
  CedeObligations.erase(
      std::unique(CedeObligations.begin(), CedeObligations.end()),
      CedeObligations.end());
  out << "{\"schema\":\"toka.cede-obligation-evidence\",\"version\":1,\"records\":[";
  for (size_t i = 0; i < CedeObligations.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = CedeObligations[i];
    out << "{\"stage\":\"" << cedeStageName(record.Stage)
        << "\",\"status\":\"" << cedeStatusName(record.Status)
        << "\",\"reason\":\"" << toString(record.Reason)
        << "\",\"subject\":\"" << escapeJSON(record.Subject)
        << "\",\"origin\":\"" << escapeJSON(record.Origin)
        << "\",\"location\":";
    dumpLocation(out, record.Location);
    out << ",\"contract_location\":";
    dumpLocation(out, record.ContractLocation);
    out << '}';
  }
  out << "]}\n";
}

void SemanticEvidence::dumpCedeObligationsV2JSON(std::ostream &out) {
  std::sort(CedeObligations.begin(), CedeObligations.end());
  CedeObligations.erase(
      std::unique(CedeObligations.begin(), CedeObligations.end()),
      CedeObligations.end());
  out << "{\"schema\":\"toka.cede-obligation-evidence\",\"version\":2,\"records\":[";
  for (size_t i = 0; i < CedeObligations.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = CedeObligations[i];
    out << "{\"stage\":\"" << cedeStageName(record.Stage)
        << "\",\"status\":\"" << cedeStatusName(record.Status)
        << "\",\"reason\":\"" << toString(record.Reason)
        << "\",\"subject\":\"" << escapeJSON(record.Subject)
        << "\",\"origin\":\"" << escapeJSON(record.Origin) << "\"";
    if (record.Stage == CedeObligationStage::CallerTransfer) {
      out << ",\"spelling\":\"" << escapeJSON(record.Spelling)
          << "\",\"transfer\":\"" << escapeJSON(record.Transfer)
          << "\",\"source\":\""
          << escapeJSON(record.SourceDisposition) << "\"";
    } else {
      out << ",\"spelling\":null,\"transfer\":null,\"source\":null";
    }
    out << ",\"location\":";
    dumpLocation(out, record.Location);
    out << ",\"contract_location\":";
    dumpLocation(out, record.ContractLocation);
    out << '}';
  }
  out << "]}\n";
}

void SemanticEvidence::recordCallTransferShadow(
    std::string callee, std::string specializationIdentity, std::string route,
    std::string parameter, unsigned argumentIndex, unsigned formalIndex,
    std::string valueCategory, std::string spelling, std::string transfer,
    std::string source, std::string dependency, std::string placeEligibility,
    std::string drop, std::string executionBoundary, uint64_t sourceRootID,
    std::string sourcePath, std::string sourceIdentity,
    std::string referentPath, std::string referentIdentity,
    std::vector<std::string> dependencyPaths, bool hasCleanupMask,
    uint64_t cleanupMask, bool formalCeded, bool formalInit, bool actualInit,
    bool legacyCallerRuleApplied, bool legacyCedeExempt, bool legacyMissingCede,
    bool async, ExplicitCedeStage0ShadowRecord stage0, SourceLocation location,
    SourceLocation contractLocation) {
  if (!Enabled || !CallTransferShadowEnabled)
    return;
  CallTransferShadowRecord record{std::move(callee),
                                  std::move(specializationIdentity),
                                  std::move(route),
                                  std::move(parameter),
                                  argumentIndex,
                                  formalIndex,
                                  std::move(valueCategory),
                                  std::move(spelling),
                                  std::move(transfer),
                                  std::move(source),
                                  std::move(dependency),
                                  std::move(placeEligibility),
                                  std::move(drop),
                                  std::move(executionBoundary),
                                  sourceRootID,
                                  std::move(sourcePath),
                                  std::move(sourceIdentity),
                                  std::move(referentPath),
                                  std::move(referentIdentity),
                                  std::move(dependencyPaths),
                                  hasCleanupMask,
                                  cleanupMask,
                                  formalCeded,
                                  formalInit,
                                  actualInit,
                                  legacyCallerRuleApplied,
                                  legacyCedeExempt,
                                  legacyMissingCede,
                                  async,
                                  std::move(stage0),
                                  resolveLocation(location),
                                  resolveLocation(contractLocation)};
  if (GenericBodyCallQualificationEnabled &&
      !record.SpecializationIdentity.empty())
    GenericBodyCallTransferShadows.push_back(std::move(record));
  else
    CallTransferShadows.push_back(std::move(record));
}

void SemanticEvidence::recordExplicitCedeStage0Transaction(
    ExplicitCedeStage0TransactionRecord record, SourceLocation location) {
  if (!Enabled || !CallTransferShadowEnabled)
    return;
  record.Location = resolveLocation(location);
  if (GenericBodyCallQualificationEnabled &&
      !record.SpecializationIdentity.empty())
    GenericBodyExplicitCedeStage0Transactions.push_back(std::move(record));
  else
    ExplicitCedeStage0Transactions.push_back(std::move(record));
}

void SemanticEvidence::recordExplicitCedeStage0NonCall(
    ExplicitCedeStage0NonCallRecord record, SourceLocation location) {
  if (!Enabled || !NonCallTransferShadowEnabled)
    return;
  record.Location = resolveLocation(location);
  ExplicitCedeStage0NonCalls.push_back(std::move(record));
}

SemanticEvidence::NonCallGroupToken
SemanticEvidence::beginExplicitCedeStage0NonCallGroup(
    const std::string &groupIdentity) {
  return {ExplicitCedeStage0NonCalls.size(), groupIdentity,
          !groupIdentity.empty()};
}

void SemanticEvidence::finalizeExplicitCedeStage0NonCallGroup(
    const NonCallGroupToken &token, std::string outcome, std::string rejection,
    bool admitted) {
  if (!Enabled || !NonCallTransferShadowEnabled)
    return;
  const size_t begin = std::min(token.Begin, ExplicitCedeStage0NonCalls.size());
  for (size_t index = begin; index < ExplicitCedeStage0NonCalls.size();
       ++index) {
    auto &record = ExplicitCedeStage0NonCalls[index];
    if (!token.Valid || record.GroupIdentity != token.Identity) {
      record.GroupOutcome = "Rejected";
      record.GroupRejection = "IncompleteFacts";
      record.GroupPlanAdmitted = false;
      continue;
    }
    record.GroupOutcome = outcome;
    record.GroupRejection = rejection;
    record.GroupPlanAdmitted = admitted;
  }
}

void SemanticEvidence::dumpCallTransferShadowJSON(std::ostream &out) {
  if (GenericBodyCallQualificationEnabled) {
    CallTransferShadows.insert(CallTransferShadows.end(),
                               GenericBodyCallTransferShadows.begin(),
                               GenericBodyCallTransferShadows.end());
    ExplicitCedeStage0Transactions.insert(
        ExplicitCedeStage0Transactions.end(),
        GenericBodyExplicitCedeStage0Transactions.begin(),
        GenericBodyExplicitCedeStage0Transactions.end());
    GenericBodyCallTransferShadows.clear();
    GenericBodyExplicitCedeStage0Transactions.clear();
  }
  std::sort(CallTransferShadows.begin(), CallTransferShadows.end());
  CallTransferShadows.erase(
      std::unique(CallTransferShadows.begin(), CallTransferShadows.end()),
      CallTransferShadows.end());
  std::sort(ExplicitCedeStage0Transactions.begin(),
            ExplicitCedeStage0Transactions.end());
  ExplicitCedeStage0Transactions.erase(
      std::unique(ExplicitCedeStage0Transactions.begin(),
                  ExplicitCedeStage0Transactions.end()),
      ExplicitCedeStage0Transactions.end());
  const bool genericBodyQualification = GenericBodyCallQualificationEnabled;
  out << "{\"schema\":\""
      << (genericBodyQualification
              ? "toka.internal.generic-body-call-qualification"
              : "toka.internal.call-transfer-shadow")
      << "\",\"version\":" << (genericBodyQualification ? 1 : 5)
      << ",\"status\":\"audit-only\",\"records\":[";
  for (size_t i = 0; i < CallTransferShadows.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = CallTransferShadows[i];
    out << "{\"callee\":\"" << escapeJSON(record.Callee) << "\"";
    if (genericBodyQualification)
      out << ",\"specialization_identity\":\""
          << escapeJSON(record.SpecializationIdentity) << "\"";
    out << ",\"route\":\"" << escapeJSON(record.Route) << "\",\"parameter\":\""
        << escapeJSON(record.Parameter)
        << "\",\"argument_index\":" << record.ArgumentIndex
        << ",\"formal_index\":" << record.FormalIndex
        << ",\"value_category\":\"" << escapeJSON(record.ValueCategory)
        << "\",\"spelling\":\"" << escapeJSON(record.Spelling)
        << "\",\"transfer\":\"" << escapeJSON(record.Transfer)
        << "\",\"source\":\"" << escapeJSON(record.Source)
        << "\",\"dependency\":\"" << escapeJSON(record.Dependency)
        << "\",\"place_eligibility\":\"" << escapeJSON(record.PlaceEligibility)
        << "\",\"drop\":\"" << escapeJSON(record.Drop)
        << "\",\"execution_boundary\":\""
        << escapeJSON(record.ExecutionBoundary)
        << "\",\"source_root_id\":" << record.SourceRootID
        << ",\"source_path\":\"" << escapeJSON(record.SourcePath)
        << "\",\"source_identity\":\"" << escapeJSON(record.SourceIdentity)
        << "\",\"referent_path\":\"" << escapeJSON(record.ReferentPath)
        << "\",\"referent_identity\":\"" << escapeJSON(record.ReferentIdentity)
        << "\",\"dependency_paths\":[";
    for (size_t dependency = 0; dependency < record.DependencyPaths.size();
         ++dependency) {
      if (dependency != 0)
        out << ',';
      out << "\"" << escapeJSON(record.DependencyPaths[dependency]) << "\"";
    }
    out << "],\"cleanup_mask\":";
    if (record.HasCleanupMask)
      out << record.CleanupMask;
    else
      out << "null";
    out
        << ",\"formal_ceded\":"
        << (record.FormalCeded ? "true" : "false")
        << ",\"formal_init\":" << (record.FormalInit ? "true" : "false")
        << ",\"actual_init\":" << (record.ActualInit ? "true" : "false")
        << ",\"legacy_caller_rule_applied\":"
        << (record.LegacyCallerRuleApplied ? "true" : "false")
        << ",\"legacy_cede_exempt\":"
        << (record.LegacyCedeExempt ? "true" : "false")
        << ",\"legacy_missing_cede\":"
        << (record.LegacyMissingCede ? "true" : "false")
        << ",\"async\":" << (record.Async ? "true" : "false")
        << ",\"stage0\":{\"plan_origin\":\""
        << escapeJSON(record.Stage0.PlanOrigin) << "\",\"syntax_purpose\":\""
        << escapeJSON(record.Stage0.SyntaxPurpose)
        << "\",\"surface_spelling\":\""
        << escapeJSON(record.Stage0.SurfaceSpelling)
        << "\",\"source_category\":\""
        << escapeJSON(record.Stage0.SourceCategory)
        << "\",\"exact_path\":\"" << escapeJSON(record.Stage0.ExactPath)
        << "\",\"referent_root\":\""
        << escapeJSON(record.Stage0.ReferentRoot)
        << "\",\"referent_path\":\""
        << escapeJSON(record.Stage0.ReferentPath)
        << "\",\"dependency_roots\":[";
    for (size_t dependency = 0;
         dependency < record.Stage0.DependencyRoots.size(); ++dependency) {
      if (dependency != 0)
        out << ',';
      out << "\"" << escapeJSON(record.Stage0.DependencyRoots[dependency])
          << "\"";
    }
    out << "],\"dependency\":\"" << escapeJSON(record.Stage0.Dependency)
        << "\",\"dependency_complete\":"
        << (record.Stage0.DependencyFactsComplete ? "true" : "false")
        << ",\"actual_type\":\"" << escapeJSON(record.Stage0.ActualType)
        << "\",\"formal_type\":\"" << escapeJSON(record.Stage0.FormalType)
        << "\",\"formal_contract\":\""
        << escapeJSON(record.Stage0.FormalContract)
        << "\",\"declared_formal_morphology\":\""
        << escapeJSON(record.Stage0.DeclaredFormalMorphology)
        << "\",\"formal_morphology\":\""
        << escapeJSON(record.Stage0.FormalMorphology)
        << "\",\"formal_ownership\":\""
        << escapeJSON(record.Stage0.FormalOwnership)
        << "\",\"formal_transfer_class\":\""
        << escapeJSON(record.Stage0.FormalTransferClass)
        << "\",\"formal_contract_origin\":\""
        << escapeJSON(record.Stage0.FormalContractOrigin)
        << "\",\"formal_declaration_complete\":"
        << (record.Stage0.FormalDeclarationFactsComplete ? "true" : "false")
        << ",\"formal_capabilities\":{\"complete\":"
        << (record.Stage0.FormalCapabilitiesComplete ? "true" : "false")
        << ",\"handle_rebind\":"
        << (record.Stage0.FormalHandleRebindable ? "true" : "false")
        << ",\"payload_write\":"
        << (record.Stage0.FormalPayloadWritable ? "true" : "false")
        << "},\"actual_capabilities\":{\"complete\":"
        << (record.Stage0.ActualCapabilitiesComplete ? "true" : "false")
        << ",\"handle_rebind\":"
        << (record.Stage0.ActualHandleRebindable ? "true" : "false")
        << ",\"payload_write\":"
        << (record.Stage0.ActualPayloadWritable ? "true" : "false")
        << "},\"ownership\":\"" << escapeJSON(record.Stage0.Ownership)
        << "\",\"copy_proof\":\"" << escapeJSON(record.Stage0.CopyProof)
        << "\",\"eligibility\":\""
        << escapeJSON(record.Stage0.Eligibility)
        << "\",\"temporary_eligibility\":\""
        << escapeJSON(record.Stage0.TemporaryEligibility)
        << "\",\"type_compatibility\":\""
        << escapeJSON(record.Stage0.TypeCompatibility)
        << "\",\"eligibility_context\":\""
        << escapeJSON(record.Stage0.EligibilityContext)
        << "\",\"obligation_before\":\""
        << escapeJSON(record.Stage0.ObligationBefore) << "\",\"outcome\":\""
        << escapeJSON(record.Stage0.Outcome) << "\",\"rejection\":\""
        << escapeJSON(record.Stage0.Rejection)
        << "\",\"value_production\":\""
        << escapeJSON(record.Stage0.ValueProduction) << "\",\"source\":\""
        << escapeJSON(record.Stage0.Source) << "\",\"destination\":\""
        << escapeJSON(record.Stage0.Destination) << "\",\"drop\":\""
        << escapeJSON(record.Stage0.Drop)
        << "\",\"source_obligation_action\":\""
        << escapeJSON(record.Stage0.ObligationAction)
        << "\",\"source_obligation_after\":\""
        << escapeJSON(record.Stage0.ObligationAfter)
        << "\",\"destination_obligation_action\":\""
        << escapeJSON(record.Stage0.DestinationObligationAction)
        << "\",\"destination_obligation_after\":\""
        << escapeJSON(record.Stage0.DestinationObligationAfter)
        << "\",\"source_view\":\"" << escapeJSON(record.Stage0.SourceView)
        << "\",\"reachability\":\""
        << escapeJSON(record.Stage0.Reachability)
        << "\",\"semantic_root\":\""
        << escapeJSON(record.Stage0.SemanticRoot) << "\"}"
        << ",\"location\":";
    dumpLocation(out, record.Location);
    out << ",\"contract_location\":";
    dumpLocation(out, record.ContractLocation);
    out << '}';
  }
  out << "],\"transactions\":[";
  for (size_t i = 0; i < ExplicitCedeStage0Transactions.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &transaction = ExplicitCedeStage0Transactions[i];
    out << "{\"callee\":\"" << escapeJSON(transaction.Callee) << "\"";
    if (genericBodyQualification)
      out << ",\"specialization_identity\":\""
          << escapeJSON(transaction.SpecializationIdentity) << "\"";
    out << ",\"route\":\"" << escapeJSON(transaction.Route)
        << "\",\"outcome\":\"" << escapeJSON(transaction.Outcome)
        << "\",\"rejection\":\"" << escapeJSON(transaction.Rejection)
        << "\",\"local_plan_admitted\":"
        << (transaction.LocalPlanAdmitted ? "true" : "false")
        << ",\"commit_allowed\":"
        << (transaction.CommitAllowed ? "true" : "false")
        << ",\"arity_complete\":"
        << (transaction.ArityComplete ? "true" : "false")
        << ",\"validation_complete\":"
        << (transaction.ValidationComplete ? "true" : "false")
        << ",\"prepared_before_legacy_mutation\":"
        << (transaction.PreparedBeforeLegacyMutation ? "true" : "false")
        << ",\"has_receiver\":" << (transaction.HasReceiver ? "true" : "false")
        << ",\"snapshot_revision\":" << transaction.SnapshotRevision
        << ",\"pal_revision\":" << transaction.PALRevision
        << ",\"expected_argument_count\":" << transaction.ExpectedArgumentCount
        << ",\"actual_argument_count\":" << transaction.ActualArgumentCount
        << ",\"argument_count\":" << transaction.ArgumentCount
        << ",\"items\":[";
    for (size_t itemIndex = 0; itemIndex < transaction.Items.size();
         ++itemIndex) {
      if (itemIndex != 0)
        out << ',';
      const auto &item = transaction.Items[itemIndex];
      out << "{\"role\":\"" << escapeJSON(item.Role)
          << "\",\"index\":" << item.Index << ",\"formal_index\":"
          << item.FormalIndex << ",\"actual_type\":\""
          << escapeJSON(item.ActualType) << "\",\"formal_type\":\""
          << escapeJSON(item.FormalType)
          << "\",\"formal_contract\":\""
          << escapeJSON(item.FormalContract) << "\",\"outcome\":\""
          << escapeJSON(item.Outcome) << "\",\"rejection\":\""
          << escapeJSON(item.Rejection) << "\",\"exact_path\":\""
          << escapeJSON(item.ExactPath) << "\",\"referent_path\":\""
          << escapeJSON(item.ReferentPath) << "\",\"dependency_roots\":[";
      for (size_t dependency = 0; dependency < item.DependencyRoots.size();
           ++dependency) {
        if (dependency != 0)
          out << ',';
        out << "\"" << escapeJSON(item.DependencyRoots[dependency]) << "\"";
      }
      out << "],\"source_view\":\"" << escapeJSON(item.SourceView)
          << "\",\"surface_spelling\":\""
          << escapeJSON(item.SurfaceSpelling)
          << "\",\"copy_proof\":\"" << escapeJSON(item.CopyProof)
          << "\",\"eligibility\":\"" << escapeJSON(item.Eligibility)
          << "\",\"obligation_before\":\""
          << escapeJSON(item.ObligationBefore)
          << "\",\"reachability\":\"" << escapeJSON(item.Reachability)
          << "\",\"source_liveness\":\""
          << escapeJSON(item.SourceLiveness) << "\",\"init_mask\":";
      if (item.HasInitMask)
        out << item.InitMask;
      else
        out << "null";
      out << ",\"cleanup_mask\":";
      if (item.HasCleanupMask)
        out << item.CleanupMask;
      else
        out << "null";
      out << ",\"liability_identity\":\""
          << escapeJSON(item.LiabilityIdentity)
          << "\",\"value_production\":\""
          << escapeJSON(item.ValueProduction) << "\",\"source\":\""
          << escapeJSON(item.Source) << "\",\"destination\":\""
          << escapeJSON(item.Destination) << "\",\"drop\":\""
          << escapeJSON(item.Drop)
          << "\",\"source_obligation_action\":\""
          << escapeJSON(item.SourceObligationAction)
          << "\",\"source_obligation_after\":\""
          << escapeJSON(item.SourceObligationAfter)
          << "\",\"destination_obligation_action\":\""
          << escapeJSON(item.DestinationObligationAction)
          << "\",\"destination_obligation_after\":\""
          << escapeJSON(item.DestinationObligationAfter) << "\"}";
    }
    out << "],\"location\":";
    dumpLocation(out, transaction.Location);
    out << '}';
  }
  out << ']';
  if (genericBodyQualification) {
    out << ",\"specializations\":[";
    size_t specializationIndex = 0;
    for (const auto &identity : GenericBodyQualifiedSpecializations) {
      if (specializationIndex++ != 0)
        out << ',';
      const auto receiptCount =
          std::count_if(CallTransferShadows.begin(), CallTransferShadows.end(),
                        [&](const auto &record) {
                          return record.SpecializationIdentity == identity;
                        });
      const auto transactionCount = std::count_if(
          ExplicitCedeStage0Transactions.begin(),
          ExplicitCedeStage0Transactions.end(), [&](const auto &record) {
            return record.SpecializationIdentity == identity;
          });
      out << "{\"specialization_identity\":\"" << escapeJSON(identity)
          << "\",\"validation\":\"Valid\",\"qualification_complete\":true"
          << ",\"receipt_count\":" << receiptCount
          << ",\"transaction_count\":" << transactionCount << '}';
    }
    out << ']';
  }
  out << "}\n";
}

void SemanticEvidence::dumpExplicitCedeStage0NonCallJSON(std::ostream &out) {
  std::sort(ExplicitCedeStage0NonCalls.begin(),
            ExplicitCedeStage0NonCalls.end());
  ExplicitCedeStage0NonCalls.erase(
      std::unique(ExplicitCedeStage0NonCalls.begin(),
                  ExplicitCedeStage0NonCalls.end()),
      ExplicitCedeStage0NonCalls.end());
  out << "{\"schema\":\"toka.internal.non-call-transfer-shadow\","
         "\"version\":1,\"status\":\"audit-only\",\"records\":[";
  for (size_t index = 0; index < ExplicitCedeStage0NonCalls.size(); ++index) {
    if (index != 0)
      out << ',';
    const auto &record = ExplicitCedeStage0NonCalls[index];
    const auto &plan = record.Plan;
    out << "{\"boundary\":\"" << escapeJSON(record.Boundary)
        << "\",\"group_identity\":\"" << escapeJSON(record.GroupIdentity)
        << "\",\"edge\":\"" << escapeJSON(record.Edge)
        << "\",\"edge_index\":" << record.EdgeIndex << ",\"group_outcome\":\""
        << escapeJSON(record.GroupOutcome) << "\",\"group_rejection\":\""
        << escapeJSON(record.GroupRejection) << "\",\"group_plan_admitted\":"
        << (record.GroupPlanAdmitted ? "true" : "false")
        << ",\"plan_origin\":\"" << escapeJSON(record.PlanOrigin)
        << "\",\"syntax_purpose\":\"" << escapeJSON(record.SyntaxPurpose)
        << "\",\"source_category\":\"" << escapeJSON(record.SourceCategory)
        << "\",\"dependency\":\"" << escapeJSON(record.Dependency)
        << "\",\"type_compatibility\":\""
        << escapeJSON(record.TypeCompatibility)
        << "\",\"eligibility_context\":\""
        << escapeJSON(record.EligibilityContext)
        << "\",\"destination_exact_path\":\""
        << escapeJSON(record.DestinationExactPath)
        << "\",\"destination_view\":\"" << escapeJSON(record.DestinationView)
        << "\",\"destination_reachability\":\""
        << escapeJSON(record.DestinationReachability)
        << "\",\"destination_morphology\":\""
        << escapeJSON(record.DestinationMorphology)
        << "\",\"destination_capabilities\":{\"complete\":"
        << (record.DestinationCapabilitiesComplete ? "true" : "false")
        << ",\"handle_rebind\":"
        << (record.DestinationHandleRebindable ? "true" : "false")
        << ",\"payload_write\":"
        << (record.DestinationPayloadWritable ? "true" : "false")
        << "},\"destination_flow_ceiling\":{\"complete\":"
        << (record.DestinationFlowCeilingComplete ? "true" : "false")
        << ",\"handle_rebind\":"
        << (record.DestinationFlowHandleRebindable ? "true" : "false")
        << ",\"payload_write\":"
        << (record.DestinationFlowPayloadWritable ? "true" : "false")
        << "},\"source_flow_ceiling\":{\"complete\":"
        << (record.SourceFlowCeilingComplete ? "true" : "false")
        << ",\"handle_rebind\":"
        << (record.SourceFlowHandleRebindable ? "true" : "false")
        << ",\"payload_write\":"
        << (record.SourceFlowPayloadWritable ? "true" : "false")
        << "},\"prepared_before_legacy_mutation\":"
        << (record.PreparedBeforeLegacyMutation ? "true" : "false")
        << ",\"snapshot_revision\":" << record.SnapshotRevision
        << ",\"plan\":{\"actual_type\":\"" << escapeJSON(plan.ActualType)
        << "\",\"formal_type\":\"" << escapeJSON(plan.FormalType)
        << "\",\"formal_contract\":\"" << escapeJSON(plan.FormalContract)
        << "\",\"outcome\":\"" << escapeJSON(plan.Outcome)
        << "\",\"rejection\":\"" << escapeJSON(plan.Rejection)
        << "\",\"exact_path\":\"" << escapeJSON(plan.ExactPath)
        << "\",\"referent_path\":\"" << escapeJSON(plan.ReferentPath)
        << "\",\"dependency_roots\":[";
    for (size_t dependency = 0; dependency < plan.DependencyRoots.size();
         ++dependency) {
      if (dependency != 0)
        out << ',';
      out << "\"" << escapeJSON(plan.DependencyRoots[dependency]) << "\"";
    }
    out << "],\"source_view\":\"" << escapeJSON(plan.SourceView)
        << "\",\"surface_spelling\":\"" << escapeJSON(plan.SurfaceSpelling)
        << "\",\"copy_proof\":\"" << escapeJSON(plan.CopyProof)
        << "\",\"eligibility\":\"" << escapeJSON(plan.Eligibility)
        << "\",\"obligation_before\":\"" << escapeJSON(plan.ObligationBefore)
        << "\",\"reachability\":\"" << escapeJSON(plan.Reachability)
        << "\",\"source_liveness\":\"" << escapeJSON(plan.SourceLiveness)
        << "\",\"init_mask\":";
    if (plan.HasInitMask)
      out << plan.InitMask;
    else
      out << "null";
    out << ",\"cleanup_mask\":";
    if (plan.HasCleanupMask)
      out << plan.CleanupMask;
    else
      out << "null";
    out << ",\"liability_identity\":\"" << escapeJSON(plan.LiabilityIdentity)
        << "\",\"value_production\":\"" << escapeJSON(plan.ValueProduction)
        << "\",\"source\":\"" << escapeJSON(plan.Source)
        << "\",\"destination\":\"" << escapeJSON(plan.Destination)
        << "\",\"drop\":\"" << escapeJSON(plan.Drop)
        << "\",\"source_obligation_action\":\""
        << escapeJSON(plan.SourceObligationAction)
        << "\",\"source_obligation_after\":\""
        << escapeJSON(plan.SourceObligationAfter)
        << "\",\"destination_obligation_action\":\""
        << escapeJSON(plan.DestinationObligationAction)
        << "\",\"destination_obligation_after\":\""
        << escapeJSON(plan.DestinationObligationAfter) << "\"},\"location\":";
    dumpLocation(out, record.Location);
    out << '}';
  }
  out << "]}\n";
}

void SemanticEvidence::recordCapabilityCall(
    std::string callee, std::string parameter, std::string subject,
    bool declaredHandleRebindable, bool declaredPayloadWritable,
    bool inferredHandleRebindable, bool inferredPayloadWritable,
    bool requestHandleRebind, bool requestPayloadWrite,
    bool requiredHandleRebind, bool requiredPayloadWrite,
    bool grantedHandleRebind, bool grantedPayloadWrite, bool independentCede,
    SourceLocation location, SourceLocation contractLocation) {
  if (!Enabled)
    return;
  CapabilityCalls.push_back(
      {std::move(callee), std::move(parameter), std::move(subject),
       declaredHandleRebindable, declaredPayloadWritable,
       inferredHandleRebindable, inferredPayloadWritable, requestHandleRebind,
       requestPayloadWrite, requiredHandleRebind, requiredPayloadWrite,
       grantedHandleRebind, grantedPayloadWrite, independentCede,
       resolveLocation(location), resolveLocation(contractLocation)});
}

void SemanticEvidence::dumpCapabilityCallsJSON(std::ostream &out) {
  std::sort(CapabilityCalls.begin(), CapabilityCalls.end());
  CapabilityCalls.erase(
      std::unique(CapabilityCalls.begin(), CapabilityCalls.end()),
      CapabilityCalls.end());
  out << "{\"schema\":\"toka.capability-pilot\",\"version\":1,\"records\":[";
  for (size_t i = 0; i < CapabilityCalls.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = CapabilityCalls[i];
    auto dumpCapability = [&](bool handle, bool payload) {
      out << "{\"handle_rebind\":" << (handle ? "true" : "false")
          << ",\"payload_write\":" << (payload ? "true" : "false") << '}';
    };
    out << "{\"callee\":\"" << escapeJSON(record.Callee)
        << "\",\"parameter\":\"" << escapeJSON(record.Parameter)
        << "\",\"subject\":\"" << escapeJSON(record.Subject)
        << "\",\"declared\":";
    dumpCapability(record.DeclaredHandleRebindable,
                   record.DeclaredPayloadWritable);
    out << ",\"inferred\":";
    dumpCapability(record.InferredHandleRebindable,
                   record.InferredPayloadWritable);
    out << ",\"request\":";
    dumpCapability(record.RequestHandleRebind, record.RequestPayloadWrite);
    out << ",\"required\":";
    dumpCapability(record.RequiredHandleRebind, record.RequiredPayloadWrite);
    out << ",\"granted\":";
    dumpCapability(record.GrantedHandleRebind, record.GrantedPayloadWrite);
    out << ",\"independent_cede\":"
        << (record.IndependentCede ? "true" : "false")
        << ",\"location\":";
    dumpLocation(out, record.Location);
    out << ",\"contract_location\":";
    dumpLocation(out, record.ContractLocation);
    out << '}';
  }
  out << "]}\n";
}

namespace {
const char *todoStatusName(TodoGoalStatus status) {
  switch (status) {
  case TodoGoalStatus::Incomplete: return "incomplete";
  case TodoGoalStatus::Underconstrained: return "underconstrained";
  case TodoGoalStatus::Unsupported: return "unsupported";
  }
  return "unsupported";
}
} // namespace

void SemanticEvidence::recordTodoGoal(
    uint64_t id, TodoGoalStatus status, bool hasContract, std::string type,
    std::string morphology, std::string transfer, bool handleRebind,
    bool payloadWrite, bool nullable,
    std::vector<std::string> requiredDependencies, SourceLocation location) {
  if (!Enabled)
    return;
  TodoGoals.push_back(
      {id, status, hasContract, std::move(type), std::move(morphology),
       std::move(transfer), handleRebind, payloadWrite, nullable,
       std::move(requiredDependencies), resolveLocation(location)});
}

void SemanticEvidence::dumpTodoGoalsJSON(std::ostream &out) {
  std::sort(TodoGoals.begin(), TodoGoals.end());
  TodoGoals.erase(std::unique(TodoGoals.begin(), TodoGoals.end()),
                  TodoGoals.end());
  out << "{\"schema\":\"toka.todo-goals\",\"version\":1,\"goals\":[";
  for (size_t i = 0; i < TodoGoals.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &goal = TodoGoals[i];
    out << "{\"id\":" << goal.Id << ",\"location\":";
    dumpLocation(out, goal.Location);
    out << ",\"status\":\"" << todoStatusName(goal.Status)
        << "\",\"contract\":";
    if (!goal.HasContract) {
      out << "null";
    } else {
      out << "{\"type\":\"" << escapeJSON(goal.Type)
          << "\",\"morphology\":\"" << escapeJSON(goal.Morphology)
          << "\",\"transfer\":\"" << escapeJSON(goal.Transfer)
          << "\",\"permissions\":{\"handle_rebind\":"
          << (goal.HandleRebind ? "true" : "false")
          << ",\"payload_write\":"
          << (goal.PayloadWrite ? "true" : "false")
          << "},\"nullable\":" << (goal.Nullable ? "true" : "false")
          << ",\"required_dependencies\":[";
      for (size_t dependency = 0;
           dependency < goal.RequiredDependencies.size(); ++dependency) {
        if (dependency != 0)
          out << ',';
        out << "\"" << escapeJSON(goal.RequiredDependencies[dependency])
            << "\"";
      }
      out << "]}";
    }
    out << '}';
  }
  out << "]}\n";
}

void SemanticEvidence::recordConditionalFact(
    std::string symbol, std::string type, std::vector<uint64_t> conditionalOn,
    SourceLocation location) {
  if (!Enabled || conditionalOn.empty())
    return;
  std::sort(conditionalOn.begin(), conditionalOn.end());
  conditionalOn.erase(
      std::unique(conditionalOn.begin(), conditionalOn.end()),
      conditionalOn.end());
  ConditionalFacts.push_back({std::move(symbol), std::move(type),
                              std::move(conditionalOn),
                              resolveLocation(location)});
}

void SemanticEvidence::dumpConditionalFactsJSON(std::ostream &out) {
  std::sort(ConditionalFacts.begin(), ConditionalFacts.end());
  ConditionalFacts.erase(
      std::unique(ConditionalFacts.begin(), ConditionalFacts.end()),
      ConditionalFacts.end());
  out << "{\"schema\":\"toka.conditional-facts\",\"version\":1,\"facts\":[";
  for (size_t i = 0; i < ConditionalFacts.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &fact = ConditionalFacts[i];
    out << "{\"symbol\":\"" << escapeJSON(fact.Symbol)
        << "\",\"type\":\"" << escapeJSON(fact.Type)
        << "\",\"status\":\"conditional\",\"conditional_on\":[";
    for (size_t dependency = 0; dependency < fact.ConditionalOn.size();
         ++dependency) {
      if (dependency != 0)
        out << ',';
      out << fact.ConditionalOn[dependency];
    }
    out << "],\"location\":";
    dumpLocation(out, fact.Location);
    out << '}';
  }
  out << "]}\n";
}

const char *toString(SemanticRuleID value) {
  switch (value) {
  case SemanticRuleID::PALCall001: return "PAL-CALL-001";
  case SemanticRuleID::PALBorrow001: return "PAL-BORROW-001";
  case SemanticRuleID::PALBorrow002: return "PAL-BORROW-002";
  case SemanticRuleID::PALPath001: return "PAL-PATH-001";
  case SemanticRuleID::PALCFG001: return "PAL-CFG-001";
  case SemanticRuleID::OwnMove001: return "OWN-MOVE-001";
  case SemanticRuleID::OwnCede001: return "OWN-CEDE-001";
  case SemanticRuleID::OwnCede002: return "OWN-CEDE-002";
  case SemanticRuleID::OwnResource001: return "OWN-RESOURCE-001";
  case SemanticRuleID::EffRet001: return "EFF-RET-001";
  case SemanticRuleID::EffMember001: return "EFF-MEMBER-001";
  case SemanticRuleID::EffShape001: return "EFF-SHAPE-001";
  case SemanticRuleID::AsyncEffect001: return "ASYNC-EFFECT-001";
  case SemanticRuleID::AsyncCapture001: return "ASYNC-CAPTURE-001";
  case SemanticRuleID::AsyncSuspend001: return "ASYNC-SUSPEND-001";
  case SemanticRuleID::TKIReplay001: return "TKI-REPLAY-001";
  case SemanticRuleID::TKICache001: return "TKI-CACHE-001";
  case SemanticRuleID::UnsafePub001: return "UNSAFE-PUB-001";
  case SemanticRuleID::ErrorProp001: return "ERROR-PROP-001";
  }
  return "PAL-PATH-001";
}

const char *toString(SemanticOperation value) {
  switch (value) {
  case SemanticOperation::PayloadWrite: return "PayloadWrite";
  case SemanticOperation::SharedPayloadBorrow: return "SharedPayloadBorrow";
  case SemanticOperation::ExclusivePayloadBorrow: return "ExclusivePayloadBorrow";
  case SemanticOperation::HandleViewBorrow: return "HandleViewBorrow";
  case SemanticOperation::HandleRebind: return "HandleRebind";
  case SemanticOperation::ExclusiveMutation: return "ExclusiveMutation";
  case SemanticOperation::Invalidation: return "Invalidation";
  case SemanticOperation::OwnershipTransfer: return "OwnershipTransfer";
  case SemanticOperation::CedeObligation: return "CedeObligation";
  case SemanticOperation::ResourceCopy: return "ResourceCopy";
  case SemanticOperation::EscapingDependency: return "EscapingDependency";
  case SemanticOperation::MemberDependency: return "MemberDependency";
  case SemanticOperation::EffectConsumption: return "EffectConsumption";
  case SemanticOperation::ExecutionBoundaryCapture: return "ExecutionBoundaryCapture";
  case SemanticOperation::ExecutionBoundaryArgument: return "ExecutionBoundaryArgument";
  case SemanticOperation::InterfaceReplay: return "InterfaceReplay";
  case SemanticOperation::ErrorPropagation: return "ErrorPropagation";
  }
  return "InterfaceReplay";
}

const char *toString(SemanticDecision value) {
  switch (value) {
  case SemanticDecision::Allow: return "Allow";
  case SemanticDecision::Reject: return "Reject";
  case SemanticDecision::ConservativeReject: return "ConservativeReject";
  }
  return "Reject";
}

const char *toString(SemanticReason value) {
  switch (value) {
  case SemanticReason::NoConflict: return "NoConflict";
  case SemanticReason::DisjointPaths: return "DisjointPaths";
  case SemanticReason::CompatibleSharedAccess: return "CompatibleSharedAccess";
  case SemanticReason::OverlappingExclusiveAccess: return "OverlappingExclusiveAccess";
  case SemanticReason::ActiveExclusiveBorrow: return "ActiveExclusiveBorrow";
  case SemanticReason::ActiveSharedBorrow: return "ActiveSharedBorrow";
  case SemanticReason::BorrowedPathInvalidation: return "BorrowedPathInvalidation";
  case SemanticReason::AlreadyMoved: return "AlreadyMoved";
  case SemanticReason::MissingExplicitCede: return "MissingExplicitCede";
  case SemanticReason::UnconsumedCede: return "UnconsumedCede";
  case SemanticReason::CedeConsumed: return "CedeConsumed";
  case SemanticReason::MissingCedeReturn: return "MissingCedeReturn";
  case SemanticReason::ResourceCopyForbidden: return "ResourceCopyForbidden";
  case SemanticReason::MissingReturnDependency: return "MissingReturnDependency";
  case SemanticReason::LocalEscape: return "LocalEscape";
  case SemanticReason::LifetimeDepthViolation: return "LifetimeDepthViolation";
  case SemanticReason::MemberDependencyMismatch: return "MemberDependencyMismatch";
  case SemanticReason::DanglingEffect: return "DanglingEffect";
  case SemanticReason::ImplicitBoundaryCapture: return "ImplicitBoundaryCapture";
  case SemanticReason::BorrowedBoundaryArgument: return "BorrowedBoundaryArgument";
  case SemanticReason::BorrowedBoundaryDependency: return "BorrowedBoundaryDependency";
  case SemanticReason::InterfaceContractApplied: return "InterfaceContractApplied";
  case SemanticReason::UnknownProvenance: return "UnknownProvenance";
  case SemanticReason::DirectErrorMatch: return "DirectErrorMatch";
  case SemanticReason::ExplicitErrorConversion: return "ExplicitErrorConversion";
  case SemanticReason::MissingErrorConversion: return "MissingErrorConversion";
  case SemanticReason::PartialMoveUnsupported: return "PartialMoveUnsupported";
  }
  return "UnknownProvenance";
}

} // namespace toka
