// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/AuthorityFacts.h"
#include <algorithm>

namespace toka {

std::pair<CleanupClassStore, CleanupClassStoreError>
CleanupClassStore::build(std::vector<CleanupClassFact> entries) {
  CleanupClassStore store;
  std::sort(
      entries.begin(), entries.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.Type < rhs.Type; });
  for (auto &entry : entries) {
    if (!entry.Type.valid())
      return {CleanupClassStore{}, CleanupClassStoreError::InvalidTypeIdentity};
    if (!store.Entries.empty() && store.Entries.back().Type == entry.Type) {
      if (!(store.Entries.back() == entry))
        return {CleanupClassStore{},
                CleanupClassStoreError::ConflictingTypeClassification};
      continue;
    }
    store.Entries.push_back(std::move(entry));
  }
  return {std::move(store), CleanupClassStoreError::None};
}

const CleanupClassFact *
CleanupClassStore::lookup(const ConcreteTypeId &type) const {
  auto found = std::lower_bound(
      Entries.begin(), Entries.end(), type,
      [](const CleanupClassFact &entry, const ConcreteTypeId &key) {
        return entry.Type < key;
      });
  return found != Entries.end() && found->Type == type ? &*found : nullptr;
}

SourceCleanupFact SourceCleanupFact::noCleanup(const PlaceId &place,
                                               const ConcreteTypeId &type) {
  SourceCleanupFact result;
  result.Kind = SourceCleanupKind::NoCleanup;
  result.Reason = AuthorityIndeterminateReason::None;
  result.Place = place;
  result.Type = type;
  return result;
}

SourceCleanupFact SourceCleanupFact::armed(CleanupId cleanup, PlaceId place,
                                           ConcreteTypeId type,
                                           uint64_t initMask) {
  SourceCleanupFact result;
  result.Kind = SourceCleanupKind::ArmedWholePlace;
  result.Reason = AuthorityIndeterminateReason::None;
  result.Cleanup = std::move(cleanup);
  result.Place = std::move(place);
  result.Type = std::move(type);
  result.InitMask = initMask;
  return result;
}

SourceCleanupFact
SourceCleanupFact::indeterminate(AuthorityIndeterminateReason reason) {
  SourceCleanupFact result;
  result.Kind = SourceCleanupKind::Indeterminate;
  result.Reason = reason;
  return result;
}

bool SourceCleanupFact::valid() const {
  switch (Kind) {
  case SourceCleanupKind::NoCleanup:
    return Reason == AuthorityIndeterminateReason::None && Place &&
           Place->valid() && Type && Type->valid() && !Cleanup && InitMask == 0;
  case SourceCleanupKind::ArmedWholePlace:
    return Reason == AuthorityIndeterminateReason::None && Cleanup &&
           Cleanup->valid() && Place && Place->valid() && Type &&
           Type->valid() && InitMask != 0;
  case SourceCleanupKind::Indeterminate:
    return Reason != AuthorityIndeterminateReason::None && !Cleanup && !Place &&
           !Type && InitMask == 0;
  }
  return false;
}

LegacyCedeRequirement
classifyLegacyCedeRequirement(const RawLegacyCedePolicyInput &input) {
  if (!input.valid())
    return LegacyCedeRequirement::Indeterminate;
  if (input.category() == LegacyPolicyTypeCategory::BorrowedView)
    return LegacyCedeRequirement::ExplicitRequired;
  if (input.category() != LegacyPolicyTypeCategory::Shape)
    return LegacyCedeRequirement::Indeterminate;
  const auto &soul = input.canonicalSoul();
  if (soul == "str" || soul == "bytes" || soul == "cstr" ||
      soul == "ViewStrSplitIterator" || soul == "ViewStrLinesIterator" ||
      soul == "string" || soul == "TimerHeap")
    return LegacyCedeRequirement::ExplicitRequired;
  if (soul == "SlabID")
    return LegacyCedeRequirement::ImplicitExempt;
  if (input.dropFact() == LegacyPolicyDropFact::HasDrop)
    return LegacyCedeRequirement::ExplicitRequired;
  if (input.dropFact() == LegacyPolicyDropFact::NoDrop)
    return LegacyCedeRequirement::ImplicitExempt;
  return LegacyCedeRequirement::Indeterminate;
}

std::pair<AuthorityFactsRevision, AuthorityBuildError>
AuthorityFactsRevision::build(AuthorityRevisionId id,
                              std::vector<AuthorityFactRecord> records) {
  if (!id.valid())
    return {AuthorityFactsRevision{}, AuthorityBuildError::MalformedRevision};
  std::sort(records.begin(), records.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.Key < rhs.Key; });
  AuthorityFactsRevision revision;
  revision.Identity = std::move(id);
  for (auto &record : records) {
    if (!record.Key.valid())
      return {AuthorityFactsRevision{},
              AuthorityBuildError::InvalidObservationIdentity};
    auto expectedFull = SemanticIdentityBuilder::fullExpression(
        record.Key.FullExpressionRootNode);
    if (!expectedFull || expectedFull.value() != record.Key.FullExpression)
      return {AuthorityFactsRevision{},
              AuthorityBuildError::InvalidFullExpressionIdentity};
    auto expectedObservation = SemanticIdentityBuilder::authorityObservation(
        record.Key.FullExpression, record.Key.ObservationNode.canonicalKey());
    if (!expectedObservation ||
        expectedObservation.value() != record.Key.Observation)
      return {AuthorityFactsRevision{},
              AuthorityBuildError::InvalidObservationIdentity};
    if (!record.Cleanup.valid())
      return {AuthorityFactsRevision{}, AuthorityBuildError::MalformedRevision};
    if (record.Place) {
      if (!record.Place->valid())
        return {AuthorityFactsRevision{},
                AuthorityBuildError::InvalidPlaceIdentity};
      auto expectedRoot = SemanticIdentityBuilder::rootSymbol(
          record.Place->Owner, record.Place->Declaration.canonicalKey());
      if (!expectedRoot || expectedRoot.value() != record.Place->Place.root())
        return {AuthorityFactsRevision{},
                AuthorityBuildError::OwnerDeclarationMismatch};
      if (record.Cleanup.Place && *record.Cleanup.Place != record.Place->Place)
        return {AuthorityFactsRevision{},
                AuthorityBuildError::DanglingCrossReference};
      if (record.Cleanup.Type && *record.Cleanup.Type != record.Place->Type)
        return {AuthorityFactsRevision{},
                AuthorityBuildError::DanglingCrossReference};
      if (record.Cleanup.Cleanup) {
        auto expectedCleanup = SemanticIdentityBuilder::cleanup(
            record.Place->Place.root(), record.Place->Type);
        if (!expectedCleanup ||
            expectedCleanup.value() != *record.Cleanup.Cleanup)
          return {AuthorityFactsRevision{},
                  AuthorityBuildError::DanglingCrossReference};
      }
    } else if (record.Cleanup.Kind != SourceCleanupKind::Indeterminate) {
      return {AuthorityFactsRevision{},
              AuthorityBuildError::DanglingCrossReference};
    }
    if (!revision.Records.empty() &&
        revision.Records.back().Key == record.Key) {
      if (!(revision.Records.back() == record))
        return {AuthorityFactsRevision{},
                AuthorityBuildError::ConflictingPayload};
      continue;
    }
    revision.Records.push_back(std::move(record));
  }
  return {std::move(revision), AuthorityBuildError::None};
}

const char *toString(AuthoritySnapshotPhase) { return "PreEvaluation"; }
const char *toString(CleanupClassKind value) {
  switch (value) {
  case CleanupClassKind::OwnedWholeCleanup:
    return "OwnedWholeCleanup";
  case CleanupClassKind::NoCleanup:
    return "NoCleanup";
  case CleanupClassKind::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}
const char *toString(CleanupClassIndeterminateReason value) {
  switch (value) {
  case CleanupClassIndeterminateReason::None:
    return "None";
  case CleanupClassIndeterminateReason::MissingConcreteTypeGraph:
    return "MissingConcreteTypeGraph";
  case CleanupClassIndeterminateReason::ColdAnalysis:
    return "ColdAnalysis";
  case CleanupClassIndeterminateReason::GenericOrSourceHidden:
    return "GenericOrSourceHidden";
  case CleanupClassIndeterminateReason::RecursiveCycle:
    return "RecursiveCycle";
  case CleanupClassIndeterminateReason::ConflictingDropFacts:
    return "ConflictingDropFacts";
  case CleanupClassIndeterminateReason::UnsupportedCarrier:
    return "UnsupportedCarrier";
  }
  return "MissingConcreteTypeGraph";
}
const char *toString(CleanupClassSource value) {
  switch (value) {
  case CleanupClassSource::BorrowedView:
    return "BorrowedView";
  case CleanupClassSource::BuiltinOwnedBuffer:
    return "BuiltinOwnedBuffer";
  case CleanupClassSource::ExplicitEncapDrop:
    return "ExplicitEncapDrop";
  case CleanupClassSource::StructuralOwnedField:
    return "StructuralOwnedField";
  case CleanupClassSource::ProvenNoCleanup:
    return "ProvenNoCleanup";
  case CleanupClassSource::Incomplete:
    return "Incomplete";
  }
  return "Incomplete";
}
const char *toString(SourceCleanupKind value) {
  switch (value) {
  case SourceCleanupKind::NoCleanup:
    return "NoCleanup";
  case SourceCleanupKind::ArmedWholePlace:
    return "ArmedWholePlace";
  case SourceCleanupKind::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}
const char *toString(AuthorityIndeterminateReason value) {
  switch (value) {
  case AuthorityIndeterminateReason::None:
    return "None";
  case AuthorityIndeterminateReason::MissingConcreteTypeId:
    return "MissingConcreteTypeId";
  case AuthorityIndeterminateReason::MissingCleanupClass:
    return "MissingCleanupClass";
  case AuthorityIndeterminateReason::CleanupClassColdOrIncomplete:
    return "CleanupClassColdOrIncomplete";
  case AuthorityIndeterminateReason::CleanupClassConflict:
    return "CleanupClassConflict";
  case AuthorityIndeterminateReason::MissingLegacyDropFact:
    return "MissingLegacyDropFact";
  case AuthorityIndeterminateReason::ZeroOrAmbiguousInitMask:
    return "ZeroOrAmbiguousInitMask";
  case AuthorityIndeterminateReason::PlaceNotLive:
    return "PlaceNotLive";
  case AuthorityIndeterminateReason::IncompleteOwnerOrDeclarationIdentity:
    return "IncompleteOwnerOrDeclarationIdentity";
  }
  return "MissingCleanupClass";
}
const char *toString(LegacyPolicyTypeCategory value) {
  switch (value) {
  case LegacyPolicyTypeCategory::Shape:
    return "Shape";
  case LegacyPolicyTypeCategory::BorrowedView:
    return "BorrowedView";
  case LegacyPolicyTypeCategory::Unsupported:
    return "Unsupported";
  }
  return "Unsupported";
}
const char *toString(LegacyPolicyDropFact value) {
  switch (value) {
  case LegacyPolicyDropFact::HasDrop:
    return "HasDrop";
  case LegacyPolicyDropFact::NoDrop:
    return "NoDrop";
  case LegacyPolicyDropFact::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}
const char *toString(LegacyCedeRequirement value) {
  switch (value) {
  case LegacyCedeRequirement::ExplicitRequired:
    return "ExplicitRequired";
  case LegacyCedeRequirement::ImplicitExempt:
    return "ImplicitExempt";
  case LegacyCedeRequirement::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}
const char *toString(AuthorityBuildError value) {
  switch (value) {
  case AuthorityBuildError::None:
    return "None";
  case AuthorityBuildError::InvalidFullExpressionIdentity:
    return "InvalidFullExpressionIdentity";
  case AuthorityBuildError::InvalidObservationIdentity:
    return "InvalidObservationIdentity";
  case AuthorityBuildError::InvalidPlaceIdentity:
    return "InvalidPlaceIdentity";
  case AuthorityBuildError::StaleSymbolLookupWitness:
    return "StaleSymbolLookupWitness";
  case AuthorityBuildError::OwnerDeclarationMismatch:
    return "OwnerDeclarationMismatch";
  case AuthorityBuildError::ExactPlaceMismatch:
    return "ExactPlaceMismatch";
  case AuthorityBuildError::DuplicateFactKey:
    return "DuplicateFactKey";
  case AuthorityBuildError::ConflictingPayload:
    return "ConflictingPayload";
  case AuthorityBuildError::DanglingCrossReference:
    return "DanglingCrossReference";
  case AuthorityBuildError::MalformedRevision:
    return "MalformedRevision";
  }
  return "MalformedRevision";
}

const char *toString(AuthorityExclusionReason value) {
  switch (value) {
  case AuthorityExclusionReason::UnsupportedFullExpressionRoot:
    return "UnsupportedFullExpressionRoot";
  case AuthorityExclusionReason::NonFinalOrSpeculativeTraversal:
    return "NonFinalOrSpeculativeTraversal";
  case AuthorityExclusionReason::GlobalOrSourceHiddenBinding:
    return "GlobalOrSourceHiddenBinding";
  case AuthorityExclusionReason::PlaceAliasOrProjection:
    return "PlaceAliasOrProjection";
  case AuthorityExclusionReason::TemporaryOrMissingBinding:
    return "TemporaryOrMissingBinding";
  case AuthorityExclusionReason::CapturedOrGeneratedBinding:
    return "CapturedOrGeneratedBinding";
  case AuthorityExclusionReason::GenericOrASTClone:
    return "GenericOrASTClone";
  case AuthorityExclusionReason::UnsupportedTypeCategory:
    return "UnsupportedTypeCategory";
  case AuthorityExclusionReason::UnsupportedPartialState:
    return "UnsupportedPartialState";
  }
  return "UnsupportedTypeCategory";
}

} // namespace toka
