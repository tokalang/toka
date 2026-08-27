// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "toka/DirectCallObservationAudit.h"
#include <algorithm>
#include <ostream>
#include <tuple>

namespace toka {
namespace {

std::string escapeJSON(const std::string &value) {
  std::string result;
  result.reserve(value.size());
  for (unsigned char c : value) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c < 0x20U) {
        static constexpr char Hex[] = "0123456789abcdef";
        result += "\\u00";
        result += Hex[c >> 4U];
        result += Hex[c & 0x0fU];
      } else {
        result += static_cast<char>(c);
      }
    }
  }
  return result;
}

void dumpString(std::ostream &out, const std::string &value) {
  out << '"' << escapeJSON(value) << '"';
}

void dumpStringArray(std::ostream &out,
                     const std::vector<std::string> &values) {
  out << '[';
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0)
      out << ',';
    dumpString(out, values[i]);
  }
  out << ']';
}

void dumpLocation(std::ostream &out, const D3SourceLocation &location) {
  out << "{\"file\":";
  dumpString(out, location.File);
  out << ",\"line\":" << location.Line << ",\"column\":" << location.Column
      << '}';
}

void dumpDelta(std::ostream &out, const D3DomainDelta &delta) {
  out << "{\"lane\":";
  dumpString(out, toString(delta.lane()));
  out << ",\"entries\":[";
  for (size_t i = 0; i < delta.entries().size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &entry = delta.entries()[i];
    out << "{\"edge_id\":";
    dumpString(out, entry.edgeId().canonicalKey());
    out << ",\"state_domain\":";
    dumpString(out, toString(entry.stateDomain()));
    out << ",\"subject_identity\":";
    dumpString(out, entry.subjectIdentity());
    out << ",\"expected_before\":";
    dumpString(out, entry.expectedBefore());
    out << ",\"result_after\":";
    dumpString(out, entry.resultAfter());
    out << ",\"provenance\":";
    dumpString(out, entry.provenance());
    out << '}';
  }
  out << "]}";
}

void dumpFactoryRecord(std::ostream &out,
                       const D3FactoryObservationRecord &record) {
  const auto &input = record.input();
  out << "{\"call_site\":";
  dumpLocation(out, input.Pre.CallLocation);
  out << ",\"callee\":";
  dumpString(out, input.Pre.CalleeName);
  out << ",\"callee_identity\":";
  dumpString(out, input.Pre.CalleeWitness);
  out << ",\"formal\":{\"name\":";
  dumpString(out, input.Pre.FormalName);
  out << ",\"identity\":";
  dumpString(out, input.Pre.FormalWitness);
  out << ",\"type\":";
  dumpString(out, input.Pre.FormalType);
  out << "},\"admission\":";
  dumpString(out, toString(record.admission()));
  out << ",\"reason\":";
  if (record.exclusionReason())
    dumpString(out, toString(*record.exclusionReason()));
  else if (record.validationError())
    dumpString(out, toString(*record.validationError()));
  else
    out << "null";
  out << ",\"legacy_outcome\":{\"status\":";
  dumpString(out, input.Post.LegacySucceeded ? "success" : "rejected");
  out << ",\"diagnostic_codes\":";
  dumpStringArray(out, input.Post.LegacyDiagnosticCodes);
  out << ",\"spelling\":";
  dumpString(out, input.Pre.ExplicitCede ? "explicit" : "implicit");
  out << "},\"prospective_outcome\":{\"status\":";
  dumpString(out, record.admission() == D3AdmissionKind::Admitted
                      ? "transfer"
                      : (record.admission() == D3AdmissionKind::NotInSlice
                             ? "not-in-slice"
                             : "rejected"));
  out << "}";

  if (!record.validatedCall()) {
    out << ",\"transfer_edges\":[],\"evaluation_delta\":null,"
           "\"boundary_delta\":null,\"finalization_delta\":null,"
           "\"semantic_model_patch\":[],\"region_witness\":null}";
    return;
  }

  const auto &validated = *record.validatedCall();
  out << ",\"call_site_id\":";
  dumpString(out, validated.callSiteId().canonicalKey());
  out << ",\"resolved_callee_kind\":"
      << static_cast<unsigned>(validated.calleeId().kind());
  out << ",\"transfer_edges\":[";
  for (size_t i = 0; i < validated.transferEdges().size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &edge = validated.transferEdges()[i];
    out << "{\"edge_id\":";
    dumpString(out, edge.id().canonicalKey());
    out << ",\"argument_plan_id\":";
    dumpString(out, edge.argumentPlanId().canonicalKey());
    out << ",\"formal_id\":";
    dumpString(out, edge.formalId().canonicalKey());
    out << ",\"destination_id\":";
    dumpString(out, edge.destinationId().canonicalKey());
    out << ",\"source_place\":";
    if (edge.sourcePlace())
      dumpString(out, edge.sourcePlace()->root().canonicalKey());
    else
      out << "null";
    out << ",\"type_proof\":";
    dumpString(out, edge.typeProof());
    out << ",\"value_category\":";
    dumpString(out, edge.valueCategory());
    out << ",\"transfer_mode\":";
    dumpString(out, toString(edge.transferMode()));
    out << ",\"source_disposition\":";
    dumpString(out, toString(edge.sourceDisposition()));
    out << ",\"dependency\":";
    dumpString(out, edge.dependency());
    out << ",\"liability_source\":";
    dumpString(out, edge.liabilitySource());
    out << ",\"liability_target\":";
    dumpString(out, edge.liabilityTarget());
    out << ",\"spelling\":";
    dumpString(out, edge.explicitCede() ? "explicit" : "implicit");
    out << '}';
  }
  out << "],\"evaluation_delta\":";
  dumpDelta(out, validated.evaluationDelta());
  out << ",\"boundary_delta\":";
  dumpDelta(out, validated.boundaryDelta());
  out << ",\"finalization_delta\":";
  dumpDelta(out, validated.finalizationDelta());
  out << ",\"semantic_model_patch\":[";
  const auto &patchEntries = validated.semanticModelPatch().entries();
  for (size_t i = 0; i < patchEntries.size(); ++i) {
    if (i != 0)
      out << ',';
    out << "{\"identity\":";
    dumpString(out, patchEntries[i].Identity.canonicalKey());
    out << ",\"payload\":";
    dumpString(out, patchEntries[i].Payload);
    out << '}';
  }
  const auto &region = validated.regionWitness();
  out << "],\"region_witness\":{\"call_region\":";
  dumpString(out, region.callRegion().canonicalKey());
  out << ",\"full_expression_region\":";
  dumpString(out, region.fullExpressionRegion().canonicalKey());
  out << ",\"origin\":";
  dumpString(out, region.origin());
  out << ",\"subject\":";
  dumpString(out, region.subject());
  out << ",\"terminal\":";
  dumpString(out, region.terminal());
  out << "}}";
}

} // namespace

const char *toString(D3GateExclusionReason value) {
  switch (value) {
  case D3GateExclusionReason::WrongRoute:
    return "WrongRoute";
  case D3GateExclusionReason::NonSameLexical:
    return "NonSameLexical";
  case D3GateExclusionReason::CandidateProbeOrSpeculativeContext:
    return "CandidateProbeOrSpeculativeContext";
  case D3GateExclusionReason::NonFinalSemanticTraversal:
    return "NonFinalSemanticTraversal";
  case D3GateExclusionReason::NestedObservationContext:
    return "NestedObservationContext";
  }
  return "WrongRoute";
}

bool operator==(const D3ObservationSentinel &lhs,
                const D3ObservationSentinel &rhs) {
  return std::tie(lhs.CallNodeIdentity, lhs.ActualNodeIdentity,
                  lhs.ResolvedFunctionIdentity, lhs.ArgumentCount, lhs.Callee,
                  lhs.ActualSyntax, lhs.ActualResolvedType, lhs.SourceSymbolId,
                  lhs.SourceIdentity, lhs.SourcePlaceState, lhs.SourceInitMask,
                  lhs.SourceMoved, lhs.SourceUsed, lhs.SourceMutated,
                  lhs.SourcePlaceAlias, lhs.SourceBorrowedPath,
                  lhs.SourceDependencies, lhs.PALState,
                  lhs.DiagnosticErrorCount, lhs.DiagnosticCodes,
                  lhs.Slice4CopyProofCount, lhs.CopyProofFactCount,
                  lhs.ReferencedCopyProof, lhs.LastDependencies,
                  lhs.PrecomputingCaptures, lhs.ExpectedCedeTransfer,
                  lhs.AllowPermissionSuffix) ==
             std::tie(rhs.CallNodeIdentity, rhs.ActualNodeIdentity,
                      rhs.ResolvedFunctionIdentity, rhs.ArgumentCount,
                      rhs.Callee, rhs.ActualSyntax, rhs.ActualResolvedType,
                      rhs.SourceSymbolId, rhs.SourceIdentity,
                      rhs.SourcePlaceState, rhs.SourceInitMask, rhs.SourceMoved,
                      rhs.SourceUsed, rhs.SourceMutated, rhs.SourcePlaceAlias,
                      rhs.SourceBorrowedPath, rhs.SourceDependencies,
                      rhs.PALState, rhs.DiagnosticErrorCount,
                      rhs.DiagnosticCodes, rhs.Slice4CopyProofCount,
                      rhs.CopyProofFactCount, rhs.ReferencedCopyProof,
                      rhs.LastDependencies, rhs.PrecomputingCaptures,
                      rhs.ExpectedCedeTransfer, rhs.AllowPermissionSuffix) &&
         lhs.Evidence == rhs.Evidence;
}

std::vector<std::string>
differingD3SentinelFields(const D3ObservationSentinel &before,
                          const D3ObservationSentinel &after) {
  std::vector<std::string> fields;
#define D3_COMPARE(field, name)                                                \
  do {                                                                         \
    if (before.field != after.field)                                           \
      fields.push_back(name);                                                  \
  } while (false)
  D3_COMPARE(CallNodeIdentity, "call_node_identity");
  D3_COMPARE(ActualNodeIdentity, "actual_node_identity");
  D3_COMPARE(ResolvedFunctionIdentity, "resolved_function_identity");
  D3_COMPARE(ArgumentCount, "argument_count");
  D3_COMPARE(Callee, "callee");
  D3_COMPARE(ActualSyntax, "actual_syntax");
  D3_COMPARE(ActualResolvedType, "actual_resolved_type");
  D3_COMPARE(SourceSymbolId, "source_symbol_id");
  D3_COMPARE(SourceIdentity, "source_identity");
  D3_COMPARE(SourcePlaceState, "source_place_state");
  D3_COMPARE(SourceInitMask, "source_init_mask");
  D3_COMPARE(SourceMoved, "source_moved");
  D3_COMPARE(SourceUsed, "source_used");
  D3_COMPARE(SourceMutated, "source_mutated");
  D3_COMPARE(SourcePlaceAlias, "source_place_alias");
  D3_COMPARE(SourceBorrowedPath, "source_borrowed_path");
  D3_COMPARE(SourceDependencies, "source_dependencies");
  D3_COMPARE(PALState, "pal_state");
  D3_COMPARE(DiagnosticErrorCount, "diagnostic_error_count");
  D3_COMPARE(DiagnosticCodes, "diagnostic_codes");
  D3_COMPARE(Evidence, "evidence");
  D3_COMPARE(Slice4CopyProofCount, "slice4_copy_proof_count");
  D3_COMPARE(CopyProofFactCount, "copy_proof_fact_count");
  D3_COMPARE(ReferencedCopyProof, "referenced_copy_proof");
  D3_COMPARE(LastDependencies, "last_dependencies");
  D3_COMPARE(PrecomputingCaptures, "precomputing_captures");
  D3_COMPARE(ExpectedCedeTransfer, "expected_cede_transfer");
  D3_COMPARE(AllowPermissionSuffix, "allow_permission_suffix");
#undef D3_COMPARE
  return fields;
}

D3ObservationScope::D3ObservationScope(D3ObservationSession *session)
    : Session(session) {
  if (Session)
    Session->beginObservation();
}

D3ObservationScope::D3ObservationScope(D3ObservationScope &&other) noexcept
    : Session(other.Session) {
  other.Session = nullptr;
}

D3ObservationScope &
D3ObservationScope::operator=(D3ObservationScope &&other) noexcept {
  if (this == &other)
    return *this;
  if (Session)
    Session->endObservation();
  Session = other.Session;
  other.Session = nullptr;
  return *this;
}

D3ObservationScope::~D3ObservationScope() {
  if (Session)
    Session->endObservation();
}

void D3ObservationSession::beginObservation() {
  ++ActiveObservationDepth;
  ++ConsideredCallCount;
}

void D3ObservationSession::endObservation() {
  if (ActiveObservationDepth != 0)
    --ActiveObservationDepth;
}

void D3ObservationSession::noteGateExclusion(D3GateExclusionReason reason) {
  switch (reason) {
  case D3GateExclusionReason::WrongRoute:
    ++WrongRouteCount;
    break;
  case D3GateExclusionReason::NonSameLexical:
    ++NonSameLexicalCount;
    break;
  case D3GateExclusionReason::CandidateProbeOrSpeculativeContext:
    ++CandidateProbeCount;
    break;
  case D3GateExclusionReason::NonFinalSemanticTraversal:
    ++NonFinalTraversalCount;
    break;
  case D3GateExclusionReason::NestedObservationContext:
    ++NestedContextCount;
    break;
  }
}

void D3ObservationSession::append(D3ObservationEnvelope envelope) {
  Envelopes.push_back(std::move(envelope));
}

void D3ObservationSession::dumpJSON(std::ostream &out) const {
  std::vector<const D3ObservationEnvelope *> ordered;
  ordered.reserve(Envelopes.size());
  for (const auto &envelope : Envelopes)
    ordered.push_back(&envelope);
  std::sort(ordered.begin(), ordered.end(),
            [](const auto *lhs, const auto *rhs) {
              const auto &a = lhs->FactoryRecord.input().Pre;
              const auto &b = rhs->FactoryRecord.input().Pre;
              return std::tie(a.CallLocation.File, a.CallLocation.Line,
                              a.CallLocation.Column, a.CalleeName) <
                     std::tie(b.CallLocation.File, b.CallLocation.Line,
                              b.CallLocation.Column, b.CalleeName);
            });
  bool integrity = ConsideredCallCount == FactoryInvocationCount &&
                   FactoryInvocationCount == Envelopes.size();
  for (const auto *envelope : ordered)
    integrity = integrity && envelope->PreFactCaptureUnchanged &&
                envelope->PostCacheAndFactoryUnchanged;

  out << "{\"schema\":\"toka.internal.m1b-d3-direct-call-observation\","
         "\"version\":1,\"status\":\"shadow-only\",\"integrity\":"
      << (integrity ? "true" : "false")
      << ",\"considered_call_count\":" << ConsideredCallCount
      << ",\"factory_invocation_count\":" << FactoryInvocationCount
      << ",\"gate_exclusions\":{\"WrongRoute\":" << WrongRouteCount
      << ",\"NonSameLexical\":" << NonSameLexicalCount
      << ",\"CandidateProbeOrSpeculativeContext\":" << CandidateProbeCount
      << ",\"NonFinalSemanticTraversal\":" << NonFinalTraversalCount
      << ",\"NestedObservationContext\":" << NestedContextCount
      << "},\"envelopes\":[";
  for (size_t i = 0; i < ordered.size(); ++i) {
    if (i != 0)
      out << ',';
    const auto &envelope = *ordered[i];
    out << "{\"factory_record\":";
    dumpFactoryRecord(out, envelope.FactoryRecord);
    out << ",\"comparison\":{\"pre_fact_capture_unchanged\":"
        << (envelope.PreFactCaptureUnchanged ? "true" : "false")
        << ",\"post_cache_and_factory_unchanged\":"
        << (envelope.PostCacheAndFactoryUnchanged ? "true" : "false")
        << ",\"differing_sentinel_fields\":";
    dumpStringArray(out, envelope.DifferingSentinelFields);
    out << "}}";
  }
  out << "]}\n";
}

} // namespace toka
