// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/PureNominalOverloadProbeAudit.h"
#include <sstream>
#include <unordered_set>

using namespace toka;

bool g_JsonDiagnostics = false;

template <typename Value> struct ConstantHash {
  size_t operator()(const Value &) const noexcept { return 0; }
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

namespace {

DeclarationId declaration(const std::string &name) {
  auto result = SemanticIdentityBuilder::declaration("/test/probe.tk", name);
  return result ? result.value() : DeclarationId{};
}

SemanticNodeId callSite() {
  auto result =
      SemanticIdentityBuilder::semanticNode("/test/probe.tk", "call:1");
  return result ? result.value() : SemanticNodeId{};
}

NominalShapeId nominal(const std::string &path, const std::string &name) {
  return NominalShapeId::fromSourcePath(path, name, 0);
}

} // namespace

int main() {
  const auto model = nominal("/test/model.tk", "Model");
  const auto other = nominal("/test/other.tk", "Other");
  const auto sameSpellingOtherOwner = nominal("/test/other/model.tk", "Model");
  const auto modelDecl = declaration("choose-model");
  const auto otherDecl = declaration("choose-other");

  std::unordered_set<NominalShapeId, ConstantHash<NominalShapeId>> nominals;
  nominals.insert(model);
  nominals.insert(sameSpellingOtherOwner);
  CHECK(nominals.size() == 2);

  auto unique = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      callSite(), model, {{modelDecl, 0, model}, {otherDecl, 1, other}}));
  CHECK(unique);
  CHECK(unique.Result->disposition() == D4ProbeDisposition::UniqueCompatible);
  CHECK(unique.Result->selectedDeclaration() == modelDecl);
  CHECK(unique.Result->candidates().size() == 2);
  CHECK(unique.Result->candidates()[0].Compatible);
  CHECK(!unique.Result->candidates()[1].Compatible);
  auto reverse = PureNominalOverloadProbe::run(
      D4PureNominalProbeInput(callSite(), model,
                              {{modelDecl, 0, model}, {otherDecl, 1, other}}),
      D4ProbeSchedule::ReverseForTesting);
  CHECK(reverse && reverse.Result == unique.Result);

  auto zero = PureNominalOverloadProbe::run(
      D4PureNominalProbeInput(callSite(), sameSpellingOtherOwner,
                              {{modelDecl, 0, model}, {otherDecl, 1, other}}));
  CHECK(zero);
  CHECK(zero.Result->disposition() == D4ProbeDisposition::LegacyRequired);
  CHECK(zero.Result->legacyReason() == D4LegacyReason::ZeroCompatible);

  auto multiple = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      callSite(), model, {{modelDecl, 0, model}, {otherDecl, 1, model}}));
  CHECK(multiple);
  CHECK(multiple.Result->legacyReason() == D4LegacyReason::MultipleCompatible);

  auto invalidCall = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      SemanticNodeId{}, model, {{modelDecl, 0, model}, {otherDecl, 1, other}}));
  CHECK(!invalidCall &&
        invalidCall.Error ==
            D4ProbeInfrastructureError::InvalidCallSiteIdentity);

  auto invalidActual = PureNominalOverloadProbe::run(
      D4PureNominalProbeInput(callSite(), std::nullopt,
                              {{modelDecl, 0, model}, {otherDecl, 1, other}}));
  CHECK(!invalidActual &&
        invalidActual.Error ==
            D4ProbeInfrastructureError::InvalidNominalShapeId);

  auto invalidFormal = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      callSite(), model,
      {{modelDecl, 0, std::nullopt}, {otherDecl, 1, other}}));
  CHECK(!invalidFormal &&
        invalidFormal.Error ==
            D4ProbeInfrastructureError::InvalidNominalShapeId);

  auto duplicateDeclaration =
      PureNominalOverloadProbe::run(D4PureNominalProbeInput(
          callSite(), model, {{modelDecl, 0, model}, {modelDecl, 1, other}}));
  CHECK(!duplicateDeclaration &&
        duplicateDeclaration.Error ==
            D4ProbeInfrastructureError::DuplicateCandidateIdentity);

  auto duplicateOrdinal = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      callSite(), model, {{modelDecl, 0, model}, {otherDecl, 0, other}}));
  CHECK(!duplicateOrdinal &&
        duplicateOrdinal.Error ==
            D4ProbeInfrastructureError::DuplicateLegacyOrdinal);

  auto nonContiguous = PureNominalOverloadProbe::run(D4PureNominalProbeInput(
      callSite(), model, {{modelDecl, 0, model}, {otherDecl, 2, other}}));
  CHECK(!nonContiguous &&
        nonContiguous.Error ==
            D4ProbeInfrastructureError::NonContiguousLegacyOrdinal);

  auto malformed = PureNominalOverloadProbe::run(
      D4PureNominalProbeInput(callSite(), model, {{modelDecl, 0, model}}));
  CHECK(!malformed &&
        malformed.Error == D4ProbeInfrastructureError::MalformedBatch);

  int modelPointer = 1;
  int otherPointer = 2;
  std::vector<D4FrozenMappingEntry> mapping = {{modelDecl, 0, &modelPointer},
                                               {otherDecl, 1, &otherPointer}};
  CHECK(!validateD4FrozenMapping(mapping, otherDecl, &otherPointer));
  CHECK(validateD4FrozenMapping(mapping, otherDecl, &modelPointer) ==
        D4ProbeInfrastructureError::MalformedBatch);
  CHECK(validateD4FrozenMapping(mapping, std::nullopt, &modelPointer) ==
        D4ProbeInfrastructureError::MalformedBatch);
  auto duplicateMapping = mapping;
  duplicateMapping[1].Declaration = modelDecl;
  CHECK(validateD4FrozenMapping(duplicateMapping, std::nullopt, nullptr) ==
        D4ProbeInfrastructureError::DuplicateCandidateIdentity);
  auto ordinalMapping = mapping;
  ordinalMapping[1].LegacyOrdinal = 2;
  CHECK(validateD4FrozenMapping(ordinalMapping, std::nullopt, nullptr) ==
        D4ProbeInfrastructureError::NonContiguousLegacyOrdinal);
  auto duplicateOrdinalMapping = mapping;
  duplicateOrdinalMapping[1].LegacyOrdinal = 0;
  CHECK(validateD4FrozenMapping(duplicateOrdinalMapping, std::nullopt,
                               nullptr) ==
        D4ProbeInfrastructureError::DuplicateLegacyOrdinal);

  const D4ProbeInfrastructureError infrastructureErrors[] = {
      D4ProbeInfrastructureError::InvalidCallSiteIdentity,
      D4ProbeInfrastructureError::InvalidNominalShapeId,
      D4ProbeInfrastructureError::DuplicateCandidateIdentity,
      D4ProbeInfrastructureError::DuplicateLegacyOrdinal,
      D4ProbeInfrastructureError::NonContiguousLegacyOrdinal,
      D4ProbeInfrastructureError::MalformedBatch};
  for (auto error : infrastructureErrors)
    CHECK(parseD4ProbeInfrastructureError(toString(error)) == error);
  CHECK(!parseD4ProbeInfrastructureError("UnknownFailure"));

  const D4ProbeExclusionReason exclusions[] = {
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
  D4ProbeAuditSession audit;
  for (auto exclusion : exclusions) {
    D4ProbeAuditRecord record;
    record.Exclusion = exclusion;
    audit.append(std::move(record));
  }
  std::ostringstream auditJSON;
  audit.dumpJSON(auditJSON);
  for (auto exclusion : exclusions)
    CHECK(auditJSON.str().find(toString(exclusion)) != std::string::npos);
  return 0;
}
