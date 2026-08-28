// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "toka/PureNominalOverloadProbeAudit.h"
#include <algorithm>
#include <ostream>
#include <set>
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

template <typename Enum>
void countMapJSON(std::ostream &out, const std::vector<Enum> &values,
                  const std::map<Enum, size_t> &counts) {
  out << '{';
  for (size_t index = 0; index < values.size(); ++index) {
    if (index != 0)
      out << ',';
    stringJSON(out, toString(values[index]));
    auto found = counts.find(values[index]);
    out << ':' << (found == counts.end() ? 0 : found->second);
  }
  out << '}';
}

} // namespace

const char *toString(D4ProbeExclusionReason value) {
  switch (value) {
  case D4ProbeExclusionReason::WrongRoute:
    return "WrongRoute";
  case D4ProbeExclusionReason::NonSourceBacked:
    return "NonSourceBacked";
  case D4ProbeExclusionReason::NotOverloaded:
    return "NotOverloaded";
  case D4ProbeExclusionReason::ArityOrDefault:
    return "ArityOrDefault";
  case D4ProbeExclusionReason::NonLocalOrNonLivePlace:
    return "NonLocalOrNonLivePlace";
  case D4ProbeExclusionReason::NonDirectNominalActual:
    return "NonDirectNominalActual";
  case D4ProbeExclusionReason::NonDirectNominalFormal:
    return "NonDirectNominalFormal";
  case D4ProbeExclusionReason::VariantOrRefinedNominal:
    return "VariantOrRefinedNominal";
  case D4ProbeExclusionReason::PrimitiveOrAlias:
    return "PrimitiveOrAlias";
  case D4ProbeExclusionReason::AttributesOrConversion:
    return "AttributesOrConversion";
  case D4ProbeExclusionReason::GenericOrContextual:
    return "GenericOrContextual";
  case D4ProbeExclusionReason::HandleOrPermission:
    return "HandleOrPermission";
  case D4ProbeExclusionReason::ContractUnsupported:
    return "ContractUnsupported";
  case D4ProbeExclusionReason::PALOrDependencyConflict:
    return "PALOrDependencyConflict";
  case D4ProbeExclusionReason::SourceHiddenOrIncomplete:
    return "SourceHiddenOrIncomplete";
  }
  return "WrongRoute";
}

std::optional<D4ProbeInfrastructureError>
parseD4ProbeInfrastructureError(const std::string &value) {
  static const D4ProbeInfrastructureError Values[] = {
      D4ProbeInfrastructureError::InvalidCallSiteIdentity,
      D4ProbeInfrastructureError::InvalidNominalShapeId,
      D4ProbeInfrastructureError::DuplicateCandidateIdentity,
      D4ProbeInfrastructureError::DuplicateLegacyOrdinal,
      D4ProbeInfrastructureError::NonContiguousLegacyOrdinal,
      D4ProbeInfrastructureError::MalformedBatch};
  for (auto candidate : Values) {
    if (value == toString(candidate))
      return candidate;
  }
  return std::nullopt;
}

std::optional<D4ProbeInfrastructureError>
validateD4FrozenMapping(const std::vector<D4FrozenMappingEntry> &entries,
                        std::optional<DeclarationId> fallbackIdentity,
                        const void *fallbackPointer) {
  if (entries.size() < 2)
    return D4ProbeInfrastructureError::MalformedBatch;
  std::set<DeclarationId> identities;
  std::set<const void *> pointers;
  std::set<unsigned> ordinals;
  bool fallbackFound = !fallbackIdentity && !fallbackPointer;
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto &entry = entries[index];
    if (!entry.Declaration.valid() || !entry.DeclarationPointer)
      return D4ProbeInfrastructureError::MalformedBatch;
    if (!identities.insert(entry.Declaration).second)
      return D4ProbeInfrastructureError::DuplicateCandidateIdentity;
    if (!pointers.insert(entry.DeclarationPointer).second)
      return D4ProbeInfrastructureError::DuplicateCandidateIdentity;
    if (!ordinals.insert(entry.LegacyOrdinal).second)
      return D4ProbeInfrastructureError::DuplicateLegacyOrdinal;
    if (entry.LegacyOrdinal != index)
      return D4ProbeInfrastructureError::NonContiguousLegacyOrdinal;
    if (fallbackIdentity && fallbackPointer &&
        entry.Declaration == *fallbackIdentity &&
        entry.DeclarationPointer == fallbackPointer)
      fallbackFound = true;
  }
  if (static_cast<bool>(fallbackIdentity) !=
          static_cast<bool>(fallbackPointer) ||
      !fallbackFound)
    return D4ProbeInfrastructureError::MalformedBatch;
  return std::nullopt;
}

bool operator==(const D4ProbeParentSentinel &lhs,
                const D4ProbeParentSentinel &rhs) {
  return std::tie(lhs.NextNodeSerial, lhs.DiagnosticErrorCount,
                  lhs.NextSymbolId, lhs.DiagnosticRecordCount,
                  lhs.SourceSymbolId, lhs.SourceBinding, lhs.SourceAST,
                  lhs.SourceInitMask, lhs.SourceMoved, lhs.SourceUsed,
                  lhs.SourceMutated, lhs.SourcePlaceState, lhs.SourcePALState,
                  lhs.SourceBorrowedPath, lhs.SourceDependencies,
                  lhs.CallResolvedFunction, lhs.ActualResolvedType,
                  lhs.CopyProofCount, lhs.CopyFactCount, lhs.InstantiationCount,
                  lhs.GenericShapeCount, lhs.D3ConsideredCount,
                  lhs.D3FactoryCount) ==
             std::tie(rhs.NextNodeSerial, rhs.DiagnosticErrorCount,
                      rhs.NextSymbolId, rhs.DiagnosticRecordCount,
                      rhs.SourceSymbolId, rhs.SourceBinding, rhs.SourceAST,
                      rhs.SourceInitMask, rhs.SourceMoved, rhs.SourceUsed,
                      rhs.SourceMutated, rhs.SourcePlaceState,
                      rhs.SourcePALState, rhs.SourceBorrowedPath,
                      rhs.SourceDependencies, rhs.CallResolvedFunction,
                      rhs.ActualResolvedType, rhs.CopyProofCount,
                      rhs.CopyFactCount, rhs.InstantiationCount,
                      rhs.GenericShapeCount, rhs.D3ConsideredCount,
                      rhs.D3FactoryCount) &&
         lhs.Evidence == rhs.Evidence;
}

std::vector<std::string>
differingD4ProbeParentFields(const D4ProbeParentSentinel &before,
                             const D4ProbeParentSentinel &after) {
  std::vector<std::string> result;
#define D4_COMPARE(field, name)                                                \
  do {                                                                         \
    if (before.field != after.field)                                           \
      result.push_back(name);                                                  \
  } while (false)
  D4_COMPARE(NextNodeSerial, "next_node_serial");
  D4_COMPARE(NextSymbolId, "next_symbol_id");
  D4_COMPARE(DiagnosticErrorCount, "diagnostic_error_count");
  D4_COMPARE(DiagnosticRecordCount, "diagnostic_record_count");
  D4_COMPARE(Evidence, "evidence");
  D4_COMPARE(SourceSymbolId, "source_symbol_id");
  D4_COMPARE(SourceBinding, "source_binding");
  D4_COMPARE(SourceAST, "source_ast");
  D4_COMPARE(SourceInitMask, "source_init_mask");
  D4_COMPARE(SourceMoved, "source_moved");
  D4_COMPARE(SourceUsed, "source_used");
  D4_COMPARE(SourceMutated, "source_mutated");
  D4_COMPARE(SourcePlaceState, "source_place_state");
  D4_COMPARE(SourcePALState, "source_pal_state");
  D4_COMPARE(SourceBorrowedPath, "source_borrowed_path");
  D4_COMPARE(SourceDependencies, "source_dependencies");
  D4_COMPARE(CallResolvedFunction, "call_resolved_function");
  D4_COMPARE(ActualResolvedType, "actual_resolved_type");
  D4_COMPARE(CopyProofCount, "copy_proof_count");
  D4_COMPARE(CopyFactCount, "copy_fact_count");
  D4_COMPARE(InstantiationCount, "instantiation_count");
  D4_COMPARE(GenericShapeCount, "generic_shape_count");
  D4_COMPARE(D3ConsideredCount, "d3_considered_count");
  D4_COMPARE(D3FactoryCount, "d3_factory_count");
#undef D4_COMPARE
  return result;
}

size_t D4ProbeAuditSession::append(D4ProbeAuditRecord record,
                                   const void *callKey) {
  ++AttemptedBatchCount;
  if (record.Exclusion) {
    ++ExcludedCounts[*record.Exclusion];
  } else {
    ++PureBatchCount;
    if (record.LegacyReason)
      ++LegacyRequiredCounts[*record.LegacyReason];
  }
  const size_t index = Records.size();
  Records.push_back(std::move(record));
  if (callKey)
    PendingFinalChecks[callKey] = index;
  return index;
}

void D4ProbeAuditSession::appendInfrastructureFailure(
    D4ProbeInfrastructureError error, D4ProbeAuditRecord record) {
  ++AttemptedBatchCount;
  ++InfrastructureErrors[error];
  Records.push_back(std::move(record));
}

void D4ProbeAuditSession::noteFinalLegacyCheck(const void *callKey) {
  auto found = PendingFinalChecks.find(callKey);
  if (found == PendingFinalChecks.end())
    return;
  ++Records[found->second].FinalLegacyCheckCount;
}

void D4ProbeAuditSession::setForcedLegacySelection(
    size_t recordIndex, std::optional<std::string> declaration,
    size_t diagnosticCount) {
  if (recordIndex >= Records.size())
    return;
  Records[recordIndex].ForcedLegacySelectedDeclarationIdentity =
      std::move(declaration);
  Records[recordIndex].CandidateDiagnosticCount = diagnosticCount;
}

void D4ProbeAuditSession::dumpJSON(std::ostream &out) const {
  static const std::vector<D4ProbeExclusionReason> Exclusions = {
      D4ProbeExclusionReason::WrongRoute,
      D4ProbeExclusionReason::NonSourceBacked,
      D4ProbeExclusionReason::NotOverloaded,
      D4ProbeExclusionReason::ArityOrDefault,
      D4ProbeExclusionReason::NonLocalOrNonLivePlace,
      D4ProbeExclusionReason::NonDirectNominalActual,
      D4ProbeExclusionReason::NonDirectNominalFormal,
      D4ProbeExclusionReason::VariantOrRefinedNominal,
      D4ProbeExclusionReason::PrimitiveOrAlias,
      D4ProbeExclusionReason::AttributesOrConversion,
      D4ProbeExclusionReason::GenericOrContextual,
      D4ProbeExclusionReason::HandleOrPermission,
      D4ProbeExclusionReason::ContractUnsupported,
      D4ProbeExclusionReason::PALOrDependencyConflict,
      D4ProbeExclusionReason::SourceHiddenOrIncomplete};
  static const std::vector<D4LegacyReason> LegacyReasons = {
      D4LegacyReason::ZeroCompatible, D4LegacyReason::MultipleCompatible};
  static const std::vector<D4ProbeInfrastructureError> Errors = {
      D4ProbeInfrastructureError::InvalidCallSiteIdentity,
      D4ProbeInfrastructureError::InvalidNominalShapeId,
      D4ProbeInfrastructureError::DuplicateCandidateIdentity,
      D4ProbeInfrastructureError::DuplicateLegacyOrdinal,
      D4ProbeInfrastructureError::NonContiguousLegacyOrdinal,
      D4ProbeInfrastructureError::MalformedBatch};
  out << "{\"schema\":\"toka.internal.m1b-d4a-pure-nominal-overload-probe\","
         "\"version\":1,\"evaluation_schedule\":\""
      << (ReverseSchedule ? "reverse-for-testing" : "legacy-order")
      << "\",\"attempted_batch_count\":" << AttemptedBatchCount
      << ",\"pure_batch_count\":" << PureBatchCount
      << ",\"excluded_count_by_reason\":";
  countMapJSON(out, Exclusions, ExcludedCounts);
  out << ",\"legacy_required_count_by_reason\":";
  countMapJSON(out, LegacyReasons, LegacyRequiredCounts);
  size_t infrastructureErrorCount = 0;
  for (const auto &entry : InfrastructureErrors)
    infrastructureErrorCount += entry.second;
  out << ",\"infrastructure_error_count\":" << infrastructureErrorCount
      << ",\"infrastructure_error_count_by_reason\":";
  countMapJSON(out, Errors, InfrastructureErrors);
  out << ",\"batches\":[";
  for (size_t index = 0; index < Records.size(); ++index) {
    if (index != 0)
      out << ',';
    const auto &record = Records[index];
    out << "{\"call_site\":{\"file\":";
    stringJSON(out, record.Location.File);
    out << ",\"line\":" << record.Location.Line
        << ",\"column\":" << record.Location.Column << "},\"callee\":";
    stringJSON(out, record.Callee);
    out << ",\"exclusion\":";
    if (record.Exclusion)
      stringJSON(out, toString(*record.Exclusion));
    else
      out << "null";
    out << ",\"candidates\":[";
    for (size_t candidate = 0; candidate < record.Candidates.size();
         ++candidate) {
      if (candidate != 0)
        out << ',';
      const auto &value = record.Candidates[candidate];
      out << "{\"declaration_id\":";
      stringJSON(out, value.DeclarationIdentity);
      out << ",\"legacy_ordinal\":" << value.LegacyOrdinal
          << ",\"formal_nominal_id\":";
      stringJSON(out, value.FormalNominalIdentity);
      out << ",\"compatible\":" << (value.Compatible ? "true" : "false") << '}';
    }
    out << "],\"disposition\":";
    if (record.Disposition)
      stringJSON(out, toString(*record.Disposition));
    else
      out << "null";
    out << ",\"legacy_reason\":";
    if (record.LegacyReason)
      stringJSON(out, toString(*record.LegacyReason));
    else
      out << "null";
    auto optionalString = [&](const std::optional<std::string> &value) {
      if (value)
        stringJSON(out, *value);
      else
        out << "null";
    };
    out << ",\"selected_declaration_id\":";
    optionalString(record.SelectedDeclarationIdentity);
    out << ",\"forced_legacy_selected_declaration_id\":";
    optionalString(record.ForcedLegacySelectedDeclarationIdentity);
    out << ",\"candidate_diagnostic_count\":" << record.CandidateDiagnosticCount
        << ",\"final_legacy_check_count\":" << record.FinalLegacyCheckCount
        << ",\"parent_unchanged\":"
        << (record.ParentUnchanged ? "true" : "false")
        << ",\"differing_parent_fields\":[";
    for (size_t field = 0; field < record.DifferingParentFields.size();
         ++field) {
      if (field != 0)
        out << ',';
      stringJSON(out, record.DifferingParentFields[field]);
    }
    out << "]}";
  }
  out << "]}\n";
}

} // namespace toka
