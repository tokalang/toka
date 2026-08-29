// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#pragma once

#include "toka/PlaceState.h"
#include "toka/SemanticModel.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class AuthoritySnapshotPhase : uint8_t { PreEvaluation };

enum class CleanupClassKind : uint8_t {
  OwnedWholeCleanup,
  NoCleanup,
  Indeterminate,
};

enum class CleanupClassIndeterminateReason : uint8_t {
  None,
  MissingConcreteTypeGraph,
  ColdAnalysis,
  GenericOrSourceHidden,
  RecursiveCycle,
  ConflictingDropFacts,
  UnsupportedCarrier,
};

enum class CleanupClassSource : uint8_t {
  BorrowedView,
  BuiltinOwnedBuffer,
  ExplicitEncapDrop,
  StructuralOwnedField,
  ProvenNoCleanup,
  Incomplete,
};

struct CleanupClassFact {
  ConcreteTypeId Type;
  CleanupClassKind Kind = CleanupClassKind::Indeterminate;
  CleanupClassIndeterminateReason Reason =
      CleanupClassIndeterminateReason::MissingConcreteTypeGraph;
  CleanupClassSource Source = CleanupClassSource::Incomplete;

  friend bool operator==(const CleanupClassFact &lhs,
                         const CleanupClassFact &rhs) {
    return lhs.Type == rhs.Type && lhs.Kind == rhs.Kind &&
           lhs.Reason == rhs.Reason && lhs.Source == rhs.Source;
  }
};

enum class CleanupClassStoreError : uint8_t {
  None,
  InvalidTypeIdentity,
  ConflictingTypeClassification,
};

class CleanupClassStore final {
public:
  static std::pair<CleanupClassStore, CleanupClassStoreError>
  build(std::vector<CleanupClassFact> entries);
  const CleanupClassFact *lookup(const ConcreteTypeId &type) const;
  size_t size() const { return Entries.size(); }
  const std::vector<CleanupClassFact> &entries() const { return Entries; }

private:
  std::vector<CleanupClassFact> Entries;
};

struct AuthorityFactKey {
  SemanticNodeId FullExpressionRootNode;
  FullExpressionId FullExpression;
  SemanticNodeId ObservationNode;
  AuthorityObservationId Observation;
  AuthoritySnapshotPhase Phase = AuthoritySnapshotPhase::PreEvaluation;

  bool valid() const {
    return FullExpressionRootNode.valid() && FullExpression.valid() &&
           ObservationNode.valid() && Observation.valid();
  }
  friend bool operator==(const AuthorityFactKey &lhs,
                         const AuthorityFactKey &rhs) {
    return lhs.FullExpressionRootNode == rhs.FullExpressionRootNode &&
           lhs.FullExpression == rhs.FullExpression &&
           lhs.ObservationNode == rhs.ObservationNode &&
           lhs.Observation == rhs.Observation && lhs.Phase == rhs.Phase;
  }
  friend bool operator<(const AuthorityFactKey &lhs,
                        const AuthorityFactKey &rhs) {
    if (lhs.FullExpressionRootNode != rhs.FullExpressionRootNode)
      return lhs.FullExpressionRootNode < rhs.FullExpressionRootNode;
    if (lhs.FullExpression != rhs.FullExpression)
      return lhs.FullExpression < rhs.FullExpression;
    if (lhs.ObservationNode != rhs.ObservationNode)
      return lhs.ObservationNode < rhs.ObservationNode;
    if (lhs.Observation != rhs.Observation)
      return lhs.Observation < rhs.Observation;
    return lhs.Phase < rhs.Phase;
  }
};

struct SymbolLookupWitness {
  uint64_t SymbolID = 0;
};

struct AuthorityPlaceFact {
  PlaceId Place;
  SymbolLookupWitness Lookup;
  DeclarationId Declaration;
  DeclarationId Owner;
  PlaceState State = PlaceState::Never;
  uint64_t InitMask = 0;
  ConcreteTypeId Type;

  bool valid() const {
    return Place.valid() && Lookup.SymbolID != 0 && Declaration.valid() &&
           Owner.valid() && Type.valid();
  }
  friend bool operator==(const AuthorityPlaceFact &lhs,
                         const AuthorityPlaceFact &rhs) {
    return lhs.Place == rhs.Place &&
           lhs.Lookup.SymbolID == rhs.Lookup.SymbolID &&
           lhs.Declaration == rhs.Declaration && lhs.Owner == rhs.Owner &&
           lhs.State == rhs.State && lhs.InitMask == rhs.InitMask &&
           lhs.Type == rhs.Type;
  }
};

enum class SourceCleanupKind : uint8_t {
  NoCleanup,
  ArmedWholePlace,
  Indeterminate,
};

enum class AuthorityIndeterminateReason : uint8_t {
  None,
  MissingConcreteTypeId,
  MissingCleanupClass,
  CleanupClassColdOrIncomplete,
  CleanupClassConflict,
  MissingLegacyDropFact,
  ZeroOrAmbiguousInitMask,
  PlaceNotLive,
  IncompleteOwnerOrDeclarationIdentity,
};

struct SourceCleanupFact {
  SourceCleanupKind Kind = SourceCleanupKind::Indeterminate;
  AuthorityIndeterminateReason Reason =
      AuthorityIndeterminateReason::MissingCleanupClass;
  std::optional<CleanupId> Cleanup;
  std::optional<PlaceId> Place;
  std::optional<ConcreteTypeId> Type;
  uint64_t InitMask = 0;

  static SourceCleanupFact noCleanup(const PlaceId &place,
                                     const ConcreteTypeId &type);
  static SourceCleanupFact armed(CleanupId cleanup, PlaceId place,
                                 ConcreteTypeId type, uint64_t initMask);
  static SourceCleanupFact indeterminate(AuthorityIndeterminateReason reason);
  bool valid() const;
  friend bool operator==(const SourceCleanupFact &lhs,
                         const SourceCleanupFact &rhs) {
    return lhs.Kind == rhs.Kind && lhs.Reason == rhs.Reason &&
           lhs.Cleanup == rhs.Cleanup && lhs.Place == rhs.Place &&
           lhs.Type == rhs.Type && lhs.InitMask == rhs.InitMask;
  }
};

enum class LegacyPolicyTypeCategory : uint8_t {
  Shape,
  BorrowedView,
  Unsupported,
};
enum class LegacyPolicyDropFact : uint8_t {
  HasDrop,
  NoDrop,
  Indeterminate,
};
enum class LegacyCedeRequirement : uint8_t {
  ExplicitRequired,
  ImplicitExempt,
  Indeterminate,
};

class RawLegacyCedePolicyInput final {
public:
  RawLegacyCedePolicyInput(ConcreteTypeId type, std::string canonicalSoul,
                           LegacyPolicyTypeCategory category,
                           LegacyPolicyDropFact drop)
      : Type(std::move(type)), CanonicalSoul(std::move(canonicalSoul)),
        Category(category), Drop(drop) {}
  const ConcreteTypeId &type() const { return Type; }
  const std::string &canonicalSoul() const { return CanonicalSoul; }
  LegacyPolicyTypeCategory category() const { return Category; }
  LegacyPolicyDropFact dropFact() const { return Drop; }
  bool valid() const { return Type.valid() && !CanonicalSoul.empty(); }
  friend bool operator==(const RawLegacyCedePolicyInput &lhs,
                         const RawLegacyCedePolicyInput &rhs) {
    return lhs.Type == rhs.Type && lhs.CanonicalSoul == rhs.CanonicalSoul &&
           lhs.Category == rhs.Category && lhs.Drop == rhs.Drop;
  }

private:
  ConcreteTypeId Type;
  std::string CanonicalSoul;
  LegacyPolicyTypeCategory Category = LegacyPolicyTypeCategory::Unsupported;
  LegacyPolicyDropFact Drop = LegacyPolicyDropFact::Indeterminate;
};

LegacyCedeRequirement
classifyLegacyCedeRequirement(const RawLegacyCedePolicyInput &input);

struct AuthorityFactRecord {
  AuthorityFactKey Key;
  std::optional<AuthorityPlaceFact> Place;
  SourceCleanupFact Cleanup;
  std::optional<RawLegacyCedePolicyInput> LegacyPolicy;

  friend bool operator==(const AuthorityFactRecord &lhs,
                         const AuthorityFactRecord &rhs) {
    return lhs.Key == rhs.Key && lhs.Place == rhs.Place &&
           lhs.Cleanup == rhs.Cleanup && lhs.LegacyPolicy == rhs.LegacyPolicy;
  }
};

enum class AuthorityBuildError : uint8_t {
  None,
  InvalidFullExpressionIdentity,
  InvalidObservationIdentity,
  InvalidPlaceIdentity,
  StaleSymbolLookupWitness,
  OwnerDeclarationMismatch,
  ExactPlaceMismatch,
  DuplicateFactKey,
  ConflictingPayload,
  DanglingCrossReference,
  MalformedRevision,
};

enum class AuthorityExclusionReason : uint8_t {
  UnsupportedFullExpressionRoot,
  NonFinalOrSpeculativeTraversal,
  GlobalOrSourceHiddenBinding,
  PlaceAliasOrProjection,
  TemporaryOrMissingBinding,
  CapturedOrGeneratedBinding,
  GenericOrASTClone,
  UnsupportedTypeCategory,
  UnsupportedPartialState,
};

class AuthorityFactsRevision final {
public:
  static std::pair<AuthorityFactsRevision, AuthorityBuildError>
  build(AuthorityRevisionId id, std::vector<AuthorityFactRecord> records);
  const AuthorityRevisionId &id() const { return Identity; }
  const std::vector<AuthorityFactRecord> &records() const { return Records; }
  size_t size() const { return Records.size(); }

private:
  AuthorityRevisionId Identity;
  std::vector<AuthorityFactRecord> Records;
};

const char *toString(AuthoritySnapshotPhase value);
const char *toString(CleanupClassKind value);
const char *toString(CleanupClassIndeterminateReason value);
const char *toString(CleanupClassSource value);
const char *toString(SourceCleanupKind value);
const char *toString(AuthorityIndeterminateReason value);
const char *toString(LegacyPolicyTypeCategory value);
const char *toString(LegacyPolicyDropFact value);
const char *toString(LegacyCedeRequirement value);
const char *toString(AuthorityBuildError value);
const char *toString(AuthorityExclusionReason value);

} // namespace toka
