// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/AuthorityFacts.h"
#include <unordered_set>

using namespace toka;

bool g_JsonDiagnostics = false;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

template <typename Value> struct ConstantHash {
  size_t operator()(const Value &) const noexcept { return 0; }
};

int main() {
  auto rootNode =
      SemanticIdentityBuilder::semanticNode("/test/main.tk", "10:5:binary");
  auto otherRootNode =
      SemanticIdentityBuilder::semanticNode("/test/main.tk", "11:5:binary");
  CHECK(rootNode && otherRootNode);
  auto full = SemanticIdentityBuilder::fullExpression(rootNode.value());
  auto otherFull =
      SemanticIdentityBuilder::fullExpression(otherRootNode.value());
  CHECK(full && otherFull && full.value() != otherFull.value());
  auto observationNode =
      SemanticIdentityBuilder::semanticNode("/test/main.tk", "10:12:variable");
  CHECK(observationNode);
  auto observation = SemanticIdentityBuilder::authorityObservation(
      full.value(), observationNode.value().canonicalKey());
  auto foreignObservation = SemanticIdentityBuilder::authorityObservation(
      otherFull.value(), observationNode.value().canonicalKey());
  CHECK(observation && foreignObservation);

  auto owner =
      SemanticIdentityBuilder::declaration("/test/main.tk", "8:1:main");
  auto declaration =
      SemanticIdentityBuilder::declaration("/test/main.tk", "9:10:value");
  CHECK(owner && declaration);
  auto root = SemanticIdentityBuilder::rootSymbol(
      owner.value(), declaration.value().canonicalKey());
  auto type = SemanticIdentityBuilder::concreteType("/test/types.tk", "Model");
  CHECK(root && type);
  PlaceId place(root.value());
  auto cleanup = SemanticIdentityBuilder::cleanup(root.value(), type.value());
  CHECK(cleanup);

  CleanupClassFact owned{type.value(), CleanupClassKind::OwnedWholeCleanup,
                         CleanupClassIndeterminateReason::None,
                         CleanupClassSource::ExplicitEncapDrop};
  auto plainType =
      SemanticIdentityBuilder::concreteType("/test/types.tk", "Plain");
  CHECK(plainType);
  CleanupClassFact plain{plainType.value(), CleanupClassKind::NoCleanup,
                         CleanupClassIndeterminateReason::None,
                         CleanupClassSource::ProvenNoCleanup};
  auto store = CleanupClassStore::build({owned, plain, owned});
  CHECK(store.second == CleanupClassStoreError::None);
  CHECK(store.first.size() == 2 && store.first.lookup(type.value()) &&
        store.first.lookup(type.value())->Kind ==
            CleanupClassKind::OwnedWholeCleanup);
  auto conflicting = owned;
  conflicting.Kind = CleanupClassKind::NoCleanup;
  CHECK(CleanupClassStore::build({owned, conflicting}).second ==
        CleanupClassStoreError::ConflictingTypeClassification);

  auto sourceCleanup =
      SourceCleanupFact::armed(cleanup.value(), place, type.value(), 1);
  CHECK(sourceCleanup.valid());
  CHECK(SourceCleanupFact::noCleanup(place, plainType.value()).valid());
  CHECK(SourceCleanupFact::indeterminate(
            AuthorityIndeterminateReason::MissingCleanupClass)
            .valid());

  RawLegacyCedePolicyInput slab(type.value(), "SlabID",
                                LegacyPolicyTypeCategory::Shape,
                                LegacyPolicyDropFact::HasDrop);
  CHECK(classifyLegacyCedeRequirement(slab) ==
        LegacyCedeRequirement::ImplicitExempt);
  RawLegacyCedePolicyInput resource(type.value(), "Resource",
                                    LegacyPolicyTypeCategory::Shape,
                                    LegacyPolicyDropFact::HasDrop);
  CHECK(classifyLegacyCedeRequirement(resource) ==
        LegacyCedeRequirement::ExplicitRequired);

  AuthorityPlaceFact placeFact{
      place,         SymbolLookupWitness{42}, declaration.value(),
      owner.value(), PlaceState::Live,        1,
      type.value()};
  AuthorityFactRecord record;
  record.Key = {rootNode.value(), full.value(), observationNode.value(),
                observation.value(), AuthoritySnapshotPhase::PreEvaluation};
  record.Place = placeFact;
  record.Cleanup = sourceCleanup;
  record.LegacyPolicy = resource;
  auto revisionId =
      SemanticIdentityBuilder::authorityRevision("/test/main.tk", "revision");
  CHECK(revisionId);
  auto revision =
      AuthorityFactsRevision::build(revisionId.value(), {record, record});
  CHECK(revision.second == AuthorityBuildError::None &&
        revision.first.size() == 1);

  auto conflictingRecord = record;
  conflictingRecord.Place->InitMask = 3;
  CHECK(AuthorityFactsRevision::build(revisionId.value(),
                                      {record, conflictingRecord})
            .second == AuthorityBuildError::ConflictingPayload);
  auto foreignRecord = record;
  foreignRecord.Key.Observation = foreignObservation.value();
  CHECK(AuthorityFactsRevision::build(revisionId.value(), {foreignRecord})
            .second == AuthorityBuildError::InvalidObservationIdentity);

  AuthorityPlaceFact otherWitness = placeFact;
  otherWitness.Lookup.SymbolID = 999;
  CHECK(otherWitness.Place == placeFact.Place);
  std::unordered_set<PlaceId, ConstantHash<PlaceId>> places;
  places.insert(placeFact.Place);
  auto otherDeclaration =
      SemanticIdentityBuilder::declaration("/test/main.tk", "12:10:value");
  CHECK(otherDeclaration);
  auto otherRoot = SemanticIdentityBuilder::rootSymbol(
      owner.value(), otherDeclaration.value().canonicalKey());
  CHECK(otherRoot);
  places.insert(PlaceId(otherRoot.value()));
  CHECK(places.size() == 2);
  return 0;
}
