// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/SemanticEvidence.h"
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include <algorithm>
#include <ostream>
#include <tuple>

namespace toka {

bool SemanticEvidence::Enabled = false;
std::vector<SemanticDecisionRecord> SemanticEvidence::Records;
std::vector<CedeObligationRecord> SemanticEvidence::CedeObligations;

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
  return std::tie(Stage, Status, Reason, Subject, Origin, Location,
                  ContractLocation) <
         std::tie(rhs.Stage, rhs.Status, rhs.Reason, rhs.Subject, rhs.Origin,
                  rhs.Location, rhs.ContractLocation);
}

bool CedeObligationRecord::operator==(const CedeObligationRecord &rhs) const {
  return Stage == rhs.Stage && Status == rhs.Status && Reason == rhs.Reason &&
         Subject == rhs.Subject && Origin == rhs.Origin &&
         Location == rhs.Location && ContractLocation == rhs.ContractLocation;
}

void SemanticEvidence::enable(bool value) {
  Enabled = value;
  reset();
}

bool SemanticEvidence::isEnabled() { return Enabled; }

void SemanticEvidence::reset() {
  Records.clear();
  CedeObligations.clear();
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
    SourceLocation location, SourceLocation contractLocation) {
  if (!Enabled)
    return;
  CedeObligations.push_back(
      {stage, status, reason, std::move(subject), std::move(origin),
       resolveLocation(location), resolveLocation(contractLocation)});
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
