// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/SemanticModel.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class D3AdmissionKind : uint8_t { Admitted, NotInSlice, Rejected };

enum class D3ExclusionReason : uint8_t {
  Generic,
  VariadicOrDefault,
  MultipleArguments,
  InitOrOutcome,
  AsyncOrExecutionBoundary,
  ReturnDependencyOrRegionEscape,
  NestedObservation,
  Projection,
  SharedIdentity,
  RawOrReferenceIdentity,
  FunctionOrDynIdentity,
  NonCedeScalar,
  NonCedeAggregateTemporary,
  UnsupportedTypeCategory,
};

enum class D3CallValidationError : uint8_t {
  LegacyTypeMismatch,
  BorrowedFormalExplicitCede,
  IndeterminateCopyProof,
  IndeterminateOwnership,
  InvalidWholePlaceAdmission,
  IncompleteObservationFacts,
};

enum class D3ActualCategory : uint8_t {
  Indeterminate,
  WholePlace,
  WholeTemporary,
  Projection,
};

enum class D3TypeCategory : uint8_t {
  Indeterminate,
  Scalar,
  Aggregate,
  BorrowedAggregate,
  OwnedIdentity,
  SharedIdentity,
  RawOrReferenceIdentity,
  FunctionOrDynIdentity,
  Unsupported,
};

enum class D3CopyProof : uint8_t {
  ProvenCopy,
  ProvenNonCopy,
  Indeterminate,
};

enum class D3TransferMode : uint8_t {
  BorrowCapture,
  CopyValue,
  MoveOwned,
  ConsumeTemporary,
};

enum class D3SourceDisposition : uint8_t {
  KeepLive,
  InvalidateWhole,
  NoSourcePlace,
};

enum class D3StateDomain : uint8_t {
  Evaluation,
  PlaceState,
  PAL,
  Dependency,
  CleanupLiability,
  RegionObligation,
};

enum class D3DeltaLane : uint8_t {
  Evaluation,
  Boundary,
  Finalization,
};

enum class D3OwnershipProof : uint8_t {
  Trivial,
  Borrowed,
  Owned,
  Shared,
  Indeterminate,
};

enum class D3BoundaryAccess : uint8_t {
  None,
  SharedBorrow,
  Invalidation,
  Unsupported,
};

enum class D3DependencyRelation : uint8_t {
  None,
  BorrowedCallRegion,
};

enum class D3SubjectKind : uint8_t {
  SourcePlace,
  Destination,
  Loan,
  Cleanup,
};

enum class D3LiabilityKind : uint8_t {
  NoLiability,
  SourcePlaceCleanup,
  TemporaryCleanup,
  SourceRetained,
  DestinationCleanup,
};

enum class D3PatchBuildError : uint8_t {
  None,
  InvalidIdentity,
  ConflictingPayload,
};

const char *toString(D3AdmissionKind value);
const char *toString(D3ExclusionReason value);
const char *toString(D3CallValidationError value);
const char *toString(D3ActualCategory value);
const char *toString(D3TypeCategory value);
const char *toString(D3CopyProof value);
const char *toString(D3TransferMode value);
const char *toString(D3SourceDisposition value);
const char *toString(D3StateDomain value);
const char *toString(D3DeltaLane value);
const char *toString(D3OwnershipProof value);
const char *toString(D3BoundaryAccess value);
const char *toString(D3DependencyRelation value);
const char *toString(D3SubjectKind value);
const char *toString(D3LiabilityKind value);

class D3SubjectIdentity final {
public:
  static D3SubjectIdentity sourcePlace(std::string key) {
    return D3SubjectIdentity(D3SubjectKind::SourcePlace, std::move(key));
  }
  static D3SubjectIdentity destination(std::string key) {
    return D3SubjectIdentity(D3SubjectKind::Destination, std::move(key));
  }
  static D3SubjectIdentity loan(std::string key) {
    return D3SubjectIdentity(D3SubjectKind::Loan, std::move(key));
  }
  static D3SubjectIdentity cleanup(std::string key) {
    return D3SubjectIdentity(D3SubjectKind::Cleanup, std::move(key));
  }

  bool valid() const { return !Key.empty(); }
  D3SubjectKind kind() const { return Kind; }
  const std::string &key() const { return Key; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3SubjectIdentity &lhs,
                         const D3SubjectIdentity &rhs) {
    return lhs.Kind == rhs.Kind && lhs.Key == rhs.Key;
  }

private:
  D3SubjectKind Kind;
  std::string Key;
  D3SubjectIdentity(D3SubjectKind kind, std::string key)
      : Kind(kind), Key(std::move(key)) {}
};

class D3LiabilityFact final {
public:
  static D3LiabilityFact noLiability();
  static D3LiabilityFact sourcePlace(std::string key);
  static D3LiabilityFact temporary(std::string key);
  static D3LiabilityFact sourceRetained(std::string key);
  static D3LiabilityFact destination(std::string key);

  D3LiabilityKind kind() const { return Kind; }
  const std::optional<D3SubjectIdentity> &subject() const { return Subject; }
  bool valid() const {
    return Kind == D3LiabilityKind::NoLiability ||
           (Subject && Subject->valid());
  }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3LiabilityFact &lhs,
                         const D3LiabilityFact &rhs) {
    return lhs.Kind == rhs.Kind && lhs.Subject == rhs.Subject;
  }

private:
  D3LiabilityKind Kind = D3LiabilityKind::NoLiability;
  std::optional<D3SubjectIdentity> Subject;
};

struct D3SourceLocation {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;

  friend bool operator==(const D3SourceLocation &lhs,
                         const D3SourceLocation &rhs) {
    return lhs.File == rhs.File && lhs.Line == rhs.Line &&
           lhs.Column == rhs.Column;
  }
};

// Immutable-by-convention integration DTOs. They contain no AST/Sema/PAL
// objects and are the pure factory's only input.
struct D3PreLegacyDirectCallFacts {
  std::string IdentityOrigin;
  std::string CallWitness;
  std::string CalleeWitness;
  std::string FormalWitness;
  std::string SourceWitness;
  std::string DestinationWitness;
  std::string CallerBindingOwnerWitness;
  std::string CalleeName;
  std::string FormalName;
  std::string FormalType;
  std::string SourceSpelling;
  std::string SourceStateBefore;
  std::string PALStateBefore;
  D3SourceLocation CallLocation;
  D3SourceLocation FormalLocation;
  D3ActualCategory ActualCategory = D3ActualCategory::Indeterminate;
  bool CoreFactsComplete = false;
  bool FormalCeded = false;
  bool ExplicitCede = false;
  bool Generic = false;
  bool VariadicOrDefault = false;
  bool MultipleArguments = false;
  bool InitOrOutcome = false;
  bool AsyncOrExecutionBoundary = false;
  bool ReturnDependencyOrRegionEscape = false;
  bool NestedObservation = false;
  bool SourcePlaceAlias = false;
};

struct D3PostLegacyDirectCallFacts {
  std::string ActualType;
  std::vector<std::string> LegacyDiagnosticCodes;
  D3TypeCategory TypeCategory = D3TypeCategory::Indeterminate;
  D3CopyProof CopyProof = D3CopyProof::Indeterminate;
  D3OwnershipProof OwnershipProof = D3OwnershipProof::Indeterminate;
  D3BoundaryAccess BoundaryAccess = D3BoundaryAccess::Unsupported;
  D3LiabilityFact SourceLiability = D3LiabilityFact::noLiability();
  std::string CleanupWitness;
  bool LegacySucceeded = false;
  bool LegacyTypeMismatch = false;
  bool AdmissionFactsComplete = false;
  bool WholePlaceEligible = false;
  bool LiabilityComplete = false;
  bool RegionFactsComplete = false;
};

struct D3CallObservationInput {
  D3PreLegacyDirectCallFacts Pre;
  D3PostLegacyDirectCallFacts Post;
};

class D3ValidatedTransferEdge final {
public:
  D3ValidatedTransferEdge(const D3ValidatedTransferEdge &) = default;
  D3ValidatedTransferEdge(D3ValidatedTransferEdge &&) noexcept = default;
  D3ValidatedTransferEdge &operator=(const D3ValidatedTransferEdge &) = default;
  D3ValidatedTransferEdge &
  operator=(D3ValidatedTransferEdge &&) noexcept = default;
  const TransferEdgeId &id() const { return Id; }
  const ArgumentPlanId &argumentPlanId() const { return ArgumentPlan; }
  const FormalId &formalId() const { return Formal; }
  const DestinationId &destinationId() const { return Destination; }
  const std::optional<PlaceId> &sourcePlace() const { return SourcePlace; }
  const std::string &typeProof() const { return TypeProof; }
  const std::string &valueCategory() const { return ValueCategory; }
  D3TransferMode transferMode() const { return Transfer; }
  D3SourceDisposition sourceDisposition() const { return Source; }
  D3BoundaryAccess boundaryAccess() const { return BoundaryAccess; }
  D3DependencyRelation dependency() const { return Dependency; }
  const D3LiabilityFact &liabilitySource() const { return LiabilitySource; }
  const D3LiabilityFact &liabilityTarget() const { return LiabilityTarget; }
  bool explicitCede() const { return ExplicitCede; }

  size_t hashValue() const noexcept;
  friend bool operator==(const D3ValidatedTransferEdge &lhs,
                         const D3ValidatedTransferEdge &rhs);

private:
  friend class DirectCallObservationFactory;
  D3ValidatedTransferEdge() = default;
  TransferEdgeId Id;
  ArgumentPlanId ArgumentPlan;
  FormalId Formal;
  DestinationId Destination;
  std::optional<PlaceId> SourcePlace;
  std::string TypeProof;
  std::string ValueCategory;
  D3TransferMode Transfer = D3TransferMode::BorrowCapture;
  D3SourceDisposition Source = D3SourceDisposition::KeepLive;
  D3BoundaryAccess BoundaryAccess = D3BoundaryAccess::Unsupported;
  D3DependencyRelation Dependency = D3DependencyRelation::None;
  D3LiabilityFact LiabilitySource = D3LiabilityFact::noLiability();
  D3LiabilityFact LiabilityTarget = D3LiabilityFact::noLiability();
  bool ExplicitCede = false;
};

class D3DeltaEntry final {
public:
  D3DeltaEntry(const D3DeltaEntry &) = default;
  D3DeltaEntry(D3DeltaEntry &&) noexcept = default;
  D3DeltaEntry &operator=(const D3DeltaEntry &) = default;
  D3DeltaEntry &operator=(D3DeltaEntry &&) noexcept = default;
  const TransferEdgeId &edgeId() const { return Edge; }
  D3StateDomain stateDomain() const { return Domain; }
  const D3SubjectIdentity &subjectIdentity() const { return SubjectIdentity; }
  const std::string &expectedBefore() const { return ExpectedBefore; }
  const std::string &resultAfter() const { return ResultAfter; }
  const std::string &provenance() const { return Provenance; }

  size_t hashValue() const noexcept;
  friend bool operator==(const D3DeltaEntry &lhs, const D3DeltaEntry &rhs);

private:
  friend class DirectCallObservationFactory;
  D3DeltaEntry() = default;
  TransferEdgeId Edge;
  D3StateDomain Domain = D3StateDomain::Evaluation;
  D3SubjectIdentity SubjectIdentity = D3SubjectIdentity::sourcePlace("");
  std::string ExpectedBefore;
  std::string ResultAfter;
  std::string Provenance;
};

class D3DomainDelta final {
public:
  D3DomainDelta(const D3DomainDelta &) = default;
  D3DomainDelta(D3DomainDelta &&) noexcept = default;
  D3DomainDelta &operator=(const D3DomainDelta &) = default;
  D3DomainDelta &operator=(D3DomainDelta &&) noexcept = default;
  D3DeltaLane lane() const { return Lane; }
  const std::vector<D3DeltaEntry> &entries() const { return Entries; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3DomainDelta &lhs, const D3DomainDelta &rhs);

private:
  friend class DirectCallObservationFactory;
  friend class D3ValidatedCall;
  D3DomainDelta() = default;
  D3DeltaLane Lane = D3DeltaLane::Evaluation;
  std::vector<D3DeltaEntry> Entries;
};

struct D3PatchEntrySeed {
  PatchEntryId Identity;
  std::string Payload;
};

struct D3PatchBuildResult;

class D3SemanticModelPatch final {
public:
  D3SemanticModelPatch(const D3SemanticModelPatch &) = default;
  D3SemanticModelPatch(D3SemanticModelPatch &&) noexcept = default;
  D3SemanticModelPatch &operator=(const D3SemanticModelPatch &) = default;
  D3SemanticModelPatch &operator=(D3SemanticModelPatch &&) noexcept = default;
  const std::vector<D3PatchEntrySeed> &entries() const { return Entries; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3SemanticModelPatch &lhs,
                         const D3SemanticModelPatch &rhs);

private:
  friend struct D3PatchBuildResult;
  friend class D3ValidatedCall;
  friend D3PatchBuildResult
  buildD3SemanticModelPatch(std::vector<D3PatchEntrySeed> entries);
  D3SemanticModelPatch() = default;
  std::vector<D3PatchEntrySeed> Entries;
};

struct D3PatchBuildResult {
  D3SemanticModelPatch Patch;
  D3PatchBuildError Error = D3PatchBuildError::None;
  explicit operator bool() const { return Error == D3PatchBuildError::None; }
};

D3PatchBuildResult
buildD3SemanticModelPatch(std::vector<D3PatchEntrySeed> entries);

class D3MinimalRegionWitness final {
public:
  D3MinimalRegionWitness(const D3MinimalRegionWitness &) = default;
  D3MinimalRegionWitness(D3MinimalRegionWitness &&) noexcept = default;
  D3MinimalRegionWitness &operator=(const D3MinimalRegionWitness &) = default;
  D3MinimalRegionWitness &
  operator=(D3MinimalRegionWitness &&) noexcept = default;
  const RegionId &callRegion() const { return CallRegion; }
  const RegionId &fullExpressionRegion() const { return FullExpressionRegion; }
  const std::string &origin() const { return Origin; }
  const D3SubjectIdentity &subject() const { return Subject; }
  const std::string &terminal() const { return Terminal; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3MinimalRegionWitness &lhs,
                         const D3MinimalRegionWitness &rhs);

private:
  friend class DirectCallObservationFactory;
  friend class D3ValidatedCall;
  D3MinimalRegionWitness() = default;
  RegionId CallRegion;
  RegionId FullExpressionRegion;
  std::string Origin;
  D3SubjectIdentity Subject = D3SubjectIdentity::sourcePlace("");
  std::string Terminal;
};

class D3ValidatedCall final {
public:
  D3ValidatedCall(const D3ValidatedCall &) = default;
  D3ValidatedCall(D3ValidatedCall &&) noexcept = default;
  D3ValidatedCall &operator=(const D3ValidatedCall &) = default;
  D3ValidatedCall &operator=(D3ValidatedCall &&) noexcept = default;
  const SemanticNodeId &callSiteId() const { return CallSite; }
  const ResolvedCalleeId &calleeId() const { return Callee; }
  const std::vector<D3ValidatedTransferEdge> &transferEdges() const {
    return TransferEdges;
  }
  const D3DomainDelta &evaluationDelta() const { return Evaluation; }
  const D3DomainDelta &boundaryDelta() const { return Boundary; }
  const D3DomainDelta &finalizationDelta() const { return Finalization; }
  const D3SemanticModelPatch &semanticModelPatch() const { return ModelPatch; }
  const D3MinimalRegionWitness &regionWitness() const { return RegionWitness; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3ValidatedCall &lhs,
                         const D3ValidatedCall &rhs);

private:
  friend class DirectCallObservationFactory;
  D3ValidatedCall() = default;
  SemanticNodeId CallSite;
  ResolvedCalleeId Callee;
  std::vector<D3ValidatedTransferEdge> TransferEdges;
  D3DomainDelta Evaluation;
  D3DomainDelta Boundary;
  D3DomainDelta Finalization;
  D3SemanticModelPatch ModelPatch;
  D3MinimalRegionWitness RegionWitness;
};

class D3FactoryObservationRecord final {
public:
  D3FactoryObservationRecord(const D3FactoryObservationRecord &) = default;
  D3FactoryObservationRecord(D3FactoryObservationRecord &&) noexcept = default;
  D3FactoryObservationRecord &
  operator=(const D3FactoryObservationRecord &) = default;
  D3FactoryObservationRecord &
  operator=(D3FactoryObservationRecord &&) noexcept = default;
  D3AdmissionKind admission() const { return Admission; }
  const std::optional<D3ExclusionReason> &exclusionReason() const {
    return ExclusionReason;
  }
  const std::optional<D3CallValidationError> &validationError() const {
    return ValidationError;
  }
  const std::optional<D3ValidatedCall> &validatedCall() const {
    return ValidatedCall;
  }
  const D3CallObservationInput &input() const { return Input; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3FactoryObservationRecord &lhs,
                         const D3FactoryObservationRecord &rhs);

private:
  friend class DirectCallObservationFactory;
  D3FactoryObservationRecord() = default;
  D3AdmissionKind Admission = D3AdmissionKind::Rejected;
  std::optional<D3ExclusionReason> ExclusionReason;
  std::optional<D3CallValidationError> ValidationError;
  std::optional<D3ValidatedCall> ValidatedCall;
  D3CallObservationInput Input;
};

class DirectCallObservationFactory final {
public:
  static D3FactoryObservationRecord observe(D3CallObservationInput input);
};

} // namespace toka

namespace std {
template <> struct hash<toka::D3ValidatedTransferEdge> {
  size_t operator()(const toka::D3ValidatedTransferEdge &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3DeltaEntry> {
  size_t operator()(const toka::D3DeltaEntry &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3SemanticModelPatch> {
  size_t operator()(const toka::D3SemanticModelPatch &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3MinimalRegionWitness> {
  size_t operator()(const toka::D3MinimalRegionWitness &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3FactoryObservationRecord> {
  size_t
  operator()(const toka::D3FactoryObservationRecord &value) const noexcept {
    return value.hashValue();
  }
};
} // namespace std
