// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "toka/PreparedCallParityAudit.h"
#include <algorithm>
#include <ostream>
#include <tuple>

namespace toka {
namespace {

std::string escapeJSON(const std::string &value) {
  std::string result;
  for (unsigned char ch : value) {
    if (ch == '"')
      result += "\\\"";
    else if (ch == '\\')
      result += "\\\\";
    else if (ch == '\n')
      result += "\\n";
    else if (ch == '\r')
      result += "\\r";
    else if (ch == '\t')
      result += "\\t";
    else
      result += static_cast<char>(ch);
  }
  return result;
}

void stringJSON(std::ostream &out, const std::string &value) {
  out << '"' << escapeJSON(value) << '"';
}

template <typename Enum, size_t N>
void countMapJSON(std::ostream &out, const Enum (&values)[N],
                  const std::map<Enum, size_t> &counts) {
  out << '{';
  for (size_t index = 0; index < N; ++index) {
    if (index)
      out << ',';
    stringJSON(out, toString(values[index]));
    auto found = counts.find(values[index]);
    out << ':' << (found == counts.end() ? 0 : found->second);
  }
  out << '}';
}

void optionalString(std::ostream &out,
                    const std::optional<std::string> &value) {
  if (value)
    stringJSON(out, *value);
  else
    out << "null";
}

void planJSON(std::ostream &out,
              const std::optional<D5PreparedPlanSummary> &plan) {
  if (!plan) {
    out << "null";
    return;
  }
  out << "{\"type_proof\":";
  stringJSON(out, plan->TypeProof);
  out << ",\"transfer\":";
  stringJSON(out, plan->Transfer);
  out << ",\"source\":";
  stringJSON(out, plan->Source);
  out << ",\"boundary_access\":";
  stringJSON(out, plan->BoundaryAccess);
  out << ",\"dependency\":";
  stringJSON(out, plan->Dependency);
  out << ",\"spelling\":";
  stringJSON(out, plan->Spelling);
  out << ",\"liability_source\":";
  stringJSON(out, plan->LiabilitySource);
  out << ",\"liability_target\":";
  stringJSON(out, plan->LiabilityTarget);
  out << ",\"evaluation_entries\":" << plan->EvaluationEntries
      << ",\"boundary_entries\":" << plan->BoundaryEntries
      << ",\"finalization_entries\":" << plan->FinalizationEntries
      << ",\"normalized_boundary_entries\":[";
  for (size_t index = 0; index < plan->NormalizedBoundaryEntries.size();
       ++index) {
    if (index)
      out << ',';
    stringJSON(out, plan->NormalizedBoundaryEntries[index]);
  }
  out << "],\"normalized_finalization_entries\":[";
  for (size_t index = 0; index < plan->NormalizedFinalizationEntries.size();
       ++index) {
    if (index)
      out << ',';
    stringJSON(out, plan->NormalizedFinalizationEntries[index]);
  }
  out << "],\"patch_payloads\":[";
  for (size_t index = 0; index < plan->PatchPayloads.size(); ++index) {
    if (index)
      out << ',';
    stringJSON(out, plan->PatchPayloads[index]);
  }
  out << "],\"region_terminal\":";
  stringJSON(out, plan->RegionTerminal);
  out << '}';
}

} // namespace

const char *toString(D5GateExclusionReason value) {
  switch (value) {
  case D5GateExclusionReason::WrongRoute:
    return "WrongRoute";
  case D5GateExclusionReason::NonSameLexical:
    return "NonSameLexical";
  case D5GateExclusionReason::OverloadOrCandidateProbe:
    return "OverloadOrCandidateProbe";
  case D5GateExclusionReason::SpeculativeOrNonFinalTraversal:
    return "SpeculativeOrNonFinalTraversal";
  case D5GateExclusionReason::NestedPreparation:
    return "NestedPreparation";
  case D5GateExclusionReason::ExistingCallDiagnostic:
    return "ExistingCallDiagnostic";
  }
  return "WrongRoute";
}

const char *toString(D5ParityError value) {
  switch (value) {
  case D5ParityError::PrePostFactMismatch:
    return "PrePostFactMismatch";
  case D5ParityError::PreparedPlanMismatch:
    return "PreparedPlanMismatch";
  case D5ParityError::LegacyOutcomeMismatch:
    return "LegacyOutcomeMismatch";
  case D5ParityError::LegacyCheckCountMismatch:
    return "LegacyCheckCountMismatch";
  case D5ParityError::NonEmptyEvaluationDelta:
    return "NonEmptyEvaluationDelta";
  }
  return "PreparedPlanMismatch";
}

const char *toString(D5InfrastructureError value) {
  switch (value) {
  case D5InfrastructureError::InvalidCallSiteIdentity:
    return "InvalidCallSiteIdentity";
  case D5InfrastructureError::InvalidCalleeIdentity:
    return "InvalidCalleeIdentity";
  case D5InfrastructureError::InvalidFormalOrDestinationIdentity:
    return "InvalidFormalOrDestinationIdentity";
  case D5InfrastructureError::InvalidSourcePlaceIdentity:
    return "InvalidSourcePlaceIdentity";
  case D5InfrastructureError::ConflictingPatchPayload:
    return "ConflictingPatchPayload";
  case D5InfrastructureError::MalformedPreparedResult:
    return "MalformedPreparedResult";
  }
  return "MalformedPreparedResult";
}

std::optional<D5InfrastructureError>
parseD5InfrastructureError(const std::string &value) {
  static const D5InfrastructureError Values[] = {
      D5InfrastructureError::InvalidCallSiteIdentity,
      D5InfrastructureError::InvalidCalleeIdentity,
      D5InfrastructureError::InvalidFormalOrDestinationIdentity,
      D5InfrastructureError::InvalidSourcePlaceIdentity,
      D5InfrastructureError::ConflictingPatchPayload,
      D5InfrastructureError::MalformedPreparedResult};
  for (auto candidate : Values) {
    if (value == toString(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::optional<D5ParityError> parseD5ParityError(const std::string &value) {
  static const D5ParityError Values[] = {
      D5ParityError::PrePostFactMismatch, D5ParityError::PreparedPlanMismatch,
      D5ParityError::LegacyOutcomeMismatch,
      D5ParityError::LegacyCheckCountMismatch,
      D5ParityError::NonEmptyEvaluationDelta};
  for (auto candidate : Values) {
    if (value == toString(candidate))
      return candidate;
  }
  return std::nullopt;
}

bool operator==(const D5PreparedPlanSummary &lhs,
                const D5PreparedPlanSummary &rhs) {
  return std::tie(lhs.TypeProof, lhs.Transfer, lhs.Source, lhs.BoundaryAccess,
                  lhs.Dependency, lhs.Spelling, lhs.LiabilitySource,
                  lhs.LiabilityTarget, lhs.EvaluationEntries,
                  lhs.BoundaryEntries, lhs.FinalizationEntries,
                  lhs.NormalizedBoundaryEntries,
                  lhs.NormalizedFinalizationEntries, lhs.PatchPayloads,
                  lhs.RegionTerminal) ==
         std::tie(rhs.TypeProof, rhs.Transfer, rhs.Source, rhs.BoundaryAccess,
                  rhs.Dependency, rhs.Spelling, rhs.LiabilitySource,
                  rhs.LiabilityTarget, rhs.EvaluationEntries,
                  rhs.BoundaryEntries, rhs.FinalizationEntries,
                  rhs.NormalizedBoundaryEntries,
                  rhs.NormalizedFinalizationEntries, rhs.PatchPayloads,
                  rhs.RegionTerminal);
}

D5PreparedPlanSummary summarizeD5PreparedCall(const D3ValidatedCall &call) {
  D5PreparedPlanSummary result;
  if (!call.transferEdges().empty()) {
    const auto &edge = call.transferEdges().front();
    result.TypeProof = edge.typeProof();
    result.Transfer = toString(edge.transferMode());
    result.Source = toString(edge.sourceDisposition());
    result.BoundaryAccess = toString(edge.boundaryAccess());
    result.Dependency = toString(edge.dependency());
    result.Spelling = edge.explicitCede() ? "explicit" : "implicit";
    result.LiabilitySource = toString(edge.liabilitySource().kind());
    result.LiabilityTarget = toString(edge.liabilityTarget().kind());
  }
  result.EvaluationEntries = call.evaluationDelta().entries().size();
  result.BoundaryEntries = call.boundaryDelta().entries().size();
  result.FinalizationEntries = call.finalizationDelta().entries().size();
  auto normalizeEntries = [](const D3DomainDelta &delta) {
    std::vector<std::string> entries;
    for (const auto &entry : delta.entries())
      entries.push_back(std::string(toString(entry.stateDomain())) + "|" +
                        toString(entry.subjectIdentity().kind()) + "|" +
                        entry.expectedBefore() + "->" + entry.resultAfter() +
                        "|" + entry.provenance());
    std::sort(entries.begin(), entries.end());
    return entries;
  };
  result.NormalizedBoundaryEntries = normalizeEntries(call.boundaryDelta());
  result.NormalizedFinalizationEntries =
      normalizeEntries(call.finalizationDelta());
  for (const auto &entry : call.semanticModelPatch().entries())
    result.PatchPayloads.push_back(entry.Payload);
  std::sort(result.PatchPayloads.begin(), result.PatchPayloads.end());
  result.RegionTerminal = call.regionWitness().terminal();
  return result;
}

void D5PreparedCallAuditSession::noteGateExclusion(
    D5GateExclusionReason reason) {
  ++GateExclusions[reason];
}

void D5PreparedCallAuditSession::append(D5PreparedCallReceipt receipt) {
  ++ConsideredCallCount;
  if (receipt.PreparationExclusion)
    ++PreparationExclusions[*receipt.PreparationExclusion];
  if (receipt.PreparationError)
    ++PreparationErrors[*receipt.PreparationError];
  if (receipt.ParityError)
    ++ParityErrors[*receipt.ParityError];
  if (receipt.InfrastructureError)
    ++InfrastructureErrors[*receipt.InfrastructureError];
  if (receipt.PreAdmission == D3AdmissionKind::Admitted &&
      !receipt.ParityError && !receipt.InfrastructureError)
    ++PreparedCount;
  Receipts.push_back(std::move(receipt));
}

void D5PreparedCallAuditSession::dumpJSON(std::ostream &out) const {
  static const D5GateExclusionReason GateReasons[] = {
      D5GateExclusionReason::WrongRoute,
      D5GateExclusionReason::NonSameLexical,
      D5GateExclusionReason::OverloadOrCandidateProbe,
      D5GateExclusionReason::SpeculativeOrNonFinalTraversal,
      D5GateExclusionReason::NestedPreparation,
      D5GateExclusionReason::ExistingCallDiagnostic};
  static const D5PreparationExclusionReason Exclusions[] = {
      D5PreparationExclusionReason::ArityOrDefault,
      D5PreparationExclusionReason::GenericOrContextual,
      D5PreparationExclusionReason::InitOrOutcome,
      D5PreparationExclusionReason::AsyncOrExecutionBoundary,
      D5PreparationExclusionReason::ReturnDependencyOrRegionEscape,
      D5PreparationExclusionReason::ProjectionOrTemporary,
      D5PreparationExclusionReason::NonLocalPlace,
      D5PreparationExclusionReason::SharedRawReferenceOrCallable,
      D5PreparationExclusionReason::DependencyBearingActual,
      D5PreparationExclusionReason::TypeRequiresContextOrConversion,
      D5PreparationExclusionReason::CededNonCopyLegacyExempt};
  static const D5PreparationError Errors[] = {
      D5PreparationError::InvalidIdentity,
      D5PreparationError::IncompatibleType,
      D5PreparationError::IndeterminateCopyProof,
      D5PreparationError::IndeterminateOwnership,
      D5PreparationError::IndeterminateLegacyCedeRequirement,
      D5PreparationError::InconsistentLegacyCedeRequirement,
      D5PreparationError::InvalidWholePlaceAdmission,
      D5PreparationError::IncompleteLiability,
      D5PreparationError::IncompleteRegion,
      D5PreparationError::ConflictingPreparedPlan};
  static const D5ParityError ParityReasons[] = {
      D5ParityError::PrePostFactMismatch, D5ParityError::PreparedPlanMismatch,
      D5ParityError::LegacyOutcomeMismatch,
      D5ParityError::LegacyCheckCountMismatch,
      D5ParityError::NonEmptyEvaluationDelta};
  static const D5InfrastructureError InfrastructureReasons[] = {
      D5InfrastructureError::InvalidCallSiteIdentity,
      D5InfrastructureError::InvalidCalleeIdentity,
      D5InfrastructureError::InvalidFormalOrDestinationIdentity,
      D5InfrastructureError::InvalidSourcePlaceIdentity,
      D5InfrastructureError::ConflictingPatchPayload,
      D5InfrastructureError::MalformedPreparedResult};

  out << "{\"schema\":\"toka.internal.m1b-d5a-prepared-call-parity\","
         "\"version\":1,\"status\":\"shadow-only\","
         "\"considered_call_count\":"
      << ConsideredCallCount
      << ",\"pre_factory_invocation_count\":" << PreFactoryInvocationCount
      << ",\"post_oracle_invocation_count\":" << PostOracleInvocationCount
      << ",\"prepared_count\":" << PreparedCount
      << ",\"gate_excluded_count_by_reason\":";
  countMapJSON(out, GateReasons, GateExclusions);
  out << ",\"excluded_count_by_reason\":";
  countMapJSON(out, Exclusions, PreparationExclusions);
  out << ",\"rejected_count_by_reason\":";
  countMapJSON(out, Errors, PreparationErrors);
  out << ",\"parity_failure_count_by_reason\":";
  countMapJSON(out, ParityReasons, ParityErrors);
  out << ",\"infrastructure_error_count_by_reason\":";
  countMapJSON(out, InfrastructureReasons, InfrastructureErrors);
  out << ",\"receipts\":[";
  for (size_t index = 0; index < Receipts.size(); ++index) {
    if (index)
      out << ',';
    const auto &receipt = Receipts[index];
    out << "{\"call_site\":{\"file\":";
    stringJSON(out, receipt.Location.File);
    out << ",\"line\":" << receipt.Location.Line
        << ",\"column\":" << receipt.Location.Column << "},\"callee\":";
    stringJSON(out, receipt.Callee);
    out << ",\"formal_identity\":";
    stringJSON(out, receipt.FormalIdentity);
    out << ",\"source_identity\":";
    stringJSON(out, receipt.SourceIdentity);
    out << ",\"actual_type\":";
    stringJSON(out, receipt.ActualType);
    out << ",\"formal_type\":";
    stringJSON(out, receipt.FormalType);
    out << ",\"source_state_before\":";
    stringJSON(out, receipt.SourceStateBefore);
    out << ",\"pal_state_before\":";
    stringJSON(out, receipt.PALStateBefore);
    out << ",\"source_init_mask\":" << receipt.SourceInitMask
        << ",\"dependency_bearing_actual\":"
        << (receipt.DependencyBearingActual ? "true" : "false");
    out << ",\"legacy_cede_requirement\":";
    optionalString(out, receipt.LegacyRequirement
                            ? std::optional<std::string>(
                                  toString(*receipt.LegacyRequirement))
                            : std::nullopt);
    out << ",\"pre_admission\":";
    stringJSON(out, toString(receipt.PreAdmission));
    out << ",\"post_admission\":";
    stringJSON(out, toString(receipt.PostAdmission));
    out << ",\"pre_reason\":";
    std::optional<std::string> reason;
    if (receipt.PreparationExclusion)
      reason = toString(*receipt.PreparationExclusion);
    else if (receipt.PreparationError)
      reason = toString(*receipt.PreparationError);
    optionalString(out, reason);
    out << ",\"parity_error\":";
    optionalString(
        out, receipt.ParityError
                 ? std::optional<std::string>(toString(*receipt.ParityError))
                 : std::nullopt);
    out << ",\"infrastructure_error\":";
    optionalString(out, receipt.InfrastructureError
                            ? std::optional<std::string>(
                                  toString(*receipt.InfrastructureError))
                            : std::nullopt);
    out << ",\"pre_plan\":";
    planJSON(out, receipt.PrePlan);
    out << ",\"post_plan\":";
    planJSON(out, receipt.PostPlan);
    out << ",\"authority_projection\":";
    planJSON(out, receipt.PrePlan);
    out << ",\"legacy_diagnostic_codes\":[";
    for (size_t code = 0; code < receipt.LegacyDiagnosticCodes.size(); ++code) {
      if (code)
        out << ',';
      stringJSON(out, receipt.LegacyDiagnosticCodes[code]);
    }
    out << "],\"final_legacy_check_count\":" << receipt.FinalLegacyCheckCount
        << ",\"same_call_structural_parity\":"
        << (receipt.SameCallStructuralParity ? "true" : "false")
        << ",\"pre_factory_parent_unchanged\":"
        << (receipt.PreFactoryParentUnchanged ? "true" : "false")
        << ",\"post_factory_parent_unchanged\":"
        << (receipt.PostFactoryParentUnchanged ? "true" : "false")
        << ",\"pre_differing_parent_fields\":[";
    for (size_t field = 0; field < receipt.PreDifferingParentFields.size();
         ++field) {
      if (field)
        out << ',';
      stringJSON(out, receipt.PreDifferingParentFields[field]);
    }
    out << "],\"post_differing_parent_fields\":[";
    for (size_t field = 0; field < receipt.PostDifferingParentFields.size();
         ++field) {
      if (field)
        out << ',';
      stringJSON(out, receipt.PostDifferingParentFields[field]);
    }
    out << "]}";
  }
  out << "]}\n";
}

} // namespace toka
