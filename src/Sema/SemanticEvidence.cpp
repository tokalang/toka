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
std::vector<SemanticDecisionRecord> SemanticEvidence::Records;
std::vector<CedeObligationRecord> SemanticEvidence::CedeObligations;
std::vector<CallTransferShadowRecord> SemanticEvidence::CallTransferShadows;
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

bool CallTransferShadowRecord::operator<(
    const CallTransferShadowRecord &rhs) const {
  return std::tie(
             Callee, Route, Parameter, ArgumentIndex, FormalIndex,
             ValueCategory, Spelling, Transfer, Source, Dependency,
             PlaceEligibility, Drop, ExecutionBoundary, SourceRootID,
             SourcePath, SourceIdentity, ReferentPath, ReferentIdentity,
             DependencyPaths, HasCleanupMask, CleanupMask, FormalCeded,
             FormalInit, ActualInit, LegacyCallerRuleApplied,
             LegacyCedeExempt, LegacyMissingCede,
             Async, Location, ContractLocation) <
         std::tie(
             rhs.Callee, rhs.Route, rhs.Parameter, rhs.ArgumentIndex,
             rhs.FormalIndex, rhs.ValueCategory, rhs.Spelling, rhs.Transfer,
             rhs.Source, rhs.Dependency, rhs.PlaceEligibility, rhs.Drop,
             rhs.ExecutionBoundary, rhs.SourceRootID, rhs.SourcePath,
             rhs.SourceIdentity, rhs.ReferentPath, rhs.ReferentIdentity,
             rhs.DependencyPaths, rhs.HasCleanupMask, rhs.CleanupMask,
             rhs.FormalCeded, rhs.FormalInit, rhs.ActualInit,
             rhs.LegacyCallerRuleApplied,
             rhs.LegacyCedeExempt, rhs.LegacyMissingCede, rhs.Async,
             rhs.Location, rhs.ContractLocation);
}

bool CallTransferShadowRecord::operator==(
    const CallTransferShadowRecord &rhs) const {
  return Callee == rhs.Callee && Route == rhs.Route &&
         Parameter == rhs.Parameter && ArgumentIndex == rhs.ArgumentIndex &&
         FormalIndex == rhs.FormalIndex && ValueCategory == rhs.ValueCategory &&
         Spelling == rhs.Spelling && Transfer == rhs.Transfer &&
         Source == rhs.Source && Dependency == rhs.Dependency &&
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
         Location == rhs.Location && ContractLocation == rhs.ContractLocation;
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
  reset();
}

bool SemanticEvidence::isEnabled() { return Enabled; }

void SemanticEvidence::enableCallTransferShadow(bool value) {
  CallTransferShadowEnabled = value;
}

bool SemanticEvidence::isCallTransferShadowEnabled() {
  return CallTransferShadowEnabled;
}

SemanticEvidenceAuditState SemanticEvidence::auditState() {
  return {Enabled,
          CallTransferShadowEnabled,
          Records.size(),
          CedeObligations.size(),
          CallTransferShadows.size(),
          CapabilityCalls.size(),
          TodoGoals.size(),
          ConditionalFacts.size(),
          Records,
          CedeObligations,
          CallTransferShadows,
          CapabilityCalls,
          TodoGoals,
          ConditionalFacts};
}

void SemanticEvidence::reset() {
  Records.clear();
  CedeObligations.clear();
  CallTransferShadows.clear();
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
    std::string callee, std::string route, std::string parameter,
    unsigned argumentIndex, unsigned formalIndex, std::string valueCategory,
    std::string spelling, std::string transfer, std::string source,
    std::string dependency, std::string placeEligibility, std::string drop,
    std::string executionBoundary, uint64_t sourceRootID,
    std::string sourcePath, std::string sourceIdentity,
    std::string referentPath, std::string referentIdentity,
    std::vector<std::string> dependencyPaths, bool hasCleanupMask,
    uint64_t cleanupMask, bool formalCeded, bool formalInit, bool actualInit,
    bool legacyCallerRuleApplied, bool legacyCedeExempt,
    bool legacyMissingCede, bool async, SourceLocation location,
    SourceLocation contractLocation) {
  if (!Enabled || !CallTransferShadowEnabled)
    return;
  CallTransferShadows.push_back(
      {std::move(callee), std::move(route), std::move(parameter), argumentIndex,
       formalIndex, std::move(valueCategory), std::move(spelling),
       std::move(transfer), std::move(source), std::move(dependency),
       std::move(placeEligibility), std::move(drop),
       std::move(executionBoundary), sourceRootID, std::move(sourcePath),
       std::move(sourceIdentity), std::move(referentPath),
       std::move(referentIdentity), std::move(dependencyPaths), hasCleanupMask,
       cleanupMask, formalCeded, formalInit, actualInit,
       legacyCallerRuleApplied, legacyCedeExempt,
       legacyMissingCede, async,
       resolveLocation(location), resolveLocation(contractLocation)});
}

void SemanticEvidence::dumpCallTransferShadowJSON(std::ostream &out) {
  std::sort(CallTransferShadows.begin(), CallTransferShadows.end());
  CallTransferShadows.erase(
      std::unique(CallTransferShadows.begin(), CallTransferShadows.end()),
      CallTransferShadows.end());
  out << "{\"schema\":\"toka.internal.call-transfer-shadow\","
         "\"version\":3,\"status\":\"audit-only\",\"records\":[";
  for (size_t i = 0; i < CallTransferShadows.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &record = CallTransferShadows[i];
    out << "{\"callee\":\"" << escapeJSON(record.Callee)
        << "\",\"route\":\"" << escapeJSON(record.Route)
        << "\",\"parameter\":\"" << escapeJSON(record.Parameter)
        << "\",\"argument_index\":" << record.ArgumentIndex
        << ",\"formal_index\":" << record.FormalIndex
        << ",\"value_category\":\""
        << escapeJSON(record.ValueCategory)
        << "\",\"spelling\":\"" << escapeJSON(record.Spelling)
        << "\",\"transfer\":\"" << escapeJSON(record.Transfer)
        << "\",\"source\":\"" << escapeJSON(record.Source)
        << "\",\"dependency\":\"" << escapeJSON(record.Dependency)
        << "\",\"place_eligibility\":\""
        << escapeJSON(record.PlaceEligibility)
        << "\",\"drop\":\"" << escapeJSON(record.Drop)
        << "\",\"execution_boundary\":\""
        << escapeJSON(record.ExecutionBoundary)
        << "\",\"source_root_id\":" << record.SourceRootID
        << ",\"source_path\":\"" << escapeJSON(record.SourcePath)
        << "\",\"source_identity\":\""
        << escapeJSON(record.SourceIdentity)
        << "\",\"referent_path\":\"" << escapeJSON(record.ReferentPath)
        << "\",\"referent_identity\":\""
        << escapeJSON(record.ReferentIdentity)
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
        << ",\"location\":";
    dumpLocation(out, record.Location);
    out << ",\"contract_location\":";
    dumpLocation(out, record.ContractLocation);
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
