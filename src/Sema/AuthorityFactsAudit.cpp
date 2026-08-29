// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/AuthorityFactsAudit.h"
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

const char *placeStateName(PlaceState state) {
  switch (state) {
  case PlaceState::Never:
    return "Never";
  case PlaceState::Live:
    return "Live";
  case PlaceState::Moved:
    return "Moved";
  }
  return "Never";
}

} // namespace

const char *toString(AuthorityFaultPoint value) {
  switch (value) {
  case AuthorityFaultPoint::AfterFullExpressionIdentity:
    return "AfterFullExpressionIdentity";
  case AuthorityFaultPoint::AfterObservationIdentity:
    return "AfterObservationIdentity";
  case AuthorityFaultPoint::AfterPlaceReverseLookup:
    return "AfterPlaceReverseLookup";
  case AuthorityFaultPoint::AfterCleanupClassLookup:
    return "AfterCleanupClassLookup";
  case AuthorityFaultPoint::AfterCleanupFactBuild:
    return "AfterCleanupFactBuild";
  case AuthorityFaultPoint::BeforeRevisionValidation:
    return "BeforeRevisionValidation";
  case AuthorityFaultPoint::BeforeSwap:
    return "BeforeSwap";
  }
  return "BeforeSwap";
}

std::optional<AuthorityFaultPoint>
parseAuthorityFaultPoint(const std::string &value) {
  static const AuthorityFaultPoint Values[] = {
      AuthorityFaultPoint::AfterFullExpressionIdentity,
      AuthorityFaultPoint::AfterObservationIdentity,
      AuthorityFaultPoint::AfterPlaceReverseLookup,
      AuthorityFaultPoint::AfterCleanupClassLookup,
      AuthorityFaultPoint::AfterCleanupFactBuild,
      AuthorityFaultPoint::BeforeRevisionValidation,
      AuthorityFaultPoint::BeforeSwap};
  for (auto candidate : Values)
    if (value == toString(candidate))
      return candidate;
  return std::nullopt;
}

bool operator==(const AuthorityParentSentinel &lhs,
                const AuthorityParentSentinel &rhs) {
  return std::tie(lhs.NextNodeSerial, lhs.NextSymbolID,
                  lhs.DiagnosticErrorCount, lhs.DiagnosticRecordCount,
                  lhs.Evidence, lhs.CopyProofCount, lhs.CopyProofFacts,
                  lhs.ShapePropertyCount, lhs.ShapePropertyFacts,
                  lhs.CleanupClassCount, lhs.CleanupClassFacts,
                  lhs.FullExpressionIdentity, lhs.SourceSymbolID,
                  lhs.SourcePlaceState, lhs.SourceInitMask) ==
         std::tie(rhs.NextNodeSerial, rhs.NextSymbolID,
                  rhs.DiagnosticErrorCount, rhs.DiagnosticRecordCount,
                  rhs.Evidence, rhs.CopyProofCount, rhs.CopyProofFacts,
                  rhs.ShapePropertyCount, rhs.ShapePropertyFacts,
                  rhs.CleanupClassCount, rhs.CleanupClassFacts,
                  rhs.FullExpressionIdentity, rhs.SourceSymbolID,
                  rhs.SourcePlaceState, rhs.SourceInitMask);
}

std::vector<std::string>
differingAuthorityParentFields(const AuthorityParentSentinel &before,
                               const AuthorityParentSentinel &after) {
  std::vector<std::string> result;
#define AUTHORITY_COMPARE(field, name)                                         \
  do {                                                                         \
    if (before.field != after.field)                                           \
      result.push_back(name);                                                  \
  } while (false)
  AUTHORITY_COMPARE(NextNodeSerial, "next_node_serial");
  AUTHORITY_COMPARE(NextSymbolID, "next_symbol_id");
  AUTHORITY_COMPARE(DiagnosticErrorCount, "diagnostic_error_count");
  AUTHORITY_COMPARE(DiagnosticRecordCount, "diagnostic_record_count");
  AUTHORITY_COMPARE(Evidence, "evidence");
  AUTHORITY_COMPARE(CopyProofCount, "copy_proof_count");
  AUTHORITY_COMPARE(CopyProofFacts, "copy_proof_facts");
  AUTHORITY_COMPARE(ShapePropertyCount, "shape_property_count");
  AUTHORITY_COMPARE(ShapePropertyFacts, "shape_property_facts");
  AUTHORITY_COMPARE(CleanupClassCount, "cleanup_class_count");
  AUTHORITY_COMPARE(CleanupClassFacts, "cleanup_class_facts");
  AUTHORITY_COMPARE(FullExpressionIdentity, "full_expression_identity");
  AUTHORITY_COMPARE(SourceSymbolID, "source_symbol_id");
  AUTHORITY_COMPARE(SourcePlaceState, "source_place_state");
  AUTHORITY_COMPARE(SourceInitMask, "source_init_mask");
#undef AUTHORITY_COMPARE
  return result;
}

void AuthorityFactsAuditSession::append(AuthorityObservationReceipt receipt) {
  Receipts.push_back(std::move(receipt));
}

void AuthorityFactsAuditSession::noteExclusion(
    AuthorityExclusionReason reason, AuthorityObservationReceipt receipt) {
  ++Exclusions[reason];
  receipt.Exclusion = reason;
  append(std::move(receipt));
}

void AuthorityFactsAuditSession::noteIndeterminate(
    AuthorityIndeterminateReason reason, AuthorityObservationReceipt receipt) {
  ++Indeterminate[reason];
  receipt.Indeterminate = reason;
  append(std::move(receipt));
}

void AuthorityFactsAuditSession::noteError(
    AuthorityBuildError error, AuthorityObservationReceipt receipt) {
  ++Errors[error];
  receipt.Error = error;
  append(std::move(receipt));
}

AuthorityBuildError
AuthorityFactsAuditSession::publishRecord(const AuthorityFactRecord &record,
                                          const std::string &sourceFile) {
  if (takeFaultPoint(AuthorityFaultPoint::BeforeRevisionValidation,
                     sourceFile)) {
    return AuthorityBuildError::MalformedRevision;
  }
  std::vector<AuthorityFactRecord> records;
  if (Revision)
    records = Revision->records();
  records.push_back(record);
  auto revisionId = SemanticIdentityBuilder::authorityRevision(
      "tokac-command", "m1b2a-authority-facts-v1");
  if (!revisionId)
    return AuthorityBuildError::MalformedRevision;
  auto built =
      AuthorityFactsRevision::build(revisionId.value(), std::move(records));
  if (built.second != AuthorityBuildError::None)
    return built.second;
  if (takeFaultPoint(AuthorityFaultPoint::BeforeSwap, sourceFile)) {
    return AuthorityBuildError::MalformedRevision;
  }
  Revision = std::move(built.first);
  return AuthorityBuildError::None;
}

void AuthorityFactsAuditSession::dumpJSON(std::ostream &out) const {
  static const AuthorityExclusionReason ExclusionValues[] = {
      AuthorityExclusionReason::UnsupportedFullExpressionRoot,
      AuthorityExclusionReason::NonFinalOrSpeculativeTraversal,
      AuthorityExclusionReason::GlobalOrSourceHiddenBinding,
      AuthorityExclusionReason::PlaceAliasOrProjection,
      AuthorityExclusionReason::TemporaryOrMissingBinding,
      AuthorityExclusionReason::CapturedOrGeneratedBinding,
      AuthorityExclusionReason::GenericOrASTClone,
      AuthorityExclusionReason::UnsupportedTypeCategory,
      AuthorityExclusionReason::UnsupportedPartialState};
  static const AuthorityIndeterminateReason IndeterminateValues[] = {
      AuthorityIndeterminateReason::None,
      AuthorityIndeterminateReason::MissingConcreteTypeId,
      AuthorityIndeterminateReason::MissingCleanupClass,
      AuthorityIndeterminateReason::CleanupClassColdOrIncomplete,
      AuthorityIndeterminateReason::CleanupClassConflict,
      AuthorityIndeterminateReason::MissingLegacyDropFact,
      AuthorityIndeterminateReason::ZeroOrAmbiguousInitMask,
      AuthorityIndeterminateReason::PlaceNotLive,
      AuthorityIndeterminateReason::IncompleteOwnerOrDeclarationIdentity};
  static const AuthorityBuildError ErrorValues[] = {
      AuthorityBuildError::None,
      AuthorityBuildError::InvalidFullExpressionIdentity,
      AuthorityBuildError::InvalidObservationIdentity,
      AuthorityBuildError::InvalidPlaceIdentity,
      AuthorityBuildError::StaleSymbolLookupWitness,
      AuthorityBuildError::OwnerDeclarationMismatch,
      AuthorityBuildError::ExactPlaceMismatch,
      AuthorityBuildError::DuplicateFactKey,
      AuthorityBuildError::ConflictingPayload,
      AuthorityBuildError::DanglingCrossReference,
      AuthorityBuildError::MalformedRevision};

  out << "{\"schema\":\"toka.internal.m1b-2a-authority-facts\","
         "\"version\":1,\"status\":\"authority-only\",\"revision_id\":";
  if (Revision)
    stringJSON(out, Revision->id().canonicalKey());
  else
    out << "null";
  out << ",\"record_count\":" << (Revision ? Revision->size() : 0)
      << ",\"cleanup_class_count\":" << CleanupStore.size()
      << ",\"store_build_parent_unchanged\":"
      << (StoreBuildParentUnchanged ? "true" : "false")
      << ",\"store_publish_parent_unchanged\":"
      << (StorePublishParentUnchanged ? "true" : "false")
      << ",\"store_build_differences\":[";
  for (size_t index = 0; index < StoreBuildDifferences.size(); ++index) {
    if (index)
      out << ',';
    stringJSON(out, StoreBuildDifferences[index]);
  }
  out << "],\"store_publish_differences\":[";
  for (size_t index = 0; index < StorePublishDifferences.size(); ++index) {
    if (index)
      out << ',';
    stringJSON(out, StorePublishDifferences[index]);
  }
  out << ']' << ",\"excluded_count_by_reason\":";
  countMapJSON(out, ExclusionValues, Exclusions);
  out << ",\"indeterminate_count_by_reason\":";
  countMapJSON(out, IndeterminateValues, Indeterminate);
  out << ",\"error_count_by_reason\":";
  countMapJSON(out, ErrorValues, Errors);
  out << ",\"cleanup_classes\":[";
  for (size_t index = 0; index < CleanupStore.entries().size(); ++index) {
    if (index)
      out << ',';
    const auto &entry = CleanupStore.entries()[index];
    out << "{\"type_id\":";
    stringJSON(out, entry.Type.canonicalKey());
    out << ",\"class\":";
    stringJSON(out, toString(entry.Kind));
    out << ",\"reason\":";
    stringJSON(out, toString(entry.Reason));
    out << ",\"source\":";
    stringJSON(out, toString(entry.Source));
    out << '}';
  }
  out << "],\"receipts\":[";
  for (size_t index = 0; index < Receipts.size(); ++index) {
    if (index)
      out << ',';
    const auto &receipt = Receipts[index];
    out << "{\"location\":{\"file\":";
    stringJSON(out, receipt.File);
    out << ",\"line\":" << receipt.Line << ",\"column\":" << receipt.Column
        << "},\"full_expression_id\":";
    if (receipt.FullExpression)
      stringJSON(out, receipt.FullExpression->canonicalKey());
    else
      out << "null";
    out << ",\"observation_id\":";
    if (receipt.Observation)
      stringJSON(out, receipt.Observation->canonicalKey());
    else
      out << "null";
    out << ",\"result\":";
    if (receipt.Record)
      stringJSON(out, "Admitted");
    else if (receipt.Exclusion)
      stringJSON(out, "NotInSlice");
    else if (receipt.Indeterminate)
      stringJSON(out, "Indeterminate");
    else
      stringJSON(out, "Error");
    out << ",\"reason\":";
    if (receipt.Exclusion)
      stringJSON(out, toString(*receipt.Exclusion));
    else if (receipt.Indeterminate)
      stringJSON(out, toString(*receipt.Indeterminate));
    else if (receipt.Error)
      stringJSON(out, toString(*receipt.Error));
    else
      out << "null";
    out << ",\"build_parent_unchanged\":"
        << (receipt.BuildParentUnchanged ? "true" : "false")
        << ",\"publish_parent_unchanged\":"
        << (receipt.PublishParentUnchanged ? "true" : "false")
        << ",\"revision_size_before\":" << receipt.RevisionSizeBefore
        << ",\"revision_size_after\":" << receipt.RevisionSizeAfter
        << ",\"build_differences\":[";
    for (size_t field = 0; field < receipt.BuildDifferences.size(); ++field) {
      if (field)
        out << ',';
      stringJSON(out, receipt.BuildDifferences[field]);
    }
    out << "],\"publish_differences\":[";
    for (size_t field = 0; field < receipt.PublishDifferences.size(); ++field) {
      if (field)
        out << ',';
      stringJSON(out, receipt.PublishDifferences[field]);
    }
    out << "],\"record\":";
    if (!receipt.Record) {
      out << "null}";
      continue;
    }
    const auto &record = *receipt.Record;
    out << "{\"full_expression_id\":";
    stringJSON(out, record.Key.FullExpression.canonicalKey());
    out << ",\"observation_id\":";
    stringJSON(out, record.Key.Observation.canonicalKey());
    out << ",\"phase\":";
    stringJSON(out, toString(record.Key.Phase));
    out << ",\"place\":";
    if (record.Place) {
      out << "{\"place_id\":";
      stringJSON(out, record.Place->Place.root().canonicalKey());
      out << ",\"symbol_witness\":" << record.Place->Lookup.SymbolID
          << ",\"declaration_id\":";
      stringJSON(out, record.Place->Declaration.canonicalKey());
      out << ",\"owner_id\":";
      stringJSON(out, record.Place->Owner.canonicalKey());
      out << ",\"state\":";
      stringJSON(out, placeStateName(record.Place->State));
      out << ",\"init_mask\":" << record.Place->InitMask << ",\"type_id\":";
      stringJSON(out, record.Place->Type.canonicalKey());
      out << '}';
    } else
      out << "null";
    out << ",\"cleanup\":{\"kind\":";
    stringJSON(out, toString(record.Cleanup.Kind));
    out << ",\"reason\":";
    stringJSON(out, toString(record.Cleanup.Reason));
    out << ",\"cleanup_id\":";
    if (record.Cleanup.Cleanup)
      stringJSON(out, record.Cleanup.Cleanup->canonicalKey());
    else
      out << "null";
    out << ",\"init_mask\":" << record.Cleanup.InitMask << '}';
    out << ",\"legacy_policy\":";
    if (record.LegacyPolicy) {
      out << "{\"type_id\":";
      stringJSON(out, record.LegacyPolicy->type().canonicalKey());
      out << ",\"soul\":";
      stringJSON(out, record.LegacyPolicy->canonicalSoul());
      out << ",\"category\":";
      stringJSON(out, toString(record.LegacyPolicy->category()));
      out << ",\"drop_fact\":";
      stringJSON(out, toString(record.LegacyPolicy->dropFact()));
      out << ",\"derived_requirement\":";
      stringJSON(out,
                 toString(classifyLegacyCedeRequirement(*record.LegacyPolicy)));
      out << '}';
    } else
      out << "null";
    out << "}}";
  }
  out << "]}\n";
}

} // namespace toka
